// Copyright 2026 Sulley. All Rights Reserved.

#include "Nodes/ComposableCameraSplineNode.h"

#include "Cameras/ComposableCameraCameraBase.h"
#include "CameraRig_Rail.h"
#include "ComposableCameraSystemModule.h"
#include "Components/SplineComponent.h"
#include "Curves/CurveFloat.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "Kismet/KismetSystemLibrary.h"

#if !UE_BUILD_SHIPPING
#include "Debug/ComposableCameraViewportDebug.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"

namespace
{
	static TAutoConsoleVariable<int32> CVarShowSplineGizmo(
		TEXT("CCS.Debug.Viewport.Spline"),
		0,
		TEXT("Show SplineNode gizmo (violet polyline sampled 64 times along the spline + sphere at current camera position).\n")
		TEXT("Requires `CCS.Debug.Viewport 1`. Works in both possessed play and F8 eject."),
		ECVF_Default);
}
#endif

void UComposableCameraSplineNode::OnInitialize_Implementation()
{
	Super::OnInitialize_Implementation();

	MoveInterpolator_T = MoveInterpolator ? MoveInterpolator->BuildDoubleInterpolator() : nullptr;
}

void UComposableCameraSplineNode::OnTickNode_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	bool bShouldProceed = true;
	
	switch (SplineType)
	{
	case EComposableCameraSplineNodeSplineType::BuiltInSpline:
		{
			if (!Rail)
			{
				UE_LOG(LogComposableCameraSystem, Error, TEXT("BuiltInSpline is used but Rail is nullptr, will not proceed."))
				bShouldProceed = false;
			}
		}
	// Non-built-in spline modes are declared but currently evaluate as no-op
	// stubs below; only BuiltInSpline validates an external rail here.
	case EComposableCameraSplineNodeSplineType::BasicSpline:
		break;
	case EComposableCameraSplineNodeSplineType::Bezier:
		break;
	case EComposableCameraSplineNodeSplineType::CubicHermite:
		break;
	case EComposableCameraSplineNodeSplineType::NURBS:
		break;
	}

	if (!bShouldProceed)
	{
		return;
	}

	FVector OutPosition = FVector::ZeroVector;
	FRotator OutRotation = FRotator::ZeroRotator;

	switch (SplineType)
	{
	case EComposableCameraSplineNodeSplineType::BuiltInSpline:
		UpdateCameraPoseByBuiltInSpline(OutPosition, OutRotation, CurrentCameraPose, DeltaTime);
		break;
	case EComposableCameraSplineNodeSplineType::BasicSpline:
		UpdateCameraPoseByBasicSpline(OutPosition, OutRotation, CurrentCameraPose, DeltaTime);
		break;
	case EComposableCameraSplineNodeSplineType::Bezier:
		UpdateCameraPoseByBezierSpline(OutPosition, OutRotation, CurrentCameraPose, DeltaTime);
		break;
	case EComposableCameraSplineNodeSplineType::CubicHermite:
		UpdateCameraPoseByHermiteSpline(OutPosition, OutRotation, CurrentCameraPose, DeltaTime);
		break;
	case EComposableCameraSplineNodeSplineType::NURBS:
		UpdateCameraPoseByNURBSpline(OutPosition, OutRotation, CurrentCameraPose, DeltaTime);
		break;
	}

	OutCameraPose.Position = OutPosition;

	if (bLockOrientationOnSpline)
	{
		OutCameraPose.Rotation = OutRotation;
	}
}

void UComposableCameraSplineNode::GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("SplineType");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraSplineNode", "SplineType", "Spline Type");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Enum;
		PinDecl.EnumType = StaticEnum<EComposableCameraSplineNodeSplineType>();
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = false;
		PinDecl.DefaultValueString = PinDecl.EnumType ? PinDecl.EnumType->GetNameStringByValue(static_cast<int64>(SplineType)) : FString();
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraSplineNode", "SplineTypeTip",
			"Which spline representation the camera follows (BuiltInSpline, Bezier, CubicHermite, BasicSpline, or NURBS).");
		OutPins.Add(PinDecl);
	}

	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("Rail");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraSplineNode", "Rail", "Rail");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Actor;
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = false;
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraSplineNode", "RailTip", "Rail actor whose spline the camera should follow (BuiltInSpline type).");
		OutPins.Add(PinDecl);
	}

	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("MoveMethod");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraSplineNode", "MoveMethod", "Move Method");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Enum;
		PinDecl.EnumType = StaticEnum<EComposableCameraSplineNodeMoveMethod>();
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = false;
		PinDecl.DefaultValueString = PinDecl.EnumType ? PinDecl.EnumType->GetNameStringByValue(static_cast<int64>(MoveMethod)) : FString();
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraSplineNode", "MoveMethodTip",
			"How the camera moves along the spline: Automatic (time-driven) or ClosestPoint (tracks an actor's projection).");
		OutPins.Add(PinDecl);
	}

	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("ClosestMoveMethodPivotActorSource");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraSplineNode", "ClosestMoveMethodPivotActorSource", "Closest Move Method Pivot Actor Source");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Enum;
		PinDecl.EnumType = StaticEnum<EComposableCameraActorInputSource>();
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = false;
		PinDecl.DefaultValueString = PinDecl.EnumType ? PinDecl.EnumType->GetNameStringByValue(static_cast<int64>(ClosestMoveMethodPivotActorSource)) : FString();
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraSplineNode", "ClosestMoveMethodPivotActorSourceTip", "Selects whether ClosestPoint tracks the controller's controlled pawn or an explicit actor.");
		OutPins.Add(PinDecl);
	}

	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("ClosestMoveMethodPivotActor");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraSplineNode", "ClosestMoveMethodPivotActor", "Closest Move Method Pivot Actor");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Actor;
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = false;
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraSplineNode", "ClosestMoveMethodPivotActorTip", "Actor used for ClosestPoint move method; the camera tracks the closest spline point to this actor.");
		OutPins.Add(PinDecl);
	}

	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("AutomaticMoveCurve");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraSplineNode", "AutomaticMoveCurve", "Automatic Move Curve");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Object;
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = false;
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraSplineNode", "AutomaticMoveCurveTip", "Curve asset (X: normalized time [0,1], Y: normalized distance [0,1]) used for the Automatic move method.");
		OutPins.Add(PinDecl);
	}

	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("bLoop");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraSplineNode", "bLoop", "Loop");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Bool;
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = false;
		PinDecl.DefaultValueString = bLoop ? TEXT("true") : TEXT("false");
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraSplineNode", "bLoopTip", "Whether the Automatic move method loops after Duration seconds.");
		OutPins.Add(PinDecl);
	}

	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("bLockOrientationOnSpline");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraSplineNode", "bLockOrientationOnSpline", "Lock Orientation On Spline");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Bool;
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = false;
		PinDecl.DefaultValueString = bLockOrientationOnSpline ? TEXT("true") : TEXT("false");
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraSplineNode", "bLockOrientationOnSplineTip", "When true, camera orientation follows the spline's tangent direction.");
		OutPins.Add(PinDecl);
	}

	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("MoveOffset");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraSplineNode", "MoveOffset", "Move Offset");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Float;
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = false;
		PinDecl.DefaultValueString = FString::SanitizeFloat(MoveOffset);
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraSplineNode", "MoveOffsetTip", "Normalized offset [-1,1] applied to movement.");
		OutPins.Add(PinDecl);
	}

	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("Duration");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraSplineNode", "Duration", "Duration");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Float;
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = false;
		PinDecl.DefaultValueString = FString::SanitizeFloat(Duration);
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraSplineNode", "DurationTip", "Duration for Automatic move method.");
		OutPins.Add(PinDecl);
	}
}


void UComposableCameraSplineNode::UpdateCameraPoseByBuiltInSpline(FVector& OutPosition, FRotator& OutRotation,
	const FComposableCameraPose& CurrentCameraPose, float DeltaTime)
{
	switch (MoveMethod)
	{
	case EComposableCameraSplineNodeMoveMethod::ClosestPoint:
		{
			AActor* EffectivePivotActor = ComposableCameraSystem::ResolveActorInput(
				ClosestMoveMethodPivotActorSource, ClosestMoveMethodPivotActor.Get(), GetOwningPlayerCameraManager(), this);
			if (!IsValid(EffectivePivotActor))
			{
				UE_LOG(LogComposableCameraSystem, Error, TEXT("Resolved ClosestMoveMethodPivotActor is null, will not proceed."))
				return;
			}

			USplineComponent* Spline = Rail->GetRailSplineComponent();
			float SplineLength = Spline->GetSplineLength();
			float InputKey = Spline->FindInputKeyClosestToWorldLocation(EffectivePivotActor->GetActorLocation());
			float Distance = Spline->GetDistanceAlongSplineAtSplineInputKey(InputKey);
			float TargetPosition = Distance / SplineLength;
			float CurrentPosition = Rail->CurrentPositionOnRail;

			TargetPosition += MoveOffset;

			if (MoveInterpolator_T)
			{
				MoveInterpolator_T->Reset(CurrentPosition, TargetPosition);
				TargetPosition = MoveInterpolator_T->Run(DeltaTime);
			}

			TargetPosition = FMath::Clamp(TargetPosition, 0.0f, 1.0f);
			Rail->CurrentPositionOnRail = TargetPosition;
			
			OutPosition = Spline->GetLocationAtDistanceAlongSpline(SplineLength * TargetPosition, ESplineCoordinateSpace::World);
			if (bLockOrientationOnSpline)
			{
				OutRotation = Spline->GetQuaternionAtDistanceAlongSpline(SplineLength * TargetPosition, ESplineCoordinateSpace::World).Rotator();
			}

			break;
		}
	case EComposableCameraSplineNodeMoveMethod::Automatic:
		{
			if (!AutomaticMoveCurve)
			{
				UE_LOG(LogComposableCameraSystem, Error, TEXT("A movement curve must be provided, will not proceed."))
				return;
			}

			ElapsedTimeForAutomaticMethod += DeltaTime;
			if (ElapsedTimeForAutomaticMethod > Duration)
			{
				if (bLoop)
				{
					ElapsedTimeForAutomaticMethod -= Duration;
				}
				else
				{
					ElapsedTimeForAutomaticMethod = Duration;
				}
			}
			
			USplineComponent* Spline = Rail->GetRailSplineComponent();
			float SplineLength = Spline->GetSplineLength();
			float CurrentPosition = Rail->CurrentPositionOnRail;
			float TargetPosition = AutomaticMoveCurve->GetFloatValue(ElapsedTimeForAutomaticMethod / Duration);

			if (bFirstLapIfLoop && TargetPosition < CurrentPosition)
			{
				bFirstLapIfLoop = false;
			}

			TargetPosition += MoveOffset;

			if (TargetPosition < 0.f)
			{
				if (bLoop && bFirstLapIfLoop || !bLoop)
				{
					TargetPosition = 0.f;
				}
				else
				{
					TargetPosition += 1.0f;
				}
			}

			if (CurrentPosition > TargetPosition)
			{
				TargetPosition += 1.0f;
			}
			
			if (MoveInterpolator_T)
			{
				MoveInterpolator_T->Reset(CurrentPosition, TargetPosition);
				TargetPosition = MoveInterpolator_T->Run(DeltaTime);
			}

			if (bLoop)
			{
				if (TargetPosition >= 1.f)
				{
					TargetPosition -= 1.0f;
				}
			}
			else
			{
				TargetPosition = FMath::Clamp(TargetPosition, 0.f, 1.f);
			}

			Rail->CurrentPositionOnRail = TargetPosition;

			OutPosition = Spline->GetLocationAtDistanceAlongSpline(SplineLength * TargetPosition, ESplineCoordinateSpace::World);
			if (bLockOrientationOnSpline)
			{
				OutRotation = Spline->GetQuaternionAtDistanceAlongSpline(SplineLength * TargetPosition, ESplineCoordinateSpace::World).Rotator();
			}

			break;
		}
	}
}

void UComposableCameraSplineNode::UpdateCameraPoseByBezierSpline(FVector& OutPosition, FRotator& OutRotation,
	const FComposableCameraPose& CurrentCameraPose, float DeltaTime)
{
}

void UComposableCameraSplineNode::UpdateCameraPoseByHermiteSpline(FVector& OutPosition, FRotator& OutRotation,
	const FComposableCameraPose& CurrentCameraPose, float DeltaTime)
{
}

void UComposableCameraSplineNode::UpdateCameraPoseByBasicSpline(FVector& OutPosition, FRotator& OutRotation,
	const FComposableCameraPose& CurrentCameraPose, float DeltaTime)
{
}

void UComposableCameraSplineNode::UpdateCameraPoseByNURBSpline(FVector& OutPosition, FRotator& OutRotation,
	const FComposableCameraPose& CurrentCameraPose, float DeltaTime)
{
}

#if !UE_BUILD_SHIPPING
void UComposableCameraSplineNode::DrawNodeDebug(UWorld* World, bool /*bViewerIsOutsideCamera*/) const
{
	if (!World || !SplineInterface) { return; }
	if (CVarShowSplineGizmo.GetValueOnGameThread() == 0
		&& !FComposableCameraViewportDebug::ShouldShowAllNodeGizmos()) { return; }
	// The spline polyline is laid out in the world where the camera travels;
	// the small current-position sphere is ~18 units (a dot at typical FOV).
	// Neither piece occludes the view meaningfully, so no gate.

	// Sample the spline at a fixed number of steps and chain them into a
	// polyline. 64 samples balances smoothness with draw cost. Closed-loop
	// splines still read correctly because each segment is drawn independently.
	constexpr int32 SampleCount = 64;
	const float TotalLength = SplineInterface->GetSplineLength();
	if (TotalLength <= SMALL_NUMBER) { return; }

	const FColor SplineColor(170, 120, 255); // violet, distinct from every other gizmo hue
	FVector PrevPoint = SplineInterface->GetWorldSpacePositionByDistanceOnSpline(0.f);
	for (int32 i = 1; i <= SampleCount; ++i)
	{
		const float Distance = TotalLength * (static_cast<float>(i) / static_cast<float>(SampleCount));
		const FVector NextPoint = SplineInterface->GetWorldSpacePositionByDistanceOnSpline(Distance);
		DrawDebugLine(World, PrevPoint, NextPoint, SplineColor,
			/*bPersistentLines=*/false, /*LifeTime=*/-1.f, /*DepthPriority=*/0, /*Thickness=*/1.5f);
		PrevPoint = NextPoint;
	}

	// Sphere at the camera's current position on the spline so the reader can
	// see where along the path evaluation currently is.
	if (OwningCamera)
	{
		FComposableCameraViewportDebug::DrawSolidDebugSphere(
			World, OwningCamera->GetCameraPose().Position,
			/*Radius=*/9.f, SplineColor,
			/*Alpha=*/120, /*Segments=*/12, /*DepthPriority=*/0);
	}
}
#endif
