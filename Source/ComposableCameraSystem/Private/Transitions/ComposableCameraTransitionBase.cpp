// Copyright Sulley. All rights reserved.

#include "Transitions/ComposableCameraTransitionBase.h"

FComposableCameraPose UComposableCameraTransitionBase::Evaluate(float DeltaTime,
	const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose)
{
	if (bFirstFrame)
	{
		OnBeginPlay(DeltaTime, CurrentSourcePose, CurrentTargetPose);
	}

	// If no time remains, directly return the target pose.
	RemainingTime -= DeltaTime;
	if (RemainingTime <= 0.0f)
	{
		TransitionFinished();
		return CurrentTargetPose;
	}

	// Else, do evaluation.
	FComposableCameraPose OutResult = OnEvaluate(DeltaTime, CurrentSourcePose, CurrentTargetPose);
	bFirstFrame = false;

	return OutResult;
}

void UComposableCameraTransitionBase::TransitionEnabled(const FComposableCameraTransitionInitParams& InInitParams)
{
	InitParams = InInitParams;
	bFinished = false;
}

void UComposableCameraTransitionBase::TransitionFinished()
{
	bFinished = true;

	if (OnTransitionFinishesDelegate.IsBound())
	{
		OnTransitionFinishesDelegate.Broadcast();
		OnTransitionFinishesDelegate.Clear();
	}

	OnFinished();
}

void UComposableCameraTransitionBase::SetTransitionTime(float NewTransitionTime)
{
	TransitionTime = NewTransitionTime;
}

void UComposableCameraTransitionBase::ResetTransitionState()
{
	RemainingTime = TransitionTime;
	bFinished = false;
	bFirstFrame = true;
}
