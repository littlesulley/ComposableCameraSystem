// Copyright Sulley. All rights reserved.

#include "Transitions/ComposableCameraTransitionBase.h"

FComposableCameraPose UComposableCameraTransitionBase::Evaluate(float DeltaTime, const FComposableCameraPose& CurrentTargetPose)
{
	// If no time remains, directly return the target pose.
	RemainingTime -= DeltaTime;
	if (RemainingTime <= 0.0f)
	{
		TransitionFinished();
		return CurrentTargetPose;
	}

	// Else, do evaluation.
	FComposableCameraPose OutPose;
	if (OnTransitionEvaluate(DeltaTime, CurrentTargetPose,  OutPose))
	{
		// Do nothing here, as OutPose already be set.
	}
	else
	{
		OutPose = OnEvaluate(DeltaTime, CurrentTargetPose);
	}

	return OutPose;
}

void UComposableCameraTransitionBase::TransitionEnabled(FComposableCameraPose CurrentCameraPose, float InTransitionTime)
{
	StartCameraPose = CurrentCameraPose;
	TransitionTime = InTransitionTime;
	RemainingTime = InTransitionTime;
	bFinished = false;
	OnTransitionEnabled();
}

void UComposableCameraTransitionBase::TransitionFinished()
{
	bFinished = true;
	OnTransitionFinished();
}

