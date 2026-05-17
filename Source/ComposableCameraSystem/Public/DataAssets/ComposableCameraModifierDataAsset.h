// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Engine/DataAsset.h"
#include "ComposableCameraModifierDataAsset.generated.h"

class UComposableCameraTransitionBase;
class UComposableCameraModifierBase;

/**
 * Data asset for node modifiers. A node modifier can modify any parameters of any node type at runtime. \n
 * All modifiers are defined using blueprints by the users.
 * Modifiers can only be applied to non-transient cameras.
 */
UCLASS(BlueprintType, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraNodeModifierDataAsset
	: public UDataAsset
{
	GENERATED_BODY()

public:
	// Modifiers. 
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "Modifier")
	TArray<UComposableCameraModifierBase*> Modifiers;

	// Transition when this group of modifiers is applied to current running camera. If this is not set, the camera's default transition will be used.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "Modifier")
	UComposableCameraTransitionBase* OverrideEnterTransition;

	// Transition when this group of modifiers is removed from current running camera. If this is not set, the camera's default transition will be used.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "Modifier")
	UComposableCameraTransitionBase* OverrideExitTransition;
	
	// Tags for cameras on which this modifier group is applied.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Modifier")
	FGameplayTagContainer CameraTags;

	// Priority for this group of modifiers. If there are other modifiers of the same type, the ones with higher priority will be chosen.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Modifier", meta = (ClampMin = "0"))
	int32 Priority { 0 };
};