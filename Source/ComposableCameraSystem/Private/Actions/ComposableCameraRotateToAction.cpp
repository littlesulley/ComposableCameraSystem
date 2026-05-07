// Copyright Sulley. All rights reserved.

#include "Actions/ComposableCameraRotateToAction.h"

#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"

UComposableCameraRotateToAction::UComposableCameraRotateToAction(const FObjectInitializer& ObjectInitializer)
{
	ExecutionType = EComposableCameraActionExecutionType::PreCameraTick;
	ExpirationType = static_cast<uint8>(EComposableCameraActionExpirationType::Condition);
}

UEnhancedInputLocalPlayerSubsystem* UComposableCameraRotateToAction::ResolveInputSubsystem()
{
	// Per-link null-check on PCM → OwningPC → LocalPlayer. PIE stop /
	// controller swap / streaming teardown can null any of these between
	// frames; chained `->` was a crash risk.
	if (!IsValid(PlayerCameraManager))
	{
		CachedSubsystem.Reset();
		CachedLocalPlayer.Reset();
		return nullptr;
	}
	APlayerController* PC = PlayerCameraManager->GetOwningPlayerController();
	if (!IsValid(PC))
	{
		CachedSubsystem.Reset();
		CachedLocalPlayer.Reset();
		return nullptr;
	}
	ULocalPlayer* LP = PC->GetLocalPlayer();
	if (!IsValid(LP))
	{
		CachedSubsystem.Reset();
		CachedLocalPlayer.Reset();
		return nullptr;
	}

	// Controller-swap-without-destruction: chain still resolves but LP
	// is a different live object than the one the cache belongs to.
	// Invalidate the subsystem cache whenever the LocalPlayer identity
	// drifts so we don't keep reading the previous player's input.
	if (CachedLocalPlayer.Get() != LP)
	{
		CachedSubsystem.Reset();
		CachedLocalPlayer = LP;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Cached = CachedSubsystem.Get())
	{
		return Cached;
	}

	UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LP);
	CachedSubsystem = Subsystem;
	return Subsystem;
}

bool UComposableCameraRotateToAction::CanExecute_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose)
{
	bool bHasUserInput { false };
	bool bCompleteRotate { false };

	// Always go through the chain — see ResetPitchAction for the
	// controller-swap-without-destruction rationale.
	UEnhancedInputLocalPlayerSubsystem* Subsystem = ResolveInputSubsystem();

	if (Subsystem)
	{
		if (UEnhancedPlayerInput* PlayerInput = Subsystem->GetPlayerInput())
		{
			if (RotateAction)
			{
				FInputActionValue Value = PlayerInput->GetActionValue(RotateAction);
				FVector2D LookAxisVector = Value.Get<FVector2D>();
				bHasUserInput = LookAxisVector != FVector2D::ZeroVector;
			}
		}
	}

	bCompleteRotate = CurrentCameraPose.Rotation.Equals(TargetRotation, 1e-1);
	return !bHasUserInput && !bCompleteRotate;
}

void UComposableCameraRotateToAction::OnExecute_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	if (!Interp_T && Interpolator)
	{
		Interp_T = Interpolator->BuildRotatorInterpolator();
	}
	
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
