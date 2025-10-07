// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/Object.h"
#include "ComposableCameraModifierBase.generated.h"

class UComposableCameraCameraNodeBase;

UCLASS(Abstract, Blueprintable, BlueprintType, DefaultToInstanced, EditInlineNew, ClassGroup = ComposableCameraSystem, CollapseCategories)
class COMPOSABLECAMERASYSTEM_API UComposableCameraModifierBase : public UObject
{
	GENERATED_BODY()

public:
	// The node class this modifier is going to modify. \n
	// NOTE: DO NOT edit this property for instanced modifiers. Only change it in the modifier's Class Default Object.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (NoEditInline))
	TSubclassOf<UComposableCameraCameraNodeBase> NodeClass;

	// Tags for cameras on which this modifier is applied.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FGameplayTagContainer CameraTags;
};

/**
 * Data asset for node modifiers. A node modifier can modify any parameters of any node type at runtime. \n
 * All modifiers are defined using blueprints by the users.
 */
UCLASS(BlueprintType)
class UComposableCameraNodeModifierDataAsset
	: public UDataAsset
{
	GENERATED_BODY()

public:
	/** Gameplay tag for a sequence of node modifiers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag NodeModifierTag;

	/* Modifiers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced)
	TArray<UComposableCameraModifierBase*> NodeModifiers;
};