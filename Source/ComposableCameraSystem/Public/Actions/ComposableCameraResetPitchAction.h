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
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float Pitch { 0.f };

	// Rotate action to read user input from. Used to detect 
	UPROPERTY(EditAnywhere)
	class UInputAction* RotateAction { nullptr };

	// Interpolator for pitch rotation. If not specified, will use InterpTo.
	UPROPERTY(EditAnywhere, Instanced)
	UComposableCameraInterpolatorBase* Interpolator { nullptr };

	// The speed of InterpTo when not specifying Interpolator. 
	UPROPERTY(EditAnywhere, meta = (EditCondition = "Interpolator == nullptr", EditConditionHides))
	float InterpSpeed { 1.f };

private:
	class UEnhancedInputLocalPlayerSubsystem* Subsystem { nullptr };
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<double>>> Interp_T;
};
