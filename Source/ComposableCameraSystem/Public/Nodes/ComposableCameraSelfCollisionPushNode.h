// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "ComposableCameraSelfCollisionPushNode.generated.h"

/**
 * Node for resolving collision using self spherical collision detection.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraSelfCollisionPushNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()
};
