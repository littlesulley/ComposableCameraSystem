// Copyright 2026 Sulley. All Rights Reserved.

#include "Nodes/ComposableCameraHitchcockZoomNode.h"

#include "Components/SkeletalMeshComponent.h"
#include "ComposableCameraSystemModule.h"
#include "Curves/CurveFloat.h"
#include "GameFramework/Actor.h"

#if !UE_BUILD_SHIPPING
#include "Debug/ComposableCameraViewportDebug.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"

namespace
{
	static TAutoConsoleVariable<int32> CVarShowHitchcockZoomGizmo(
		TEXT("CCS.Debug.Viewport.HitchcockZoom"),
		0,
		TEXT("Show HitchcockZoomNode gizmo (purple spheres at target + camera, dolly axis line, and a current-FOV frustum on F8).\n")
		TEXT("Requires `CCS.Debug.Viewport 1`."),
		ECVF_Default);
}
#endif

namespace
{
	// FOV is authored in degrees; clamp to a sane open range before passing
	// through tan/atan. tan(FOV/2) explodes as FOV approaches 180 deg, and near
	// 0 deg it underflows toward zero. Either extreme yields nonsense camera
	// distances (zero or infinity). 1 deg..179 deg is far wider than any real
	// cinematic use of the effect yet avoids the singularities.
	constexpr double MinSafeFOVDegrees = 1.0;
	constexpr double MaxSafeFOVDegrees = 179.0;

	// Distances below this floor are treated as unusable. The LockConstant
	// formula divides by sin/tan/2 at these distances and the derived FOV
	// shoots past MaxSafeFOV. Matches the "below this, there is no useful
	// DoF" floor at roughly a typical near plane.
	constexpr double MinSafeDistance = 1.0;
}

void UComposableCameraHitchcockZoomNode::OnInitialize_Implementation()
{
	Super::OnInitialize_Implementation();

	ElapsedTime = 0.f;
	InitialDistance = 0.0;
	InitialFOVDegrees = 0.0;
	LockConstant = 0.0;
	bHasCapturedInitialState = false;
}

void UComposableCameraHitchcockZoomNode::OnTickNode_Implementation(
	float DeltaTime, const FComposableCameraPose& /*CurrentCameraPose*/, FComposableCameraPose& OutCameraPose)
{
#if !UE_BUILD_SHIPPING
	bDebugDrivenThisTick = false;
	DebugCameraPosition = OutCameraPose.Position;
	DebugCameraRotation = OutCameraPose.Rotation;
#endif

	if (!bEnable)
	{
		// Pass-through this tick; do NOT advance ElapsedTime. When the
		// user re-enables, the effect resumes from exactly where it
		// paused. Also do NOT clear bHasCapturedInitialState so a mid-
		// effect disable/enable doesn't recapture against a different
		// initial pose.
		return;
	}

	FVector TargetPoint;
	if (!ResolveTargetPoint(TargetPoint))
	{
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("HitchcockZoomNode: resolved PivotActor is null on '%s'; pass-through this tick."),
			*GetNameSafe(this));
		return;
	}

	const FVector UpstreamPos = OutCameraPose.Position;

	// --- First-tick capture ----------------------------------------------
	if (!bHasCapturedInitialState)
	{
		const double Dist = FVector::Distance(UpstreamPos, TargetPoint);
		if (Dist < MinSafeDistance)
		{
			// Upstream placed the camera essentially at the subject. We
			// cannot meaningfully derive a lock constant (tan(FOV/2) *
			// near-zero distance = near-zero constant ->any subsequent
			// FOV change would yield wild distances). Skip capture this
			// frame and try again on the next one; typical cameras settle
			// into their authored distance within a frame or two of a
			// transition.
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("HitchcockZoomNode: camera is within %.2f units of the target on first tick; waiting for upstream to settle."),
				MinSafeDistance);
			return;
		}

		InitialDistance = Dist;

		// Resolve the baseline FOV from either the explicit override (when
		// the camera type asset has no upstream FOV-writing node and the
		// author wants to specify the effect's starting FOV directly) or
		// from the upstream pose. Sentinel <= 0 means "use upstream",
		// same convention as LensNode's FocusDistance and the pose's
		// FieldOfView / FocalLength fields.
		const double BaselineFOV = (InitialFOVOverride > 0.f)
			? static_cast<double>(InitialFOVOverride)
			: OutCameraPose.GetEffectiveFieldOfView();
		InitialFOVDegrees = FMath::Clamp<double>(BaselineFOV, MinSafeFOVDegrees, MaxSafeFOVDegrees);

		const double HalfFOVRad = FMath::DegreesToRadians(InitialFOVDegrees * 0.5);
		LockConstant = InitialDistance * FMath::Tan(HalfFOVRad);
		bHasCapturedInitialState = true;

		// Fall through: first-tick output is InitialFOV / InitialDistance
		// exactly, because at NormalizedTime = 0 the additive curves
		// evaluate to 0 delta. No seam at t=0.
	}

	// --- NormalizedTime (Once-only, clamps at 1) -------------------------
	ElapsedTime += DeltaTime;
	const double NormalizedTime = Duration > UE_KINDA_SMALL_NUMBER
		? FMath::Min<double>(ElapsedTime / Duration, 1.0)
		: 1.0;

	// --- Solve FOV + Distance from the chosen driver ---------------------
	double FOVDegrees = InitialFOVDegrees;
	double Distance = InitialDistance;

	switch (Driver)
	{
	case EComposableCameraHitchcockZoomDriver::FromFOVDelta:
		{
			const double FOVDelta = FOVDeltaCurve
				? static_cast<double>(FOVDeltaCurve->GetFloatValue(static_cast<float>(NormalizedTime)))
				: 0.0;
			FOVDegrees = FMath::Clamp(InitialFOVDegrees + FOVDelta, MinSafeFOVDegrees, MaxSafeFOVDegrees);

			// Solve Distance from the lock constant. tan is monotonic on
			// (0 deg, 180 deg); clamping FOV above guarantees denominator is
			// strictly positive and finite.
			const double HalfFOVRad = FMath::DegreesToRadians(FOVDegrees * 0.5);
			const double TanHalf = FMath::Tan(HalfFOVRad);
			Distance = LockConstant / FMath::Max<double>(TanHalf, UE_KINDA_SMALL_NUMBER);
		}
		break;

	case EComposableCameraHitchcockZoomDriver::FromDistanceDelta:
		{
			const double DistDelta = DistanceDeltaCurve
				? static_cast<double>(DistanceDeltaCurve->GetFloatValue(static_cast<float>(NormalizedTime)))
				: 0.0;
			Distance = FMath::Max(InitialDistance + DistDelta, MinSafeDistance);

			// Solve FOV. atan is well-defined for all positive arguments;
			// the Max on Distance above keeps the arg bounded.
			const double HalfFOVRad = FMath::Atan(LockConstant / Distance);
			FOVDegrees = FMath::Clamp(
				FMath::RadiansToDegrees(HalfFOVRad * 2.0),
				MinSafeFOVDegrees, MaxSafeFOVDegrees);
		}
		break;
	}

	// --- User-facing distance clamp (safety rail for pathological curves) ---
	if (bClampCameraDistance)
	{
		const double Clamped = FMath::Clamp(
			Distance,
			static_cast<double>(CameraDistanceClamp.Min),
			static_cast<double>(CameraDistanceClamp.Max));

		if (!FMath::IsNearlyEqual(Clamped, Distance))
		{
			// When the clamp bites, re-derive FOV from the new distance
			// so the lock invariant still holds for the post-clamp pose.
			// Without this re-derivation the subject size would drift
			// when the clamp is engaged.
			Distance = Clamped;
			const double HalfFOVRad = FMath::Atan(LockConstant / Distance);
			FOVDegrees = FMath::Clamp(
				FMath::RadiansToDegrees(HalfFOVRad * 2.0),
				MinSafeFOVDegrees, MaxSafeFOVDegrees);
		}
	}

	// --- Resolve direction and apply -------------------------------------
	// Direction is re-read from the upstream pose every tick so an upstream
	// LookAt / CameraOffset can still steer the view direction during the
	// effect. Hitchcock owns only the radial distance + FOV.
	FVector Direction = (UpstreamPos - TargetPoint).GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		// Degenerate: camera coincides with target. Fall back to the
		// camera's forward vector so we still produce a usable direction
		// instead of leaving the camera at the target's position.
		Direction = OutCameraPose.Rotation.Vector();
		if (Direction.IsNearlyZero())
		{
			Direction = FVector::ForwardVector;
		}
	}

	OutCameraPose.Position = TargetPoint + Direction * Distance;
	OutCameraPose.FieldOfView = static_cast<float>(FOVDegrees);
	// Clear FocalLength so the pose is in FOV-mode regardless of what any
	// upstream LensNode authored. FOV is authoritative during the effect.
	OutCameraPose.FocalLength = -1.f;

#if !UE_BUILD_SHIPPING
	DebugTargetPoint = TargetPoint;
	DebugCameraPosition = OutCameraPose.Position;
	DebugCurrentDistance = Distance;
	DebugCurrentFOV = FOVDegrees;
	bDebugDrivenThisTick = true;
#endif
}

bool UComposableCameraHitchcockZoomNode::ResolveTargetPoint(FVector& OutTargetPoint) const
{
	AActor* EffectivePivotActor = ComposableCameraSystem::ResolveActorInput(
		PivotActorSource, PivotActor.Get(), GetOwningPlayerCameraManager(), this);
	if (!EffectivePivotActor)
	{
		return false;
	}

	if (bUseBoneForDetection && !BoneName.IsNone())
	{
		TArray<USkeletalMeshComponent*> SkelComps;
		EffectivePivotActor->GetComponents<USkeletalMeshComponent>(SkelComps);
		for (USkeletalMeshComponent* Skel : SkelComps)
		{
			if (Skel && Skel->DoesSocketExist(BoneName))
			{
				OutTargetPoint = Skel->GetSocketLocation(BoneName);
				return true;
			}
		}
		// Bone missing. Fall through to actor + Z offset.
	}

	OutTargetPoint = EffectivePivotActor->GetActorLocation() + FVector(0.f, 0.f, PivotZOffset);
	return true;
}

void UComposableCameraHitchcockZoomNode::GetPinDeclarations_Implementation(
	TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	// PivotActor remains graph-exposed by default for explicit-actor workflows;
	// PivotActorSource is Details-only unless promoted per instance.

	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "PivotActorSource";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Hitchcock_PivotActorSource", "Pivot Actor Source");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Enum;
		Pin.EnumType = StaticEnum<EComposableCameraActorInputSource>();
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = Pin.EnumType ? Pin.EnumType->GetNameStringByValue(static_cast<int64>(PivotActorSource)) : FString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Hitchcock_PivotActorSource_Tip",
			"Selects whether the lock subject comes from the controller's controlled pawn or an explicit actor.");
		OutPins.Add(Pin);
	}

	// Input: PivotActor.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "PivotActor";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Hitchcock_PivotActor", "Pivot Actor");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Actor;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = true;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Hitchcock_PivotActor_Tip",
			"Subject the effect locks on. Camera dollies along the camera-to-subject axis, FOV compensates to hold subject size.");
		OutPins.Add(Pin);
	}

	// Input: bUseBoneForDetection.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "bUseBoneForDetection";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Hitchcock_UseBone", "Use Bone For Detection");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Bool;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = bUseBoneForDetection ? TEXT("true") : TEXT("false");
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Hitchcock_UseBone_Tip",
			"When true, target point is sampled at BoneName on the pivot actor's skeletal mesh.");
		OutPins.Add(Pin);
	}

	// Input: BoneName.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "BoneName";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Hitchcock_BoneName", "Bone Name");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Name;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Hitchcock_BoneName_Tip",
			"Bone / socket name on the pivot actor's skeletal mesh.");
		OutPins.Add(Pin);
	}

	// Input: PivotZOffset.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "PivotZOffset";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Hitchcock_ZOffset", "Pivot Z Offset");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = FString::SanitizeFloat(PivotZOffset);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Hitchcock_ZOffset_Tip",
			"World-Z offset added to the actor location when bUseBoneForDetection is false.");
		OutPins.Add(Pin);
	}

	// Input: InitialFOVOverride.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "InitialFOVOverride";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Hitchcock_InitFOV", "Initial FOV Override");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = FString::SanitizeFloat(InitialFOVOverride);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Hitchcock_InitFOV_Tip",
			"Baseline FOV (degrees) for the effect. > 0 overrides whatever the upstream pose carried; -1 = use upstream.");
		OutPins.Add(Pin);
	}

	// Input: Driver enum.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "Driver";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Hitchcock_Driver", "Driver");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Enum;
		Pin.EnumType = StaticEnum<EComposableCameraHitchcockZoomDriver>();
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = Pin.EnumType
			? Pin.EnumType->GetNameStringByValue(static_cast<int64>(Driver))
			: FString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Hitchcock_Driver_Tip",
			"Which authored curve drives the effect: FOV delta or camera-distance delta. The other quantity is solved from the frame-zero lock constant.");
		OutPins.Add(Pin);
	}

	// Input: FOVDeltaCurve.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "FOVDeltaCurve";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Hitchcock_FOVCurve", "FOV Delta Curve");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Object;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Hitchcock_FOVCurve_Tip",
			"Curve asset: X = normalized time [0,1], Y = degrees of DELTA from InitialFOV. Author Y(0) = 0.");
		OutPins.Add(Pin);
	}

	// Input: DistanceDeltaCurve.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "DistanceDeltaCurve";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Hitchcock_DistCurve", "Distance Delta Curve");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Object;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Hitchcock_DistCurve_Tip",
			"Curve asset: X = normalized time [0,1], Y = world-units DELTA from InitialDistance. Author Y(0) = 0.");
		OutPins.Add(Pin);
	}

	// Input: Duration.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "Duration";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Hitchcock_Duration", "Duration");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = FString::SanitizeFloat(Duration);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Hitchcock_Duration_Tip",
			"Seconds the effect takes to play. After that, curves are evaluated at NormalizedTime = 1 and the pose freezes.");
		OutPins.Add(Pin);
	}

	// Input: bEnable.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "bEnable";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Hitchcock_Enable", "Enable");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Bool;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = bEnable ? TEXT("true") : TEXT("false");
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Hitchcock_Enable_Tip",
			"Master toggle. When false, the node is a pass-through and ElapsedTime does not advance (mid-effect pause / resume).");
		OutPins.Add(Pin);
	}

	// Input: bClampCameraDistance.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "bClampCameraDistance";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Hitchcock_Clamp", "Clamp Camera Distance");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Bool;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = bClampCameraDistance ? TEXT("true") : TEXT("false");
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Hitchcock_Clamp_Tip",
			"When true, camera-to-subject distance is clamped to CameraDistanceClamp.");
		OutPins.Add(Pin);
	}

	// Input: CameraDistanceClamp (FFloatInterval struct pin).
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "CameraDistanceClamp";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Hitchcock_Range", "Camera Distance Clamp");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Struct;
		Pin.StructType = TBaseStructure<FFloatInterval>::Get();
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Hitchcock_Range_Tip",
			"Min/max clamp on the derived camera-to-subject distance (safety rail for pathological curves).");
		OutPins.Add(Pin);
	}
}

#if !UE_BUILD_SHIPPING
void UComposableCameraHitchcockZoomNode::DrawNodeDebug(UWorld* World, bool bViewerIsOutsideCamera) const
{
	if (!World) { return; }
	if (CVarShowHitchcockZoomGizmo.GetValueOnGameThread() == 0
		&& !FComposableCameraViewportDebug::ShouldShowAllNodeGizmos()) { return; }
	if (!bDebugDrivenThisTick) { return; }

	const FColor HitchcockColor(180, 80, 220);  // purple

	// Sphere at the target point. The "lock subject".
	FComposableCameraViewportDebug::DrawSolidDebugSphere(
		World, DebugTargetPoint, /*Radius=*/8.f, HitchcockColor,
		/*Alpha=*/160, /*Segments=*/12, /*DepthPriority=*/0);

	// Sphere at the current camera position. The "dolly endpoint".
	FComposableCameraViewportDebug::DrawSolidDebugSphere(
		World, DebugCameraPosition, /*Radius=*/8.f, HitchcockColor,
		/*Alpha=*/120, /*Segments=*/12, /*DepthPriority=*/0);

	if (bViewerIsOutsideCamera)
	{
		// Dolly axis. Line from target out to camera. View-aligned in
		// possessed play, only useful from F8 / SIE (matches LookAt /
		// CollisionPush / FocusPull precedent).
		DrawDebugLine(World, DebugTargetPoint, DebugCameraPosition, HitchcockColor,
			/*bPersistentLines=*/false, /*LifeTime=*/-1.f, /*DepthPriority=*/0, /*Thickness=*/1.0f);

		// Small frustum at the current FOV. Makes the zoom-change visible
		// from outside the camera. Aspect 16:9 is a cosmetic default; the
		// subject-size lock is invariant to aspect anyway.
		constexpr float FrustumScale = 50.f;
		DrawDebugCamera(World, DebugCameraPosition, DebugCameraRotation,
			static_cast<float>(DebugCurrentFOV), FrustumScale,
			HitchcockColor, /*bPersistentLines=*/false, /*LifeTime=*/-1.f, /*DepthPriority=*/0);
	}
}
#endif
