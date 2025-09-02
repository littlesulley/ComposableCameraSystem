// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "ComposableCameraGeneralThirdPersonCamera.generated.h"

/**
 * Generally-purposed third person camera.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API AComposableCameraGeneralThirdPersonCamera
	: public AComposableCameraCameraBase
{
	GENERATED_BODY()

public:
	AComposableCameraGeneralThirdPersonCamera(const FObjectInitializer& ObjectInitializer);

};
