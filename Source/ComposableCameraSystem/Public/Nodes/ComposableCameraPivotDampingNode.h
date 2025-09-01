// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "ComposableCameraPivotDampingNode.generated.h"

/**
 * Node for damping (interpolating) the pivot position.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraPivotDampingNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()
};
