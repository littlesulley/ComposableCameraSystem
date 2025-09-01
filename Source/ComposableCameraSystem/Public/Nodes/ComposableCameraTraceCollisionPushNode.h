// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "ComposableCameraTraceCollisionPushNode.generated.h"

/**
 * Node for resolving collision using ray traces.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraTraceCollisionPushNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()
};
