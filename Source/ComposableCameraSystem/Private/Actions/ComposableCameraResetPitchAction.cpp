// Copyright Sulley. All rights reserved.


#include "Actions/ComposableCameraResetPitchAction.h"

#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "Kismet/KismetSystemLibrary.h"

UComposableCameraResetPitchAction::UComposableCameraResetPitchAction(const FObjectInitializer& ObjectInitializer)
{
	ExecutionType = EComposableCameraActionExecutionType::PreCameraTick;
	ExpirationType = static_cast<uint8>(EComposableCameraActionExpirationType::Condition);
}

bool UComposableCameraResetPitchAction::CanExecute_Implementation(float DeltaTime,
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
	
	bCompleteRotate = FMath::IsNearlyEqual(CurrentCameraPose.Rotation.Pitch, Pitch, 1e-2);
	return !bHasUserInput && !bCompleteRotate;
}

void UComposableCameraResetPitchAction::OnExecute_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	if (!Interp_T && Interpolator)
	{
		Interp_T = Interpolator->BuildDoubleInterpolator();
	}
	
	if (Interp_T)
	{
		Interp_T->Reset(CurrentCameraPose.Rotation.Pitch, Pitch);
		OutCameraPose.Rotation.Pitch = Interp_T->Run(DeltaTime);
	}
	else
	{
		OutCameraPose.Rotation.Pitch = FMath::FInterpTo(CurrentCameraPose.Rotation.Pitch, Pitch, DeltaTime, InterpSpeed);
	}
}
