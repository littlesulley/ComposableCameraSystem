// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Engine/DataTable.h"

#include "ComposableCameraNodeInitializerTableRow.generated.h"

class UComposableCameraCameraNodeBase;

/**
 * Data table row for node initializers. A node initializer is an instanced node with user-defined default values. \n
 * They are used as initializers to set the default values of nodes when activating a new camera.
 */
USTRUCT(BlueprintType)
struct FComposableCameraNodeInitializerTableRow
	: public FTableRowBase
{
	GENERATED_USTRUCT_BODY()

public:
	/** Gameplay tag for an instance of a node initializer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag NodeInitializerTag;

	/** Concrete node initializers. You can have multiple initializers for one tag. \n
	 * When an initializer is applied to a camera, the camera must have a compatible node type. \n
	 * If not, the initializer will not be applied.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced)
	TArray<UComposableCameraCameraNodeBase*> NodeInitializer;
};

/**
 * Data table row for node modifiers. A node modifier can modify any parameters of any node type at runtime. \n
 */
USTRUCT(BlueprintType)
struct FComposableCameraNodeModifierTableRow
	: public FTableRowBase
{
	GENERATED_USTRUCT_BODY()

public:
	
};