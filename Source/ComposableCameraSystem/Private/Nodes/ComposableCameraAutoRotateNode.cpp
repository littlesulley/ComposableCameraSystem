// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraAutoRotateNode.h"

#include "ComposableCameraSystemModule.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "Kismet/KismetMathLibrary.h"
#include "Math/ComposableCameraMath.h"

void UComposableCameraAutoRotateNode::OnInitialize_Implementation()
{
	Super::OnInitialize_Implementation();

	InterpolatorYaw_T = IsValid(RotateInterpolatorForYaw) ? RotateInterpolatorForYaw->BuildDoubleInterpolator() : nullptr;
	InterpolatorPitch_T = IsValid(RotateInterpolatorForPitch) ? RotateInterpolatorForPitch->BuildDoubleInterpolator() : nullptr;
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
	// MainDirection is a pin-matched UPROPERTY — the base TickNode prologue
	// calls ResolveAllInputPins() before OnTickNode_Implementation runs, so the
	// member already reflects the wired / exposed / default value for this frame.
	if (MainDirection.IsNearlyZero())
	{
		UE_LOG(LogComposableCameraSystem, Error, TEXT("MainDirection is zero in AutoRotate node, will not proceed."))
		return;
	}

	InputInterruptCooldownRemaining = FMath::Max(InputInterruptCooldownRemaining - DeltaTime, 0.f);
	BeyondValidRangeCooldownRemaining = FMath::Max(BeyondValidRangeCooldownRemaining - DeltaTime, 0.f);

	FRotator MainRotation = UKismetMathLibrary::MakeRotFromX(MainDirection);

	FVector2D ValidRangeYaw { MainRotation.Yaw + YawRange[0], MainRotation.Yaw + YawRange[1] };
	FVector2D ValidRangePitch { MainRotation.Pitch + PitchRange[0], MainRotation.Pitch + PitchRange[1] };

	// CameraRotationInput is a pin-matched UPROPERTY — the base TickNode prologue
	// calls ResolveAllInputPins() before OnTickNode_Implementation runs, so the
	// member already reflects the wired / exposed / default value for this frame.
	const bool bHasUserInterrupt = !CameraRotationInput.IsNearlyZero();

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

void UComposableCameraAutoRotateNode::GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("MainDirection");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraAutoRotateNode", "MainDirection", "Main Direction");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Vector3D;
		PinDecl.bRequired = false;
		PinDecl.DefaultValueString = MainDirection.ToString();
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraAutoRotateNode", "MainDirectionTip", "Reference forward direction the camera auto-rotates toward.");
		OutPins.Add(PinDecl);
	}

	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("CameraRotationInput");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraAutoRotateNode", "CameraRotationInput", "Camera Rotation Input");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Vector2D;
		PinDecl.bRequired = false;
		PinDecl.DefaultValueString = CameraRotationInput.ToString();
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraAutoRotateNode", "CameraRotationInputTip", "Camera rotation input for this frame.");
		OutPins.Add(PinDecl);
	}

	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("YawRange");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraAutoRotateNode", "YawRange", "Yaw Range");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Vector2D;
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = false;
		PinDecl.DefaultValueString = YawRange.ToString();
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraAutoRotateNode", "YawRangeTip", "Yaw range around the main direction; beyond this range auto-rotation engages.");
		OutPins.Add(PinDecl);
	}

	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("PitchRange");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraAutoRotateNode", "PitchRange", "Pitch Range");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Vector2D;
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = false;
		PinDecl.DefaultValueString = PitchRange.ToString();
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraAutoRotateNode", "PitchRangeTip", "Pitch range around the main direction; beyond this range auto-rotation engages (when bYawOnly is false).");
		OutPins.Add(PinDecl);
	}

	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("bYawOnly");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraAutoRotateNode", "bYawOnly", "Yaw Only");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Bool;
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = false;
		PinDecl.DefaultValueString = bYawOnly ? TEXT("true") : TEXT("false");
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraAutoRotateNode", "bYawOnlyTip", "When true, only yaw is auto-rotated; pitch is left unchanged.");
		OutPins.Add(PinDecl);
	}

	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("BeyondValidRangeCooldown");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraAutoRotateNode", "BeyondValidRangeCooldown", "Beyond Valid Range Cooldown");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Float;
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = false;
		PinDecl.DefaultValueString = FString::SanitizeFloat(BeyondValidRangeCooldown);
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraAutoRotateNode", "BeyondValidRangeCooldownTip", "Minimum time the camera must remain beyond the valid range before auto-rotation engages.");
		OutPins.Add(PinDecl);
	}

	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("InputInterruptCooldown");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraAutoRotateNode", "InputInterruptCooldown", "Input Interrupt Cooldown");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Float;
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = false;
		PinDecl.DefaultValueString = FString::SanitizeFloat(InputInterruptCooldown);
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraAutoRotateNode", "InputInterruptCooldownTip", "After a user input interrupt, the minimum time to wait before auto-rotation re-engages.");
		OutPins.Add(PinDecl);
	}

	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("MaxCountAfterInputInterrupt");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraAutoRotateNode", "MaxCountAfterInputInterrupt", "Max Count After Input Interrupt");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Int32;
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = false;
		PinDecl.DefaultValueString = FString::FromInt(MaxCountAfterInputInterrupt);
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraAutoRotateNode", "MaxCountAfterInputInterruptTip", "Maximum number of auto-rotations permitted after a user input interrupt (-1 for unlimited).");
		OutPins.Add(PinDecl);
	}
}


