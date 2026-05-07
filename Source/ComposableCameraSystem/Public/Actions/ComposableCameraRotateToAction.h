// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraActionBase.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "ComposableCameraRotateToAction.generated.h"

class UComposableCameraInterpolatorBase;

/**
 * Rotate camera to a given target rotation with some interpolator.
 * If the camera rotates to the target rotation or there is user input, the action will expire.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraRotateToAction : public UComposableCameraActionBase
{
	GENERATED_BODY()
	
public:
	UComposableCameraRotateToAction(const FObjectInitializer& ObjectInitializer);

	virtual bool CanExecute_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose) override;
	virtual void OnExecute_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	
public:
	// Target rotation the camera rotates to.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Action")
	FRotator TargetRotation;

	// Rotate action to read user input from. Used to detect whether user provides input.
	UPROPERTY(EditAnywhere, Category = "Action")
	class UInputAction* RotateAction { nullptr };

	// Interpolator for camera rotation. If not specified, will use RInterpTo.
	UPROPERTY(EditAnywhere, Instanced, Category = "Action")
	UComposableCameraInterpolatorBase* Interpolator { nullptr };

	// The speed of RInterpTo when not specifying Interpolator. 
	UPROPERTY(EditAnywhere, Category = "Action", meta = (EditCondition = "Interpolator == nullptr", EditConditionHides))
	float InterpSpeed { 1.f };
	
private:
	/** Resolve (or re-resolve) the cached subsystem. Same shape as
	 *  `UComposableCameraResetPitchAction::ResolveInputSubsystem` — see
	 *  that header for the LocalPlayer-teardown / chain-null /
	 *  controller-swap-without-destruction rationale. */
	class UEnhancedInputLocalPlayerSubsystem* ResolveInputSubsystem();

	/** Weak subsystem cache — see ResetPitchAction. */
	TWeakObjectPtr<class UEnhancedInputLocalPlayerSubsystem> CachedSubsystem;

	/** LocalPlayer identity guard — see ResetPitchAction. */
	TWeakObjectPtr<class ULocalPlayer> CachedLocalPlayer;

	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<FRotator>>> Interp_T;
};
