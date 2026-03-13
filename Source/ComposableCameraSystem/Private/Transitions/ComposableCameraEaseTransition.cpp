// Copyright Sulley. All rights reserved.

#include "Transitions/ComposableCameraEaseTransition.h"

FComposableCameraPose UComposableCameraEaseTransition::UComposableCameraEaseTransition::OnEvaluate_Implementation(
	float DeltaTime, const FComposableCameraPose& CurrentTargetPose)
{
	return OnEvaluateBySource(DeltaTime, StartCameraPose, CurrentTargetPose);
}

FComposableCameraPose UComposableCameraEaseTransition::OnEvaluateBySource_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose)
{
	float DurationPct = (GetTransitionTime() - GetRemainingTime()) / GetTransitionTime();
	float BlendWeight = FMath::InterpEaseInOut(0.f, 1.f, DurationPct, Exp);
	Percentage = BlendWeight;

	FComposableCameraPose CurrentPose = CurrentSourcePose;
	CurrentPose.BlendBy(CurrentTargetPose, BlendWeight);

	return CurrentPose;
}
