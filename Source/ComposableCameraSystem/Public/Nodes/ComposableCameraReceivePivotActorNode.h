// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "ComposableCameraReceivePivotActorNode.generated.h"

/**
 * Node for receiving a pivot target actor and getting its location as the pivot location. \n
 * @ InputParameter PivotActor: The actor you'd want to put into context parameter ContextPivotActor and get pivot location from. \n
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
	UComposableCameraReceivePivotActorNode(const  FObjectInitializer& ObjectInitializer);

	virtual void OnBeginPlayNode_Implementation() override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	TSoftObjectPtr<AActor> PivotActor = nullptr;

public:
	UPROPERTY(EditDefaultsOnly, Category = ContextParameters)
	FActorComposableCameraContextParameter ContextPivotActor;
	
	UPROPERTY(EditDefaultsOnly, Category = ContextParameters)
	FVector3dComposableCameraContextParameter ContextPivotPosition;
};
