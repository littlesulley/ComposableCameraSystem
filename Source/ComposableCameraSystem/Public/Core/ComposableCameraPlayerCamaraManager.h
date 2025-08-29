// Copyright Sulley. All rights reserved.

#pragma once

#include "Camera/PlayerCameraManager.h"
#include "ComposableCameraPlayerCamaraManager.generated.h"

class UComposableCameraDirector;
	
UCLASS(ClassGroup = ComposableCameraSystem, NotPlaceable)
class COMPOSABLECAMERASYSTEM_API AComposableCameraPlayerCamaraManager
	: public APlayerCameraManager
{
	GENERATED_BODY()

public:
	AComposableCameraPlayerCamaraManager(const FObjectInitializer& ObjectInitializer);
	virtual void InitializeFor(APlayerController* PlayerController) override;
	virtual void SetViewTarget(AActor* NewViewTarget, FViewTargetTransitionParams TransitionParams = FViewTargetTransitionParams()) override;
	virtual void ProcessViewRotation(float DeltaTime, FRotator& OutViewRotation, FRotator& OutDeltaRot) override;
	
protected:
	virtual void DoUpdateCamera(float DeltaTime) override;

private:
	TObjectPtr<UComposableCameraDirector> Director;
};
	