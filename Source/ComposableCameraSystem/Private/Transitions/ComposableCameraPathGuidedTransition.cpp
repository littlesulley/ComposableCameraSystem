// Copyright Sulley. All rights reserved.

#include "Transitions/ComposableCameraPathGuidedTransition.h"
#include "CameraRig_Rail.h"
#include "ComposableCameraSystemModule.h"
#include "Components/SplineComponent.h"
#include "Math/ComposableCameraMath.h"
#include "Transitions/ComposableCameraInertializedTransition.h"

void UComposableCameraPathGuidedTransition::OnBeginPlay_Implementation(float DeltaTime,
                                                                       const FComposableCameraPose& CurrentTargetPose)
{
	if (DrivingTransition)
	{
		DrivingTransition->TransitionEnabled(SourceCamera, TargetCamera, StartCameraPose);
		DrivingTransition->SetTransitionTime(TransitionTime);
		DrivingTransition->ResetTransitionState();
	}
	
	if (RailActor.IsValid())
	{
		Rail = RailActor.Get();
	}

	PreviousBasePosition = StartCameraPose.Position;
	PreviousResultPosition = StartCameraPose.Position;
}

FComposableCameraPose UComposableCameraPathGuidedTransition::OnEvaluate_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentTargetPose)
{
	if (!DrivingTransition)
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT("DrivingTransition is not valid in ComposableCameraPathGuidedTransition."));
		return CurrentTargetPose;
	}
	if (!Rail)
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT("SplineActor is not valid in ComposableCameraPathGuidedTransition."));
		return CurrentTargetPose;
	}
	
	float DurationPct = (GetTransitionTime() - GetRemainingTime()) / GetTransitionTime();

	// Base pose.
	FComposableCameraPose BasePose = DrivingTransition->Evaluate(DeltaTime, CurrentTargetPose);
	FComposableCameraPose ResultPose = BasePose;

	switch (GuideType)
	{
	case EComposableCameraPathGuidedTransitionType::SoftGuide:
		{
			// Get position on spline.
			const FVector PositionOnSpline = GetPositionOnSpline(DurationPct);

			// Not started yet.
			if (DurationPct < GuideRange.X)
			{
				ResultPose = BasePose;
			}
			// Attracted by both base pose and spline.
			else if (DurationPct <= GuideRange.Y)
			{
				ActualAttracton = FMath::FInterpTo(
					ActualAttracton,
					AttractionStrength,
					DeltaTime,
					1);
				FVector DesiredPosition = FMath::VInterpTo(
					BasePose.Position,
					PositionOnSpline,
					DeltaTime,
					ActualAttracton);
				FVector ResultPosition = FMath::VInterpTo(
					PreviousResultPosition,
					DesiredPosition,
					DeltaTime,
					FollowSpeed);
				ResultPose.Position = ResultPosition;
			}
			// Return to the base pose.
			else
			{
				// float Ratio = (DurationPct - GuideRange.Y) / (1.f - GuideRange.Y);
				// float Weight = ComposableCameraSystem::SmoothStep(Ratio);
				// FVector ResultPosition = FMath::Lerp(PreviousResultPosition, BasePose.Position, Weight);

				FVector ResultPosition = FMath::VInterpTo(
					PreviousResultPosition,
					BasePose.Position,
					DeltaTime,
					ResumeSpeed);
				ResultPose.Position = ResultPosition;
			}
			break;
		}
	case EComposableCameraPathGuidedTransitionType::HardGuide:
		{
			// Get position on spline.
			const FVector PositionOnSpline = GetPositionOnSpline(DurationPct);

			// Interp base position and spline position.
			const float InterpPct = InterpCurve->GetFloatValue(DurationPct);
			const FVector ResultPosition = FMath::Lerp(BasePose.Position, PositionOnSpline, InterpPct);

			// Final pose.
			ResultPose.Position = ResultPosition;
			break;
		}
	}

	PreviousBasePosition = BasePose.Position;
	PreviousResultPosition = ResultPose.Position;
	return ResultPose;
}

FVector UComposableCameraPathGuidedTransition::GetPositionOnSpline(float DurationPct)
{
	USplineComponent* Spline = Rail->GetRailSplineComponent();
	const float SplinePct = SplineMoveCurve->GetFloatValue(DurationPct);
	const float SplineLen = Spline->GetSplineLength();
	Rail->CurrentPositionOnRail = SplinePct;
	return Spline->GetLocationAtDistanceAlongSpline(SplinePct * SplineLen, ESplineCoordinateSpace::World);
}
