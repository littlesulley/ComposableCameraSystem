// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraFocusPullNode.h"

#include "Components/SkeletalMeshComponent.h"
#include "ComposableCameraSystemModule.h"
#include "DataAssets/ComposableCameraTargetInfo.h"
#include "GameFramework/Actor.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"

#if !UE_BUILD_SHIPPING
#include "Debug/ComposableCameraViewportDebug.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"

namespace
{
	static TAutoConsoleVariable<int32> CVarShowFocusPullGizmo(
		TEXT("CCS.Debug.Viewport.FocusPull"),
		0,
		TEXT("Show FocusPullNode gizmo (amber sphere at target point + translucent plane at FocusDistance; line camera→target on F8 only).\n")
		TEXT("Requires `CCS.Debug.Viewport 1`."),
		ECVF_Default);
}
#endif

void UComposableCameraFocusPullNode::OnInitialize_Implementation()
{
	Super::OnInitialize_Implementation();

	FocusInterpolator_T = IsValid(FocusInterpolator) ? FocusInterpolator->BuildDoubleInterpolator() : nullptr;

	bHasSeededSmoothing = false;
	LastSmoothedFocusDistance = 0.0;
}

void UComposableCameraFocusPullNode::OnTickNode_Implementation(
	float DeltaTime, const FComposableCameraPose& /*CurrentCameraPose*/, FComposableCameraPose& OutCameraPose)
{
#if !UE_BUILD_SHIPPING
	bDebugWasDrivenThisTick = false;
	DebugCameraPosition = OutCameraPose.Position;
	DebugCameraRotation = OutCameraPose.Rotation;
#endif

	if (!bEnableFocusPull)
	{
		// Pass-through: leave FocusDistance as whatever the upstream wrote.
		// Keep smoothing state seeded against the current focus so a
		// re-enable doesn't start from a stale cache.
		LastSmoothedFocusDistance = OutCameraPose.FocusDistance > 0.f
			? OutCameraPose.FocusDistance
			: LastSmoothedFocusDistance;
		return;
	}

	FVector TargetPoint;
	if (!ResolveTargetPoint(TargetPoint))
	{
		// PivotActor is required; log once per frame (matches SplineNode /
		// CollisionPush precedent — error paths are cheap enough and the
		// log surface it so authors catch misconfiguration fast).
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("FocusPullNode: PivotActor is null on '%s'; focus pass-through this tick."),
			*GetNameSafe(this));
		return;
	}

	const FVector CameraPos = OutCameraPose.Position;

	// Project (Target - Camera) onto the camera's forward axis.
	// `FocusDistance` is consumed by the renderer as camera-space depth —
	// how far along the view axis the focus plane sits — NOT as Euclidean
	// distance. An off-axis target (e.g. 10 m away, 45° to the side) has
	// Euclidean distance 10 m but on-axis depth ~7 m; using Euclidean
	// would rack focus too far for off-axis subjects.
	const FVector CameraForward = OutCameraPose.Rotation.Vector();
	const double RawDepth = FVector::DotProduct(TargetPoint - CameraPos, CameraForward);

	// Target behind the camera (or exactly on the view plane) — there is
	// no meaningful "depth" to focus on. Pass through this tick, preserving
	// whatever FocusDistance the upstream wrote, so transient look-away
	// moments don't rack focus to zero.
	if (RawDepth <= UE_KINDA_SMALL_NUMBER)
	{
		return;
	}

	const double RawDistance = RawDepth + static_cast<double>(FocusDistanceOffset);

	// The offset can push distance non-positive on pathological inputs
	// (offset more negative than the on-axis depth). Clamp to a tiny
	// positive floor before the user-clamp — a negative FocusDistance
	// would reach ApplyPhysicalCameraSettings as a sentinel (≤ 0 means
	// "no override"), silently disabling DoF for the frame.
	double ClampedDistance = FMath::Max(RawDistance, UE_KINDA_SMALL_NUMBER);

	if (bClampFocusDistance)
	{
		ClampedDistance = FMath::Clamp(
			ClampedDistance,
			static_cast<double>(FocusDistanceClamp.Min),
			static_cast<double>(FocusDistanceClamp.Max));
	}

	double FinalDistance = ClampedDistance;

	// First tick bypasses the interpolator so the initial focus snaps to the
	// real distance — without this we'd lerp from 0 (or whatever the
	// interpolator's seed is) up to the real distance over the first few
	// frames, producing a visible focus ramp. From the second tick onward,
	// the interpolator (if any) steers the focus.
	if (FocusInterpolator_T && bHasSeededSmoothing)
	{
		FocusInterpolator_T->Reset(LastSmoothedFocusDistance, ClampedDistance);
		FinalDistance = FocusInterpolator_T->Run(DeltaTime);
	}

	LastSmoothedFocusDistance = FinalDistance;
	bHasSeededSmoothing = true;
	OutCameraPose.FocusDistance = static_cast<float>(FinalDistance);

#if !UE_BUILD_SHIPPING
	DebugTargetPoint = TargetPoint;
	DebugResolvedFocusDistance = FinalDistance;
	bDebugWasDrivenThisTick = true;
#endif
}

bool UComposableCameraFocusPullNode::ResolveTargetPoint(FVector& OutTargetPoint) const
{
	// Phase A migration (V1.x): delegates to the consolidated helper in
	// DataAssets/ComposableCameraTargetInfo.h. Bit-exact behavior parity
	// with the prior inline implementation — three cases:
	//   1. Bone mode + valid bone   → socket location only, no Z offset.
	//   2. Bone mode + invalid bone → fall back to ActorLocation + Z offset.
	//   3. Actor mode               → ActorLocation + Z offset.
	// The struct's Offset is set to ZeroVector and the legacy Z offset is
	// added by THIS call site only when ResolveWorldPoint reports it did
	// NOT use the bone path (OutUsedBone == false). This preserves the
	// original "Z offset applies only on the actor branch" semantic exactly.
	FComposableCameraTargetInfo Info;
	// Explicit `.Get()` to convert TObjectPtr → AActor* → TSoftObjectPtr —
	// the unambiguous chain. The TargetInfo struct now uses TSoftObjectPtr
	// (V1.x) so its Details-panel picker can span level actors.
	Info.Actor               = PivotActor.Get();
	Info.bUseBoneAsPivot     = bUseBoneForDetection;
	Info.BoneName            = BoneName;
	Info.Offset              = FVector::ZeroVector;
	Info.bOffsetInLocalSpace = false;

	bool bUsedBone = false;
	if (!Info.ResolveWorldPoint(OutTargetPoint, &bUsedBone))
	{
		return false;
	}

	if (!bUsedBone)
	{
		OutTargetPoint += FVector(0.f, 0.f, PivotZOffset);
	}
	return true;
}

void UComposableCameraFocusPullNode::GetPinDeclarations_Implementation(
	TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	// Input: PivotActor — the only Required pin, and the only pin that is
	// exposed on the graph node by default. Everything else is pin-capable
	// but Details-only out of the box (per-instance flip via RuntimePinOverrides).
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "PivotActor";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "FocusPull_PivotActor", "Pivot Actor");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Actor;
		Pin.bRequired = true;
		Pin.bDefaultAsPin = true;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "FocusPull_PivotActor_Tip",
			"Actor whose distance from the camera drives FocusDistance.");
		OutPins.Add(Pin);
	}

	// Input: bUseBoneForDetection.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "bUseBoneForDetection";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "FocusPull_UseBone", "Use Bone For Detection");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Bool;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = bUseBoneForDetection ? TEXT("true") : TEXT("false");
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "FocusPull_UseBone_Tip",
			"When true, target point is sampled at BoneName on PivotActor's skeletal mesh.");
		OutPins.Add(Pin);
	}

	// Input: BoneName.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "BoneName";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "FocusPull_BoneName", "Bone Name");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Name;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "FocusPull_BoneName_Tip",
			"Bone / socket name on PivotActor's skeletal mesh (used when bUseBoneForDetection is true).");
		OutPins.Add(Pin);
	}

	// Input: PivotZOffset.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "PivotZOffset";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "FocusPull_ZOffset", "Pivot Z Offset");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = FString::SanitizeFloat(PivotZOffset);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "FocusPull_ZOffset_Tip",
			"World-Z offset added to the actor location when bUseBoneForDetection is false.");
		OutPins.Add(Pin);
	}

	// Input: bEnableFocusPull.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "bEnableFocusPull";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "FocusPull_Enable", "Enable Focus Pull");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Bool;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = bEnableFocusPull ? TEXT("true") : TEXT("false");
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "FocusPull_Enable_Tip",
			"Master toggle. When false the node is a pass-through this tick.");
		OutPins.Add(Pin);
	}

	// Input: FocusDistanceOffset.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "FocusDistanceOffset";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "FocusPull_Offset", "Focus Distance Offset");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = FString::SanitizeFloat(FocusDistanceOffset);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "FocusPull_Offset_Tip",
			"Constant offset added to the camera→target distance before clamp and damping.");
		OutPins.Add(Pin);
	}

	// Input: bClampFocusDistance.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "bClampFocusDistance";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "FocusPull_Clamp", "Clamp Focus Distance");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Bool;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = bClampFocusDistance ? TEXT("true") : TEXT("false");
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "FocusPull_Clamp_Tip",
			"When true, focus distance is clamped to FocusDistanceClamp.");
		OutPins.Add(Pin);
	}

	// Input: FocusDistanceClamp (FFloatInterval struct pin).
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "FocusDistanceClamp";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "FocusPull_Range", "Focus Distance Clamp");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Struct;
		Pin.StructType = TBaseStructure<FFloatInterval>::Get();
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "FocusPull_Range_Tip",
			"Min/max clamp applied to focus distance when bClampFocusDistance is true.");
		OutPins.Add(Pin);
	}
}

#if !UE_BUILD_SHIPPING
void UComposableCameraFocusPullNode::DrawNodeDebug(UWorld* World, bool bViewerIsOutsideCamera) const
{
	if (!World) { return; }
	if (CVarShowFocusPullGizmo.GetValueOnGameThread() == 0
		&& !FComposableCameraViewportDebug::ShouldShowAllNodeGizmos()) { return; }
	if (!bDebugWasDrivenThisTick) { return; }

	const FColor FocusColor(255, 200, 60);  // amber

	// Small sphere at the resolved target point (the "what we're focused
	// on" marker). Always visible; it lives out in the world, not at the
	// camera. Matches CollisionPush's pivot-sphere pattern.
	FComposableCameraViewportDebug::DrawSolidDebugSphere(
		World, DebugTargetPoint, /*Radius=*/8.f, FocusColor,
		/*Alpha=*/160, /*Segments=*/12, /*DepthPriority=*/0);

	// Translucent plane at the current focus distance, perpendicular to
	// the camera's forward vector — answers "where on the view axis is DoF
	// currently racked". Drawn in both modes: from inside the camera the
	// plane is near-invisible edge-on, but that's OK, the author gets the
	// info from F8 / SIE.
	const FVector CameraForward = DebugCameraRotation.Vector();
	if (!CameraForward.IsNearlyZero() && DebugResolvedFocusDistance > UE_KINDA_SMALL_NUMBER)
	{
		const FVector FocusPlaneCenter = DebugCameraPosition + CameraForward * DebugResolvedFocusDistance;
		const FPlane FocusPlane(FocusPlaneCenter, CameraForward.GetSafeNormal());

		// ~60-unit square is visible without dominating the view. Alpha 40
		// keeps it as a hint, not a wall.
		FColor PlaneColor = FocusColor;
		PlaneColor.A = 40;
		DrawDebugSolidPlane(World, FocusPlane, FocusPlaneCenter,
			FVector2D(60.f, 60.f), PlaneColor,
			/*bPersistentLines=*/false, /*LifeTime=*/-1.f, /*DepthPriority=*/0);
	}

	// F8 / SIE only: line from camera to target. View-aligned in possessed
	// play, invisible anyway — same reasoning as LookAt / CollisionPush.
	if (bViewerIsOutsideCamera)
	{
		DrawDebugLine(World, DebugCameraPosition, DebugTargetPoint, FocusColor,
			/*bPersistentLines=*/false, /*LifeTime=*/-1.f, /*DepthPriority=*/0, /*Thickness=*/1.0f);
	}
}
#endif
