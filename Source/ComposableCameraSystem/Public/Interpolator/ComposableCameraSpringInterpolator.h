// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraInterpolatorBase.h"
#include "ComposableCameraSpringInterpolator.generated.h"

/**
 * Spring interpolator. 
 */
UCLASS(BlueprintType, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraSpringInterpolator
	: public UComposableCameraInterpolatorBase
{
	GENERATED_BODY()
};
