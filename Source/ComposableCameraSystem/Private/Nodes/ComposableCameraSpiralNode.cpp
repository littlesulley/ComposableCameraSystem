// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraSpiralNode.h"

#include "ComposableCameraSystemModule.h"
#include "Curves/CurveFloat.h"
#include "GameFramework/Actor.h"

#if !UE_BUILD_SHIPPING
#include "Debug/ComposableCameraViewportDebug.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"

namespace
{
	// Per-node opt-in toggle. The master `CCS.Debug.Viewport` CVar gates whether
	// any viewport debug draws at all; this CVar then controls whether this
	// specific node contributes its gizmo. Default off — users opt in per node.
	static TAutoConsoleVariable<int32> CVarShowSpiralGizmo(
		TEXT("CCS.Debug.Viewport.Spiral"),
		0,
		TEXT("Show SpiralNode gizmo (orange polyline of the helical path + spheres at pivot and current position).\n")
		TEXT("Requires `CCS.Debug.Viewport 1`. Works in both possessed play and F8 eject."),
		ECVF_Default);
}
#endif

void UComposableCameraSpiralNode::OnInitialize_Implementation()
{
	Super::OnInitialize_Implementation();

	ElapsedTime = 0.f;
	bHasCapturedInitialForward = false;
	CapturedInitialForward = FVector::ForwardVector;
}

void UComposableCameraSpiralNode::OnTickNode_Implementation(
	float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	// Capture the camera's forward vector on the first tick so CameraInitialForward
	// mode has a live, post-activation seed. Doing this here (rather than in
	// OnInitialize) means we read the pose the camera actually starts evaluating
	// from — which, for a freshly-pushed camera, already reflects any upstream
	// transition's current mid-blend pose.
	if (!bHasCapturedInitialForward)
	{
		const FVector Fwd = CurrentCameraPose.Rotation.Vector();
		CapturedInitialForward = Fwd.IsNearlyZero() ? FVector::ForwardVector : Fwd.GetSafeNormal();
		bHasCapturedInitialForward = true;
	}

	FVector ResolvedPivot;
	if (!ResolvePivot(ResolvedPivot))
	{
		return;
	}

	FVector Axis, Forward, Right;
	ResolveSpiralBasis(Axis, Forward, Right);

	const FVector EffectivePivot = ResolvedPivot
		+ Forward * PivotOffset.X
		+ Right   * PivotOffset.Y
		+ Axis    * PivotOffset.Z;

	// ─── Normalized time (Progress: no integration, direct eval) ──────────
	//
	// PlayMode defines how ElapsedTime maps to NormalizedTime ∈ [0,1]:
	//   Once      — clamp at 1 after Duration; curves hold their Y(1) terminal value.
	//   Loop      — NormalizedTime = Fmod(Elapsed, Duration) / Duration.
	//   PingPong  — NormalizedTime oscillates 0 → 1 → 0 over a 2 * Duration cycle;
	//               the X mirror alone gives the retrace — no Y sign flip needed
	//               because all three curves (including AngleCurve) are Progress.

	ElapsedTime += DeltaTime;

	float NormalizedTime = 0.f;

	if (Duration > SMALL_NUMBER)
	{
		switch (PlayMode)
		{
		case EComposableCameraSpiralPlayMode::Once:
			NormalizedTime = FMath::Min(ElapsedTime / Duration, 1.f);
			break;

		case EComposableCameraSpiralPlayMode::Loop:
			NormalizedTime = FMath::Fmod(ElapsedTime, Duration) / Duration;
			break;

		case EComposableCameraSpiralPlayMode::PingPong:
			{
				const float Phase = FMath::Fmod(ElapsedTime, 2.f * Duration) / Duration;
				NormalizedTime = (Phase <= 1.f) ? Phase : (2.f - Phase);
			}
			break;

		default:
			// Defensive: if a new PlayMode is added and this switch is not
			// updated, fall back to the frozen-initial-frame behaviour rather
			// than silently producing garbage poses.
			NormalizedTime = 0.f;
			break;
		}
	}

	const float RadiusValue = RadiusCurve ? RadiusCurve->GetFloatValue(NormalizedTime) : 0.f;
	const float HeightValue = HeightCurve ? HeightCurve->GetFloatValue(NormalizedTime) : 0.f;
	const float AngleValue  = AngleCurve  ? AngleCurve->GetFloatValue(NormalizedTime)  : 0.f;

	const float ThetaRad = FMath::DegreesToRadians(InitialAngleDegrees + AngleValue);
	const FVector PerpDir = Forward * FMath::Cos(ThetaRad) + Right * FMath::Sin(ThetaRad);

	const FVector FinalPosition = EffectivePivot
		+ Axis    * HeightValue
		+ PerpDir * RadiusValue;

	OutCameraPose.Position = FinalPosition;

#if !UE_BUILD_SHIPPING
	DebugEffectivePivot = EffectivePivot;
	DebugAxis = Axis;
	DebugForward = Forward;
	DebugRight = Right;
	DebugCurrentAngleDegrees = InitialAngleDegrees + AngleValue;
	DebugCurrentPosition = FinalPosition;
#endif
}

bool UComposableCameraSpiralNode::ResolvePivot(FVector& OutPivot) const
{
	switch (PivotSourceType)
	{
	case EComposableCameraSpiralPivotSourceType::FromActor:
		{
			AActor* EffectivePivotActor = ComposableCameraSystem::ResolveActorInput(
				PivotActorSource, PivotActor.Get(), GetOwningPlayerCameraManager());
			if (!IsValid(EffectivePivotActor))
			{
				UE_LOG(LogComposableCameraSystem, Error,
					TEXT("SpiralNode: PivotSourceType=FromActor but resolved PivotActor is null; node will not evaluate this frame."));
				return false;
			}
			OutPivot = EffectivePivotActor->GetActorLocation();
			return true;
		}

	case EComposableCameraSpiralPivotSourceType::FromVector:
		OutPivot = PivotPosition;
		return true;
	}

	OutPivot = FVector::ZeroVector;
	return false;
}

void UComposableCameraSpiralNode::ResolveSpiralBasis(
	FVector& OutAxis, FVector& OutForward, FVector& OutRight) const
{
	AActor* EffectivePivotActor = (PivotSourceType == EComposableCameraSpiralPivotSourceType::FromActor)
		? ComposableCameraSystem::ResolveActorInput(PivotActorSource, PivotActor.Get(), GetOwningPlayerCameraManager())
		: nullptr;

	// ── Axis ──
	FVector Axis;
	switch (RotationAxis)
	{
	case EComposableCameraSpiralRotationAxis::PivotActorUp:
		if (IsValid(EffectivePivotActor))
		{
			Axis = EffectivePivotActor->GetActorUpVector();
		}
		else
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("SpiralNode: RotationAxis=PivotActorUp but no valid PivotActor; falling back to WorldUp."));
			Axis = FVector::UpVector;
		}
		break;

	case EComposableCameraSpiralRotationAxis::Custom:
		Axis = CustomAxis;
		break;

	case EComposableCameraSpiralRotationAxis::WorldUp:
	default:
		Axis = FVector::UpVector;
		break;
	}

	Axis = Axis.GetSafeNormal();
	if (Axis.IsNearlyZero())
	{
		Axis = FVector::UpVector;
	}

	// ── Reference forward (pre-projection) ──
	FVector ReferenceForward;
	switch (ReferenceDirection)
	{
	case EComposableCameraSpiralReferenceDirection::PivotActorForward:
		if (IsValid(EffectivePivotActor))
		{
			ReferenceForward = EffectivePivotActor->GetActorForwardVector();
		}
		else
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("SpiralNode: ReferenceDirection=PivotActorForward but no valid PivotActor; falling back to WorldX."));
			ReferenceForward = FVector::ForwardVector;
		}
		break;

	case EComposableCameraSpiralReferenceDirection::CameraInitialForward:
		ReferenceForward = CapturedInitialForward;
		break;

	case EComposableCameraSpiralReferenceDirection::Custom:
		ReferenceForward = CustomDirection;
		break;

	case EComposableCameraSpiralReferenceDirection::WorldX:
	default:
		ReferenceForward = FVector::ForwardVector;
		break;
	}

	// ── Project onto perpendicular plane and renormalize. Fall back through
	// World X then World Y when the chosen direction is parallel to the axis. ──
	FVector Forward = FVector::VectorPlaneProject(ReferenceForward, Axis).GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		Forward = FVector::VectorPlaneProject(FVector::ForwardVector, Axis).GetSafeNormal();
	}
	if (Forward.IsNearlyZero())
	{
		Forward = FVector::VectorPlaneProject(FVector::RightVector, Axis).GetSafeNormal();
	}

	const FVector RightVec = FVector::CrossProduct(Axis, Forward).GetSafeNormal();

	OutAxis = Axis;
	OutForward = Forward;
	OutRight = RightVec;
}

void UComposableCameraSpiralNode::GetPinDeclarations_Implementation(
	TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	// Input: pivot actor source (FromActor mode).
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "PivotActorSource";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Spiral_PivotActorSource", "Pivot Actor Source");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Enum;
		Pin.EnumType = StaticEnum<EComposableCameraActorInputSource>();
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = Pin.EnumType ? Pin.EnumType->GetNameStringByValue(static_cast<int64>(PivotActorSource)) : FString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Spiral_PivotActorSource_Tip",
			"Selects whether PivotActor comes from the controller's controlled pawn or an explicit actor.");
		OutPins.Add(Pin);
	}

	// Input: pivot actor (FromActor mode).
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "PivotActor";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Spiral_PivotActor", "Pivot Actor");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Actor;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Spiral_PivotActor_Tip",
			"Actor whose world location is used as the pivot (FromActor mode).");
		OutPins.Add(Pin);
	}

	// Input: pivot position (FromVector mode).
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "PivotPosition";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Spiral_PivotPosition", "Pivot Position");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Vector3D;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = PivotPosition.ToString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Spiral_PivotPosition_Tip",
			"World-space pivot position (FromVector mode). Typically wired from an upstream PivotOffsetNode.");
		OutPins.Add(Pin);
	}

	// Input: pivot offset in Spiral Space (X=Forward, Y=Right, Z=Axis).
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "PivotOffset";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Spiral_PivotOffset", "Pivot Offset");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Vector3D;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = PivotOffset.ToString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Spiral_PivotOffset_Tip",
			"Offset applied to the resolved pivot in Spiral Space (X=Forward, Y=Right, Z=Axis).");
		OutPins.Add(Pin);
	}

	// Input: starting angular offset applied to θ.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "InitialAngleDegrees";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Spiral_InitialAngleDegrees", "Initial Angle Degrees");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = FString::SanitizeFloat(InitialAngleDegrees);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Spiral_InitialAngleDegrees_Tip",
			"Starting angular offset added to the integrated angle, in degrees.");
		OutPins.Add(Pin);
	}

	// Input: radius curve (X=normalized time, Y=radius in cm).
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "RadiusCurve";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Spiral_RadiusCurve", "Radius Curve");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Object;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Spiral_RadiusCurve_Tip",
			"Curve asset (X: normalized time [0,1], Y: radius in cm) defining the radial distance from Axis over time.");
		OutPins.Add(Pin);
	}

	// Input: height curve (X=normalized time, Y=signed height in cm).
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "HeightCurve";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Spiral_HeightCurve", "Height Curve");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Object;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Spiral_HeightCurve_Tip",
			"Curve asset (X: normalized time [0,1], Y: signed distance along Axis in cm) defining height over time.");
		OutPins.Add(Pin);
	}

	// Input: angle curve (X=normalized time, Y=absolute degrees).
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "AngleCurve";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Spiral_AngleCurve", "Angle Curve");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Object;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Spiral_AngleCurve_Tip",
			"Curve asset (X: normalized time [0,1], Y: angle in degrees, absolute) read directly as θ each tick.");
		OutPins.Add(Pin);
	}

	// Input: duration of one cycle of the three curves.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "Duration";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Spiral_Duration", "Duration");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = FString::SanitizeFloat(Duration);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Spiral_Duration_Tip",
			"Length of one cycle of the three curves, in seconds.");
		OutPins.Add(Pin);
	}
}

#if !UE_BUILD_SHIPPING
void UComposableCameraSpiralNode::DrawNodeDebug(UWorld* World, bool /*bViewerIsOutsideCamera*/) const
{
	if (!World) { return; }
	if (CVarShowSpiralGizmo.GetValueOnGameThread() == 0
		&& !FComposableCameraViewportDebug::ShouldShowAllNodeGizmos()) { return; }

	// The spiral polyline is laid out in the world around the pivot and
	// rarely coincides with the camera's own position, so no F8 gate is
	// needed — same reasoning as SplineNode's polyline.

	const FColor SpiralColor(255, 150, 60);  // warm orange, distinct from SplineNode violet and PivotOffset yellow

	// Sample the helical path across [0,1] normalized time. Each sample is an
	// O(1) direct read of the three curves — no integration, no dependence on
	// previous samples — so the polyline is the exact ground-truth shape the
	// runtime will trace (modulo the PlayMode wrap / mirror applied to
	// NormalizedTime, which we deliberately skip: the gizmo shows a single
	// forward cycle, which stays readable across Once / Loop / PingPong).
	constexpr int32 SampleCount = 128;

	auto SamplePoint = [&](float NormalizedTime) -> FVector
	{
		const float Radius = RadiusCurve ? RadiusCurve->GetFloatValue(NormalizedTime) : 0.f;
		const float Height = HeightCurve ? HeightCurve->GetFloatValue(NormalizedTime) : 0.f;
		const float Angle  = AngleCurve  ? AngleCurve->GetFloatValue(NormalizedTime)  : 0.f;
		const float ThetaRad = FMath::DegreesToRadians(InitialAngleDegrees + Angle);
		const FVector PerpDir = DebugForward * FMath::Cos(ThetaRad) + DebugRight * FMath::Sin(ThetaRad);
		return DebugEffectivePivot + DebugAxis * Height + PerpDir * Radius;
	};

	FVector PrevPoint = SamplePoint(0.f);
	for (int32 i = 1; i <= SampleCount; ++i)
	{
		const float NormalizedTime = static_cast<float>(i) / static_cast<float>(SampleCount);
		const FVector NextPoint = SamplePoint(NormalizedTime);
		DrawDebugLine(World, PrevPoint, NextPoint, SpiralColor,
			/*bPersistentLines=*/false, /*LifeTime=*/-1.f, /*DepthPriority=*/0, /*Thickness=*/1.5f);
		PrevPoint = NextPoint;
	}

	// Small sphere at the effective pivot, so the reader can see where the
	// spiral is anchored (post PivotOffset).
	FComposableCameraViewportDebug::DrawSolidDebugSphere(
		World, DebugEffectivePivot, /*Radius=*/8.f, FColor::Yellow,
		/*Alpha=*/120, /*Segments=*/12, /*DepthPriority=*/0);

	// Short line along the rotation axis at the pivot, so the axis is unambiguous.
	DrawDebugLine(World, DebugEffectivePivot, DebugEffectivePivot + DebugAxis * 40.f,
		FColor::White, /*bPersistentLines=*/false, /*LifeTime=*/-1.f,
		/*DepthPriority=*/0, /*Thickness=*/1.5f);

	// Highlighted sphere at the current evaluation position.
	FComposableCameraViewportDebug::DrawSolidDebugSphere(
		World, DebugCurrentPosition, /*Radius=*/9.f, SpiralColor,
		/*Alpha=*/160, /*Segments=*/12, /*DepthPriority=*/0);
}
#endif
