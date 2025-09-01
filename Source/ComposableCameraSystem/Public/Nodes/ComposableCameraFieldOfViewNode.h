// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "ComposableCameraFieldOfViewNode.generated.h"

/**
 * Node for adjusting field of view.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraFieldOfViewNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()
};
