// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "ComposableCameraTransitionDataAsset.generated.h"

class UComposableCameraTransitionBase;

/**
 * Data asset for defining an instanced transition.
 */
UCLASS(BlueprintType, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraTransitionDataAsset
	: public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "Transition")
	UComposableCameraTransitionBase* Transition;
};