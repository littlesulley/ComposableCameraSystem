// Copyright 2026 Sulley. All Rights Reserved.

#include "Nodes/ComposableCameraAutoRotateNode.h"

#include "ComposableCameraSystemModule.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "Kismet/KismetMathLibrary.h"
#include "Math/ComposableCameraMath.h"

void UComposableCameraAutoRotateNode::OnInitialize_Implementation()
{
	Super::OnInitialize_Implementation();

	Interpolator_T = IsValid(RotateInterpolator) ? RotateInterpolator->BuildRotatorInterpolator() : nullptr;
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
	// Resolve the reference forward direction for this frame. Pin-matched
	// UPROPERTYs (DirectionMode, MainDirection, PrimaryActor) have already been
	// written by ResolveAllInputPins in the base TickNode prologue.
	FVector ResolvedDirection = FVector::ZeroVector;
	if (DirectionMode == EComposableCameraAutoRotateDirectionMode::Direction)
	{
		ResolvedDirection = MainDirection;
	}
	else if (DirectionMode == EComposableCameraAutoRotateDirectionMode::ActorForward)
	{
		AActor* EffectivePrimaryActor = ComposableCameraSystem::ResolveActorInput(
			PrimaryActorSource, PrimaryActor.Get(), GetOwningPlayerCameraManager(), this);
		if (IsValid(EffectivePrimaryActor))
		{
			ResolvedDirection = EffectivePrimaryActor->GetActorForwardVector();
		}
	}

	if (ResolvedDirection.IsNearlyZero())
	{
		UE_LOG(LogComposableCameraSystem, Error, TEXT("Reference direction is zero in AutoRotate node (DirectionMode=%d), will not proceed."),
			static_cast<int32>(DirectionMode));
		return;
	}

	InputInterruptCooldownRemaining = FMath::Max(InputInterruptCooldownRemaining - DeltaTime, 0.f);
	BeyondValidRangeCooldownRemaining = FMath::Max(BeyondValidRangeCooldownRemaining - DeltaTime, 0.f);

	FRotator MainRotation = UKismetMathLibrary::MakeRotFromX(ResolvedDirection);

	FVector2D ValidRangeYaw { MainRotation.Yaw + YawRange[0], MainRotation.Yaw + YawRange[1] };
	FVector2D ValidRangePitch { MainRotation.Pitch + PitchRange[0], MainRotation.Pitch + PitchRange[1] };

	// Only evaluate user interrupt when bInterruptOnUserInput is enabled; if it
	// is disabled the Interrupt cooldown and counter are never armed.
	if (bInterruptOnUserInput)
	{
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
	}

	auto [ bYawInValidRange, bPitchInValidRange ] = CheckIfInValidRange(ValidRangeYaw, ValidRangePitch, OutCameraPose.Rotation);
	// Beyond-range = ANY axis out of range (both axes engage together via the
	// unified interpolator below). bYawOnly still only looks at yaw.
	auto bBeyondValidRange = bYawOnly ? !bYawInValidRange : (!bYawInValidRange || !bPitchInValidRange);

	const bool bInterruptGateOpen = !bInterruptOnUserInput
		|| (InputInterruptCooldownRemaining <= 0.f
			&& (MaxCountAfterInputInterrupt == -1 || UsedCountAfterInputInterrupt <= MaxCountAfterInputInterrupt));

	if (!bBeyondValidRange)
	{
		BeyondValidRangeCooldownRemaining = BeyondValidRangeCooldown;
	}
	else if (bInterruptGateOpen && BeyondValidRangeCooldownRemaining <= 0.f)
	{
		bInAutoRotate = true;

		float TargetYaw = bYawInValidRange
			? OutCameraPose.Rotation.Yaw
			: ComposableCameraSystem::GetClosestAngleDegree(OutCameraPose.Rotation.Yaw, ValidRangeYaw[0], ValidRangeYaw[1]);

		float TargetPitch = (bYawOnly || bPitchInValidRange)
			? OutCameraPose.Rotation.Pitch
			: ComposableCameraSystem::GetClosestAngleDegree(OutCameraPose.Rotation.Pitch, ValidRangePitch[0], ValidRangePitch[1]);

		FRotator TargetRotation { TargetPitch, TargetYaw, OutCameraPose.Rotation.Roll };

		if (Interpolator_T)
		{
			// Single rotator interpolator drives yaw + pitch as one unit, so both
			// axes finish together rather than yaw completing while pitch lags.
			Interpolator_T->Reset(OutCameraPose.Rotation, TargetRotation);
			OutCameraPose.Rotation = Interpolator_T->Run(DeltaTime);
		}
		else
		{
			// No interpolator authored. Teleport to the target boundary.
			OutCameraPose.Rotation = TargetRotation;
		}

		if ((TargetRotation - OutCameraPose.Rotation).GetNormalized().IsNearlyZero())
		{
			bInAutoRotate = false;
		}
	}
}

void UComposableCameraAutoRotateNode::GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("DirectionMode");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraAutoRotateNode", "DirectionMode", "Direction Mode");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Enum;
		PinDecl.EnumType = StaticEnum<EComposableCameraAutoRotateDirectionMode>();
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = false;
		PinDecl.DefaultValueString = PinDecl.EnumType ? PinDecl.EnumType->GetNameStringByValue(static_cast<int64>(DirectionMode)) : FString();
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraAutoRotateNode", "DirectionModeTip", "Selects whether the reference forward is taken from MainDirection or from PrimaryActor's forward vector.");
		OutPins.Add(PinDecl);
	}

	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("PrimaryActorSource");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraAutoRotateNode", "PrimaryActorSource", "Primary Actor Source");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Enum;
		PinDecl.EnumType = StaticEnum<EComposableCameraActorInputSource>();
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = false;
		PinDecl.DefaultValueString = PinDecl.EnumType ? PinDecl.EnumType->GetNameStringByValue(static_cast<int64>(PrimaryActorSource)) : FString();
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraAutoRotateNode", "PrimaryActorSourceTip", "Selects whether PrimaryActor comes from the controller's controlled pawn or an explicit actor.");
		OutPins.Add(PinDecl);
	}

	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("MainDirection");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraAutoRotateNode", "MainDirection", "Main Direction");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Vector3D;
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = false;
		PinDecl.DefaultValueString = MainDirection.ToString();
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraAutoRotateNode", "MainDirectionTip", "Reference forward direction (used when DirectionMode is Direction).");
		OutPins.Add(PinDecl);
	}

	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("PrimaryActor");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraAutoRotateNode", "PrimaryActor", "Primary Actor");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Actor;
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = false;
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraAutoRotateNode", "PrimaryActorTip", "Actor whose forward vector is used as the reference direction (when DirectionMode is ActorForward).");
		OutPins.Add(PinDecl);
	}

	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("bInterruptOnUserInput");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraAutoRotateNode", "bInterruptOnUserInput", "Interrupt On User Input");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Bool;
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = false;
		PinDecl.DefaultValueString = bInterruptOnUserInput ? TEXT("true") : TEXT("false");
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraAutoRotateNode", "bInterruptOnUserInputTip", "When true, user rotation input cancels auto-rotation and counts toward the interrupt limit. When false, CameraRotationInput is ignored.");
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
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraAutoRotateNode", "CameraRotationInputTip", "Camera rotation input for this frame (only read when bInterruptOnUserInput is true).");
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
