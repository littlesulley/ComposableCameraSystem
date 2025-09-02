// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "ComposableCameraReceivePivotActorNode.generated.h"

/**
 * Node for receiving a pivot target actor. This node updates the camera pose context's PivotActor and PivotPosition.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, CollapseCategories)
class COMPOSABLECAMERASYSTEM_API UComposableCameraReceivePivotActorNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()
	
public:
	UComposableCameraReceivePivotActorNode(const  FObjectInitializer& ObjectInitializer);

	virtual void OnBeginPlayNode_Implementation() override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	TSoftObjectPtr<AActor> PivotActor = nullptr;

private:
	UComposableCameraPoseContextPivotOnly* PivotOnlyContext;
};
