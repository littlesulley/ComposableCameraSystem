// Copyright Sulley. All rights reserved.

#include "Transitions/ComposableCameraInertializedTransition.h"

#include "ComposableCameraSystemModule.h"

template struct ComposableCameraInitializer<FVector, ComposableCameraPositionalInertializer>;
template struct ComposableCameraInitializer<FVector, ComposableCameraIndependentPositionalInertializer>;
template struct ComposableCameraInitializer<FRotator, ComposableCameraRotationalInertializer>;

void UComposableCameraInertializedTransition::OnBeginPlay_Implementation(float DeltaTime,
                                                                         const FComposableCameraPose& CurrentSourcePose,
                                                                         const FComposableCameraPose& CurrentTargetPose)
{
	// Use the previous and current source poses from InitParams for velocity estimation.
	// These are the Director's blended output poses from the previous and current frames.
	const FComposableCameraPose& LastSourceCameraPose = InitParams.PreviousSourcePose;
	const FComposableCameraPose& ThisSourceCameraPose = InitParams.CurrentSourcePose;
	const float InitDeltaTime = InitParams.DeltaTime;

	TransitionTime = GetActualBlendTime(InitDeltaTime, LastSourceCameraPose, ThisSourceCameraPose, CurrentTargetPose);
	RemainingTime = TransitionTime;

	PositionalInertializer = ComposableCameraInitializer<FVector, ComposableCameraIndependentPositionalInertializer>{ LastSourceCameraPose, ThisSourceCameraPose, CurrentTargetPose, TransitionTime, InitDeltaTime };
	RotationalInertializer = ComposableCameraInitializer<FRotator, ComposableCameraRotationalInertializer>{ LastSourceCameraPose, ThisSourceCameraPose, CurrentTargetPose, TransitionTime, InitDeltaTime };
}

FComposableCameraPose UComposableCameraInertializedTransition::OnEvaluate_Implementation(float DeltaTime,
                                                                                         const FComposableCameraPose& CurrentSourcePose,
                                                                                         const FComposableCameraPose& CurrentTargetPose)
{
	const float BlendDuration = TransitionTime - RemainingTime;
	const float BlendPct = BlendDuration / TransitionTime;
	Percentage = BlendPct;

	// Blend ALL pose fields (FOV, physical camera, projection, etc.) using the shared BlendBy rule.
	// Position/Rotation are then overwritten below with the inertialized values — this transition's
	// whole point is that transform blending is governed by velocity/acceleration, not linear lerp.
	FComposableCameraPose OutPose = CurrentSourcePose;
	OutPose.BlendBy(CurrentTargetPose, BlendPct);

	if (AdditiveCurve)
	{
		OutPose.Position = PositionalInertializer.Evaluate(BlendDuration, CurrentTargetPose.Position, BlendPct, AdditiveCurve, AdditiveCurveWeight, AdditiveCurveShape);
		OutPose.Rotation = RotationalInertializer.Evaluate(BlendDuration, CurrentTargetPose.Rotation, BlendPct, AdditiveCurve, AdditiveCurveWeight, AdditiveCurveShape);
	}
	else
	{
		OutPose.Position = PositionalInertializer.Evaluate(BlendDuration, CurrentTargetPose.Position);
		OutPose.Rotation = RotationalInertializer.Evaluate(BlendDuration, CurrentTargetPose.Rotation);
	}

	return OutPose;
}

float UComposableCameraInertializedTransition::GetActualBlendTime(float InDeltaTime,
                                                                  const FComposableCameraPose& LastSourceCameraPose,
                                                                  const FComposableCameraPose& ThisSourceCameraPose,
                                                                  const FComposableCameraPose& CurrentTargetPose)
{
	if (bAutoTransitionTime)
	{
		auto [Velocity, Length] = [&]() -> auto
		{
			FVector LastCameraLocation = LastSourceCameraPose.Position;
			FVector ThisCameraLocation = ThisSourceCameraPose.Position;
			FVector InitialDirection = ThisCameraLocation - CurrentTargetPose.Position;
			FVector PreviousDirection = LastCameraLocation - CurrentTargetPose.Position;
			float InitialMagnitude = InitialDirection.Length();
			InitialDirection.Normalize();
			float PreviousMagnitude = PreviousDirection.Dot(InitialDirection);
			float InitialVelocity = (InitialMagnitude - PreviousMagnitude) / InDeltaTime;
			return std::pair{ InitialVelocity, InitialMagnitude };
		}();

		float AutoBlendTime = 1.f;
		float Sqrt = 64 * Velocity + 80 * MaxAcceleration * Length;
		if (Sqrt >= 0)
		{
			AutoBlendTime = FMath::Max(AutoBlendTime, (8 * Velocity + FMath::Sqrt(Sqrt)) / (2 * MaxAcceleration));
		}
		Sqrt = 64 * Velocity - 80 * MaxAcceleration * Length;
		if (Sqrt >= 0)
		{
			AutoBlendTime = FMath::Max(AutoBlendTime, (-8 * Velocity + FMath::Sqrt(Sqrt)) / (2 * MaxAcceleration));
		}

		return AutoBlendTime;
	}
	else
	{
		return TransitionTime;
	}
}
