// Copyright 2026 Sulley. All Rights Reserved.

#include "Transitions/ComposableCameraViewTargetTransition.h"

void UComposableCameraViewTargetTransition::InitFromViewTargetParams(const FViewTargetTransitionParams& InParams)
{
	ViewTargetParams = InParams;
	SetTransitionTime(InParams.BlendTime);
	ResetTransitionState();
}

FComposableCameraPose UComposableCameraViewTargetTransition::OnEvaluate_Implementation(
	float DeltaTime,
	const FComposableCameraPose& CurrentSourcePose,
	const FComposableCameraPose& CurrentTargetPose)
{
	// Compute the time-based factor (0 ->1 over BlendTime).
	const float Elapsed = GetTransitionTime() - GetRemainingTime();
	const float TimeFactor = (GetTransitionTime() > 0.f)
		? FMath::Clamp(Elapsed / GetTransitionTime(), 0.f, 1.f)
		: 1.f;

	// Delegate to the engine's blend alpha computation, which respects
	// BlendFunction (Linear, EaseIn, EaseOut, EaseInOut, Cubic) and BlendExp.
	const float BlendWeight = ViewTargetParams.GetBlendAlpha(TimeFactor);
	Percentage = BlendWeight;

	FComposableCameraPose OutPose = BlendPosesByLockedRotationPath(
		CurrentSourcePose,
		CurrentTargetPose,
		BlendWeight);
	return OutPose;
}
