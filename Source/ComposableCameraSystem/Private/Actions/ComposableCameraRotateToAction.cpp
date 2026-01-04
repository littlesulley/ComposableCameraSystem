// Copyright Sulley. All rights reserved.

#include "Actions/ComposableCameraRotateToAction.h"

#include "EnhancedInputSubsystems.h"
#include "Core/ComposableCameraPlayerCamaraManager.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"

UComposableCameraRotateToAction::UComposableCameraRotateToAction(const FObjectInitializer& ObjectInitializer)
{
	ExecutionType = EComposableCameraActionExecutionType::PreCameraTick;
	ExpirationType = static_cast<uint8>(EComposableCameraActionExpirationType::Condition);

	Interp_T = Interpolator ? Interpolator->BuildRotatorInterpolator() : nullptr;
}

bool UComposableCameraRotateToAction::CanExecute_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose)
{
	bool bHasUserInput { false };
	bool bCompleteRotate { false };

	if (!Subsystem)
	{
		Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerCameraManager->GetOwningPlayerController()->GetLocalPlayer());
	}
	
	if (Subsystem)
	{
		UEnhancedPlayerInput* PlayerInput = Subsystem->GetPlayerInput();
		if (RotateAction && PlayerInput)
		{
			FInputActionValue Value = PlayerInput->GetActionValue(RotateAction);
			FVector2D LookAxisVector = Value.Get<FVector2D>();
			bHasUserInput = LookAxisVector != FVector2D::ZeroVector;
		}
	}
	
	bCompleteRotate = CurrentCameraPose.Rotation.Equals(TargetRotation);
	return !bHasUserInput && !bCompleteRotate;
}

void UComposableCameraRotateToAction::OnExecute_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	if (Interp_T)
	{
		Interp_T->Reset(CurrentCameraPose.Rotation, TargetRotation);
		OutCameraPose.Rotation = Interp_T->Run(DeltaTime);
	}
	else
	{
		OutCameraPose.Rotation = FMath::RInterpTo(CurrentCameraPose.Rotation, TargetRotation, DeltaTime, InterpSpeed);
	}
}
