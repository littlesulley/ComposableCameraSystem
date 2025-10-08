// Copyright Sulley. All rights reserved.

#include "Transitions/ComposableCameraTransitionBase.h"

FComposableCameraPose UComposableCameraTransitionBase::Evaluate(float DeltaTime, const FComposableCameraPose& CurrentTargetPose)
{
	if (bFirstFrame)
	{
		OnBeginPlay(DeltaTime, CurrentTargetPose);
		bFirstFrame = false;
	}
	
	// If no time remains, directly return the target pose.
	RemainingTime -= DeltaTime;
	if (RemainingTime <= 0.0f)
	{
		TransitionFinished();
		return CurrentTargetPose;
	}

	// Else, do evaluation.
	return OnEvaluate(DeltaTime, CurrentTargetPose);
}

void UComposableCameraTransitionBase::TransitionEnabled(AComposableCameraCameraBase* InSourceCamera, AComposableCameraCameraBase* InTargetCamera, const FComposableCameraPose& CurrentSourceCameraPose, float InTransitionTime)
{
	SourceCamera = InSourceCamera;
	TargetCamera = InTargetCamera;
	StartCameraPose = CurrentSourceCameraPose;
	TransitionTime = InTransitionTime;
	RemainingTime = InTransitionTime;
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

