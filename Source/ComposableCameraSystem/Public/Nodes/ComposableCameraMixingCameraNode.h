// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "ComposableCameraMixingCameraNode.generated.h"

/**
 * Node for mixing multiple cameras. 
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraMixingCameraNode :
	public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()
};
