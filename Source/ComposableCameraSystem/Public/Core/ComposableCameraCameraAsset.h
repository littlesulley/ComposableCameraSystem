// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagAssetInterface.h"
#include "GameplayTagContainer.h"
#include "UObject/Object.h"

#include "ComposableCameraCameraAsset.generated.h"

/**
 * Base class for camera assets in the Composable Camera System.
 * Create from this class to create custom camera assets with specific properties and behaviors.
 */
UCLASS(BlueprintType, Blueprintable, ClassGroup = ComposableCameraSystem, Experimental)
class COMPOSABLECAMERASYSTEM_API UComposableCameraCameraAsset
	: public UObject
	, public IGameplayTagAssetInterface
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = GameplayTags)
	FGameplayTagContainer CameraTags;
	
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override
	{
		TagContainer.AppendTags(CameraTags);
	}
};
