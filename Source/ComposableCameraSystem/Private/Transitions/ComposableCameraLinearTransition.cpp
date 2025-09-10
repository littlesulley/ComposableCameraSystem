// Copyright Sulley. All rights reserved.

#include "Transitions/ComposableCameraLinearTransition.h"

FComposableCameraPose UComposableCameraLinearTransition::OnEvaluate(float DeltaTime,
	const FComposableCameraPose& CurrentTargetPose)
{
	float BlendWeight = (GetTransitionTime() - GetRemainingTime()) / GetTransitionTime();
	
	FComposableCameraPose CurrentPose = StartCameraPose;
	CurrentPose.BlendBy(CurrentTargetPose, BlendWeight);

	return CurrentPose;
}
