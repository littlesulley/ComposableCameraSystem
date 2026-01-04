// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraActionBase.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "ComposableCameraRotateToAction.generated.h"

class UComposableCameraInterpolatorBase;

/**
 * Rotate camera to a given target rotation with some interpolator.
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
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FRotator TargetRotation;

	// Rotate action to read user input from.
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	class UInputAction* RotateAction { nullptr };

	// Interpolator for camera rotation. If not specified, will use RInterpTo.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced)
	UComposableCameraInterpolatorBase* Interpolator { nullptr };

	// The speed of RInterpTo when not specifying Interpolator. 
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float InterpSpeed { 1.f };
	
private:
	class UEnhancedInputLocalPlayerSubsystem* Subsystem { nullptr };
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<FRotator>>> Interp_T;
};
