// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraSplineNode.h"

#include "CameraRig_Rail.h"
#include "ComposableCameraSystemModule.h"
#include "Components/SplineComponent.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "Kismet/KismetSystemLibrary.h"

void UComposableCameraSplineNode::OnBeginPlayNode_Implementation(const FComposableCameraPose& CurrentCameraPose)
{
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

void UComposableCameraSplineNode::ReceiveInitializerNode(UComposableCameraCameraNodeBase* Initializer)
{
	
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
