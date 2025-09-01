// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "ComposableCameraControlRotateNode.generated.h"

/**
 * Node for receiving user input and applying it to camera rotation.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraControlRotateNode : public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()
};
