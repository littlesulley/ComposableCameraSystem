// Copyright Sulley. All rights reserved.

#include "Transitions/ComposableCameraLinearTransition.h"

FComposableCameraPose UComposableCameraLinearTransition::OnEvaluate_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose)
{
	float BlendWeight = (GetTransitionTime() - GetRemainingTime()) / GetTransitionTime();
	Percentage = BlendWeight;

	FComposableCameraPose CurrentPose = CurrentSourcePose;
	CurrentPose.BlendBy(CurrentTargetPose, BlendWeight);

	return CurrentPose;
}
