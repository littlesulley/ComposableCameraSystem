// Copyright Sulley. All rights reserved.

#include "Transitions/ComposableCameraTransitionBase.h"

FComposableCameraPose UComposableCameraTransitionBase::Evaluate(float DeltaTime, const FComposableCameraPose& CurrentTargetPose)
{
	if (bFirstFrame)
	{
		OnBeginPlay(DeltaTime, CurrentTargetPose);
	}
	
	// If no time remains, directly return the target pose.
	RemainingTime -= DeltaTime;
	if (RemainingTime <= 0.0f)
	{
		TransitionFinished();
		return CurrentTargetPose;
	}

	// Else, do evaluation.
	FComposableCameraPose OutResult = OnEvaluate(DeltaTime, CurrentTargetPose);
	bFirstFrame = false;
	
	return OutResult;
}

FComposableCameraPose UComposableCameraTransitionBase::EvaluateBySource(float DeltaTime,
	const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose)
{
	if (bFirstFrame)
	{
		OnBeginPlay(DeltaTime, CurrentTargetPose);
	}
	
	// If no time remains, directly return the target pose.
	RemainingTime -= DeltaTime;
	if (RemainingTime <= 0.0f)
	{
		TransitionFinished();
		return CurrentTargetPose;
	}

	// Else, do evaluation.
	FComposableCameraPose OutResult = OnEvaluateBySource(DeltaTime, CurrentSourcePose, CurrentTargetPose);
	bFirstFrame = false;
	
	return OutResult;
}

void UComposableCameraTransitionBase::TransitionEnabled(AComposableCameraCameraBase* InSourceCamera, AComposableCameraCameraBase* InTargetCamera, const FComposableCameraPose& CurrentSourceCameraPose)
{
	SourceCamera = InSourceCamera;
	TargetCamera = InTargetCamera;
	StartCameraPose = CurrentSourceCameraPose;
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

