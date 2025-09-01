// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "ComposableCameraApplyPivotOffsetNode.generated.h"

/**
 * Node for applying camera offset to the pivot in camera space.
 */
UCLASS(NotBlueprintable, ClassGroup =  ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraApplyPivotOffsetNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()
};
