// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "ComposableCameraReceivePivotActorNode.generated.h"

/**
 * Node for reading a context pivot target actor and getting its location as the pivot location. \n
 * @ ContextParameter ContextPivotActor: The actor you'd want to maintain during the node's life cycle. The input PivotActor will be read into here, without validity check. \n 
 * @ ContextParameter ContextPivotPosition: The location of the PivotActor. \n
 * This node runs every tick.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraReceivePivotActorNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()
	
public:
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;

public:
	// The actor you'd want to maintain during the node's life cycle. The input PivotActor will be read into here, without validity check.
	UPROPERTY(EditAnywhere, Category = ContextParameters)
	FActorComposableCameraContextParameter ContextPivotActor;

	// The location of the PivotActor.
	UPROPERTY(EditAnywhere, Category = ContextParameters)
	FVector3dComposableCameraContextParameter ContextPivotPosition;
};
