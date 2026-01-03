// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraActionBase.h"
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
	UComposableCameraRotateToAction(FObjectInitializer& ObjectInitializer);
	
	virtual bool CanExecute_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose) override;
	virtual void OnExecute_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	
public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FRotator TargetRotation;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced)
	UComposableCameraInterpolatorBase* Interpolator;
};
