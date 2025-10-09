// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"

#include "ComposableCameraNodeInitializerDataAsset.generated.h"

class UComposableCameraModifierBase;
class UComposableCameraCameraNodeBase;

/**
 * Data asset for node initializers. A node initializer is a collection of node parameters which are used to initialize nodes. \n
 * They are used as the default values of nodes when activating a new camera.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class UComposableCameraNodeInitializerDataAsset
	: public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced)
	TArray<UComposableCameraCameraNodeBase*> NodeParameterInitializers;

	/** Comment for this node initializer. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString Comment;
};

