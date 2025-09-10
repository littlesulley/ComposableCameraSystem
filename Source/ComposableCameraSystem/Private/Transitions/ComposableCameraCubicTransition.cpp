// Copyright Sulley. All rights reserved.


#include "Transitions/ComposableCameraCubicTransition.h"

FComposableCameraPose UComposableCameraCubicTransition::OnEvaluate(
	float DeltaTime, const FComposableCameraPose& CurrentTargetPose)
{
	float DurationPct = (GetTransitionTime() - GetRemainingTime()) / GetTransitionTime();
	float BlendWeight = FMath::CubicInterp(0.f, 0.f, 1.f, 0.f, DurationPct);

	FComposableCameraPose CurrentPose = StartCameraPose;
	CurrentPose.BlendBy(CurrentTargetPose, BlendWeight);

	return CurrentPose;
}