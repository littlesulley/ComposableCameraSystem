// Copyright Sulley. All rights reserved.


#include "Transitions/ComposableCameraCubicTransition.h"

FComposableCameraPose UComposableCameraCubicTransition::OnEvaluate_Implementation(
	float DeltaTime, const FComposableCameraPose& CurrentTargetPose)
{
	return OnEvaluateBySource(DeltaTime, StartCameraPose, CurrentTargetPose);
}

FComposableCameraPose UComposableCameraCubicTransition::OnEvaluateBySource_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose)
{
	float DurationPct = (GetTransitionTime() - GetRemainingTime()) / GetTransitionTime();
	float BlendWeight = FMath::CubicInterp(0.f, 0.f, 1.f, 0.f, DurationPct);
	Percentage = BlendWeight;

	FComposableCameraPose CurrentPose = CurrentSourcePose;
	CurrentPose.BlendBy(CurrentTargetPose, BlendWeight);

	return CurrentPose;
}
