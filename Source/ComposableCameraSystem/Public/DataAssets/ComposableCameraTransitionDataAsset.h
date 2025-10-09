// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"

#include "ComposableCameraTransitionDataAsset.generated.h"

class UComposableCameraTransitionBase;

/**
 * Data asset for defining an instanced transition.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class UComposableCameraTransitionDataAsset
	: public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced)
	UComposableCameraTransitionBase* Transition;
};