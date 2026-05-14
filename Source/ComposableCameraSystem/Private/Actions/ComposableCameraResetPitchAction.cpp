// Copyright Sulley. All rights reserved.


#include "Actions/ComposableCameraResetPitchAction.h"

#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"

UComposableCameraResetPitchAction::UComposableCameraResetPitchAction(const FObjectInitializer& ObjectInitializer)
{
	ExecutionType = EComposableCameraActionExecutionType::PreCameraTick;
	ExpirationType = static_cast<uint8>(EComposableCameraActionExpirationType::Condition);
}

UEnhancedInputLocalPlayerSubsystem* UComposableCameraResetPitchAction::ResolveInputSubsystem()
{
	// Per-link null-check on PCM -> OwningPC -> LocalPlayer. Any of these
	// can be null after PIE stop / controller swap / kick / streaming
	// teardown. Reading through a single chained `->` is a crash risk.
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

	// Controller-swap-without-destruction: the chain still resolves but
	// LP is a DIFFERENT object than the one the subsystem cache belongs
	// to. The previous LocalPlayer + its subsystem can both be alive
	// (so `CachedSubsystem.IsValid()` would return true) but we'd be
	// reading the wrong player's input. Invalidate the subsystem cache
	// whenever the LocalPlayer identity drifts, then re-resolve.
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

bool UComposableCameraResetPitchAction::CanExecute_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose)
{
	bool bHasUserInput { false };
	bool bCompleteRotate { false };

	// Always re-resolve through the chain so a controller / LocalPlayer
	// swap that the cache wouldn't notice is caught here. The chain
	// walk is three null-checks + one map lookup (GetSubsystem) on the
	// miss path. Negligible vs the cost of reading a stale player's
	// input every tick. ResolveInputSubsystem returns the cached value
	// when LP is unchanged, so steady-state cost stays at the chain.
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
