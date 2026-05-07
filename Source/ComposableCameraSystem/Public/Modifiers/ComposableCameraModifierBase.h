// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Templates/SubclassOf.h"
#include "UObject/Object.h"
#include "ComposableCameraModifierBase.generated.h"

class UComposableCameraCameraNodeBase;

/**
 * An abstract modifier that provides interfaces for customizing node properties.
 * Modifiers can only be applied to non-transient cameras.
 */
UCLASS(Abstract, Blueprintable, BlueprintType, DefaultToInstanced, EditInlineNew, ClassGroup = ComposableCameraSystem, CollapseCategories)
class COMPOSABLECAMERASYSTEM_API UComposableCameraModifierBase : public UObject
{
	GENERATED_BODY()

public:
	// The node class this modifier is going to modify. \n
	// NOTE: DO NOT edit this property for instanced modifiers. Only change it in the modifier's Class Default Object.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modifier", meta = (NoEditInline))
	TSubclassOf<UComposableCameraCameraNodeBase> NodeClass;

public:
	// This is the function implementing and executing your modifier logic.
	// Typically, you should define your own modifier-specific function that truly sets node parameters or other stuff.
	UFUNCTION(BlueprintImplementableEvent, Category = "ComposableCameraSystem|Modifiers")
	void ApplyModifier(UComposableCameraCameraNodeBase* Node);
};
