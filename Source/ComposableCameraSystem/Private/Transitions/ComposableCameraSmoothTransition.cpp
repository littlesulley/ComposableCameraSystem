// Copyright Sulley. All rights reserved.

#include "Transitions/ComposableCameraSmoothTransition.h"
#include "Math/ComposableCameraMath.h"

FComposableCameraPose UComposableCameraSmoothTransition::OnEvaluate(float DeltaTime,
	const FComposableCameraPose& CurrentTargetPose)
{
	float DurationPct = (GetTransitionTime() - GetRemainingTime()) / GetTransitionTime();
	float BlendWeight = bSmootherStep ? SmootherStep(DurationPct) : SmoothStep(DurationPct);
	
	FComposableCameraPose CurrentPose = StartCameraPose;
	CurrentPose.BlendBy(CurrentTargetPose, BlendWeight);

	return CurrentPose;
}
