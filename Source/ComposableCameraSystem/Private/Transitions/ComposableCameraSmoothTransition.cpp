// Copyright Sulley. All rights reserved.

#include "Transitions/ComposableCameraSmoothTransition.h"
#include "Math/ComposableCameraMath.h"

FComposableCameraPose UComposableCameraSmoothTransition::OnEvaluate_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose)
{
	float DurationPct = (GetTransitionTime() - GetRemainingTime()) / GetTransitionTime();
	float BlendWeight = bSmootherStep ? ComposableCameraSystem::SmootherStep(DurationPct) : ComposableCameraSystem::SmoothStep(DurationPct);
	Percentage = BlendWeight;

	FComposableCameraPose CurrentPose = CurrentSourcePose;
	CurrentPose.BlendBy(CurrentTargetPose, BlendWeight);

	return CurrentPose;
}
