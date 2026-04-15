// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraSplineNode.h"

#include "CameraRig_Rail.h"
#include "ComposableCameraSystemModule.h"
#include "Components/SplineComponent.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "Kismet/KismetSystemLibrary.h"

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
	// @TODO: implement all splines
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
			"How the camera moves along the spline — Automatic (time-driven) or ClosestPoint (tracks an actor's projection).");
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
			if (!ClosestMoveMethodPivotActor)
			{
				UE_LOG(LogComposableCameraSystem, Error, TEXT("ClosestMoveMethodPivotActor is null, will not proceed."))
				return;
			}

			USplineComponent* Spline = Rail->GetRailSplineComponent();
			float SplineLength = Spline->GetSplineLength();
			float InputKey = Spline->FindInputKeyClosestToWorldLocation(ClosestMoveMethodPivotActor->GetActorLocation());
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
			if (bLoop && ElapsedTimeForAutomaticMethod > Duration)
			{
				ElapsedTimeForAutomaticMethod -= Duration;
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

			if (TargetPosition >= 1.f)
			{
				TargetPosition -= 1.0f;
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
