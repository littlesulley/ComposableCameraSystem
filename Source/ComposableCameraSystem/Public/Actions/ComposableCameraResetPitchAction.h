// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraActionBase.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "ComposableCameraResetPitchAction.generated.h"

class UComposableCameraInterpolatorBase;

/**
 * This action smoothly resets pitch to a target value.
 * If the camera rotates to the target rotation or there is user input, the action will expire.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraResetPitchAction : public UComposableCameraActionBase
{
	GENERATED_BODY()

public:
	UComposableCameraResetPitchAction(const FObjectInitializer& ObjectInitializer);
	
	virtual bool CanExecute_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose) override;
	virtual void OnExecute_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;

public:
	// Target pitch.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Action")
	float Pitch { 0.f };

	// Rotate action to read user input from. Used to detect 
	UPROPERTY(EditAnywhere, Category = "Action")
	class UInputAction* RotateAction { nullptr };

	// Interpolator for pitch rotation. If not specified, will use InterpTo.
	UPROPERTY(EditAnywhere, Instanced, Category = "Action")
	UComposableCameraInterpolatorBase* Interpolator { nullptr };

	// The speed of InterpTo when not specifying Interpolator. 
	UPROPERTY(EditAnywhere, Category = "Action", meta = (EditCondition = "Interpolator == nullptr", EditConditionHides))
	float InterpSpeed { 1.f };

private:
	/** Walk PCM → OwningPC → LocalPlayer with per-link `IsValid`, then
	 *  reuse the cached subsystem only if its owning LocalPlayer matches
	 *  the chain's current LocalPlayer. Returns nullptr on any chain
	 *  null OR mismatch (after invalidating the stale cache).
	 *
	 *  Caching just the subsystem isn't enough: `OwningPlayerController`
	 *  can be re-pointed (re-possess, AI takeover, splitscreen reshuffle)
	 *  to a DIFFERENT `LocalPlayer` whose subsystem is a different live
	 *  object. The previous LocalPlayer can stay alive (its subsystem
	 *  still valid in isolation) — so a `IsValid(CachedSubsystem)` test
	 *  alone passes against a stale-player cache and we'd keep reading
	 *  the previous player's input source. The LocalPlayer comparison
	 *  fixes that. */
	class UEnhancedInputLocalPlayerSubsystem* ResolveInputSubsystem();

	/** Weak — `UEnhancedInputLocalPlayerSubsystem` is owned by the
	 *  LocalPlayer, which is destroyed on PIE stop, controller swap,
	 *  level-streaming-out, or kick. */
	TWeakObjectPtr<class UEnhancedInputLocalPlayerSubsystem> CachedSubsystem;

	/** Identity of the LocalPlayer the cached subsystem belongs to. Used
	 *  by `ResolveInputSubsystem` to detect controller-swap-without-
	 *  destruction and invalidate the subsystem cache before reading from
	 *  the wrong player's input source. */
	TWeakObjectPtr<class ULocalPlayer> CachedLocalPlayer;

	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<double>>> Interp_T;
};
