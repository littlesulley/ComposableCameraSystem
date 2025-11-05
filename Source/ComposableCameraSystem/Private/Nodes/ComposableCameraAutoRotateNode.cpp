// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraAutoRotateNode.h"

#include "ComposableCameraSystemModule.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "Kismet/KismetMathLibrary.h"
#include "Math/ComposableCameraMath.h"

void UComposableCameraAutoRotateNode::OnBeginPlayNode_Implementation(const FComposableCameraPose& CurrentCameraPose)
{
	InterpolatorYaw_T = RotateInterpolatorForYaw ? RotateInterpolatorForYaw->BuildDoubleInterpolator() : nullptr;
	InterpolatorPitch_T = RotateInterpolatorForPitch ? RotateInterpolatorForPitch->BuildDoubleInterpolator() : nullptr;
}

void UComposableCameraAutoRotateNode::SetAutoRotateMainDirectionFunction(
	FOnReceiveAutoRotateMainDirection OnUpdateAutoRotateMainDirection)
{
	OnReceiveAutoRotateMainDirection = OnUpdateAutoRotateMainDirection;
}

std::pair<bool, bool> UComposableCameraAutoRotateNode::CheckIfInValidRange(const FVector2D& ValidRangeYaw, const FVector2D& ValidRangePitch, const FRotator& Rotation)
{
	bool bYawInRange = UKismetMathLibrary::InRange_FloatFloat(FMath::UnwindDegrees(Rotation.Yaw), ValidRangeYaw[0], ValidRangeYaw[1])
					|| UKismetMathLibrary::InRange_FloatFloat(FMath::UnwindDegrees(Rotation.Yaw) + 360.f, ValidRangeYaw[0], ValidRangeYaw[1])
					|| UKismetMathLibrary::InRange_FloatFloat(FMath::UnwindDegrees(Rotation.Yaw) - 360.f, ValidRangeYaw[0], ValidRangeYaw[1]);
	bool bPitchInRange = UKismetMathLibrary::InRange_FloatFloat(FMath::UnwindDegrees(Rotation.Pitch), ValidRangePitch[0], ValidRangePitch[1]);
	return { bYawInRange, bPitchInRange };
}


void UComposableCameraAutoRotateNode::OnTickNode_Implementation(float DeltaTime,
                                                                const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	if (!OnReceiveAutoRotateMainDirection.IsBound())
	{
		UE_LOG(LogComposableCameraSystem, Error, TEXT("Delegate OnReceiveAutoRotateMainDirection is not bound to any function, will not proceed."))
		return;
	}

	InputInterruptCooldownRemaining = FMath::Max(InputInterruptCooldownRemaining - DeltaTime, 0.f);
	BeyondValidRangeCooldownRemaining = FMath::Max(BeyondValidRangeCooldownRemaining - DeltaTime, 0.f);

	FVector MainDirection = OnReceiveAutoRotateMainDirection.Execute();
	FRotator MainRotation = UKismetMathLibrary::MakeRotFromX(MainDirection);

	FVector2D ValidRangeYaw { MainRotation.Yaw + YawRange[0], MainRotation.Yaw + YawRange[1] };
	FVector2D ValidRangePitch { MainRotation.Pitch + PitchRange[0], MainRotation.Pitch + PitchRange[1] };

	bool bHasUserInterrupt = ContextCameraRotationInput.Variable && !ContextCameraRotationInput.Variable->RuntimeValue.IsNearlyZero()
						  || !ContextCameraRotationInput.Value.IsNearlyZero();

	if (bHasUserInterrupt)
	{
		InputInterruptCooldownRemaining = InputInterruptCooldown;
		
		if (bInAutoRotate)
		{
			UsedCountAfterInputInterrupt++;
			bInAutoRotate = false;
		}
	}

	auto [ bYawInValidRange, bPitchInValidRange ] = CheckIfInValidRange(ValidRangeYaw, ValidRangePitch, OutCameraPose.Rotation);
	auto bBeyondValidRange = bYawOnly ? !bYawInValidRange : (!bYawInValidRange && !bPitchInValidRange);

	if (!bBeyondValidRange)
	{
		BeyondValidRangeCooldownRemaining = BeyondValidRangeCooldown;
	}
	else if (InputInterruptCooldownRemaining <= 0.f
		&& BeyondValidRangeCooldownRemaining <= 0.f
		&& (MaxCountAfterInputInterrupt == -1 || UsedCountAfterInputInterrupt <= MaxCountAfterInputInterrupt))
	{
		bInAutoRotate = true;
		
		float TargetYaw = bYawInValidRange
			? OutCameraPose.Rotation.Yaw
			: ComposableCameraSystem::GetClosestAngleDegree(OutCameraPose.Rotation.Yaw, ValidRangeYaw[0], ValidRangeYaw[1]);
		
		float TargetPitch = bPitchInValidRange
			? OutCameraPose.Rotation.Pitch
			: ComposableCameraSystem::GetClosestAngleDegree(OutCameraPose.Rotation.Pitch, ValidRangePitch[0], ValidRangePitch[1]);

		if (bYawOnly)
		{
			TargetPitch = OutCameraPose.Rotation.Pitch;
		}

		FRotator TargetRotation = FRotator { TargetPitch, TargetYaw, OutCameraPose.Rotation.Roll };
		FRotator DeltaRotation = (TargetRotation - OutCameraPose.Rotation).GetNormalized();
		
		if (InterpolatorYaw_T)
		{
			InterpolatorYaw_T->Reset(0,  DeltaRotation.Yaw);
			 DeltaRotation.Yaw = InterpolatorYaw_T->Run(DeltaTime);
		}

		if (InterpolatorPitch_T)
		{
			InterpolatorPitch_T->Reset(0, DeltaRotation.Pitch);
			DeltaRotation.Pitch = InterpolatorPitch_T->Run(DeltaTime);
		}

		FRotator DesiredRotation = (OutCameraPose.Rotation + DeltaRotation).GetDenormalized();
		OutCameraPose.Rotation = DesiredRotation;

		if (DeltaRotation.GetNormalized().IsNearlyZero())
		{
			bInAutoRotate = false;
		}
	}
}

void UComposableCameraAutoRotateNode::ReceiveInitializerNode(UComposableCameraCameraNodeBase* Initializer)
{
	if (UComposableCameraAutoRotateNode* CastedInitializer = Cast<UComposableCameraAutoRotateNode>(Initializer))
	{
		YawRange = CastedInitializer->YawRange;
		PitchRange = CastedInitializer->PitchRange;
		bYawOnly = CastedInitializer->bYawOnly;
		BeyondValidRangeCooldown = CastedInitializer->BeyondValidRangeCooldown;
		InputInterruptCooldown = CastedInitializer->InputInterruptCooldown;
		MaxCountAfterInputInterrupt = CastedInitializer->MaxCountAfterInputInterrupt;
		RotateInterpolatorForYaw = CastedInitializer->RotateInterpolatorForYaw;
		RotateInterpolatorForPitch = CastedInitializer->RotateInterpolatorForPitch;
	}
}

