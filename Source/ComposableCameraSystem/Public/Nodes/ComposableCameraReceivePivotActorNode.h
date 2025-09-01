// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "ComposableCameraReceivePivotActorNode.generated.h"

/**
 * Node for receiving a pivot target actor. Only used when the owning camera is initialized.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraReceivePivotActorNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Nodes")
	AActor* Invoke(AActor* InActor);
};
