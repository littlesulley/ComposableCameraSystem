// Copyright Sulley. All rights reserved.

#include "Transitions/ComposableCameraInertializedTransition.h"

#include "ComposableCameraSystemModule.h"
#include "Transitions/ComposableCameraSmoothTransition.h"

void UComposableCameraInertializedTransition::OnBeginPlay_Implementation(float DeltaTime,
                                                                         const FComposableCameraPose& CurrentTargetPose)
{
	if (SourceCamera)
	{
		FComposableCameraPose LastSourceCameraPose = SourceCamera->GetLastFrameCameraPose();
		FComposableCameraPose ThisSourceCameraPose = SourceCamera->GetCameraPose();

		TransitionTime = GetActualBlendTime(DeltaTime, LastSourceCameraPose, ThisSourceCameraPose, CurrentTargetPose);
		RemainingTime = TransitionTime;

		PositionalInertializer = ComposableCameraInitializer<FVector, ComposableCameraPositionalInertializer>{ LastSourceCameraPose, ThisSourceCameraPose, CurrentTargetPose, TransitionTime, DeltaTime };
		RotationalInertializer = ComposableCameraInitializer<FRotator, ComposableCameraRotationalInertializer>{ LastSourceCameraPose, ThisSourceCameraPose, CurrentTargetPose, TransitionTime, DeltaTime };
	}
	else
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT("SourceCamera is null in ComposableCameraInertializedTransition. Turn to use SmoothTransition."))

		BackupSmoothTransition = NewObject<UComposableCameraSmoothTransition>();
		BackupSmoothTransition->TransitionEnabled(SourceCamera, TargetCamera, StartCameraPose, TransitionTime);
	}
}

FComposableCameraPose UComposableCameraInertializedTransition::OnEvaluate_Implementation(float DeltaTime,
                                                                                         const FComposableCameraPose& CurrentTargetPose)
{
	if (SourceCamera)
	{
		FComposableCameraPose OutPose {};
		
		float BlendDuration = TransitionTime - RemainingTime;
		float BlendPct = BlendDuration / TransitionTime;
		
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

		OutPose.FieldOfView = StartCameraPose.FieldOfView + BlendPct * (CurrentTargetPose.FieldOfView - StartCameraPose.FieldOfView);

		return OutPose;
	}
	else
	{
		return BackupSmoothTransition->Evaluate(DeltaTime, CurrentTargetPose);
	}
}

float UComposableCameraInertializedTransition::GetActualBlendTime(float DeltaTime,
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
			float InitialVelocity = (InitialMagnitude - PreviousMagnitude) / DeltaTime;
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
		return BlendTime;
	}
}
