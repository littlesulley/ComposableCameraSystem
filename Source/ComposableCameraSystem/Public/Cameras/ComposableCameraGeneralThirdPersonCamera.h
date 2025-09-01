// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "ComposableCameraGeneralThirdPersonCamera.generated.h"

/**
 * Generally-purposed third person camera.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraGeneralThirdPersonCamera
	: public UComposableCameraCameraBase
{
	GENERATED_BODY()

public:
	UComposableCameraGeneralThirdPersonCamera(const FObjectInitializer& ObjectInitializer);

protected:
	virtual FComposableCameraPose OnTickCamera_Implementation(float DeltaTime) override;
	virtual void OnBeginPlayCamera_Implementation() override;

private:
	TObjectPtr<AActor> PivotActor { nullptr };
};
