// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "Engine/DataTable.h"

#include "ComposableCameraNodeInitializerTableRow.generated.h"

class UComposableCameraModifierBase;
class UComposableCameraCameraNodeBase;

/**
 * Data table row for node initializers. A node initializer is a collection of node parameters which are used to initialize nodes. \n
 * They are used as the default values of nodes when activating a new camera.
 */
USTRUCT(BlueprintType)
struct FComposableCameraNodeInitializerTableRow
	: public FTableRowBase
{
	GENERATED_BODY()

public:
	/** Gameplay tag for an instance of a node initializer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag NodeInitializerTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BaseStruct = "/Script/ComposableCameraSystem.ComposableCameraNodeParamaters", ExcludeBaseStruct))
	TArray<FInstancedStruct> NodeParameterInitializers;

	/** Comment for this node initializer. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString Comment;
};