// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "ComposableCameraCameraOffsetNode.generated.h"

/**
 * Node for applying camera offset to the pivot in camera space. The result in written into OutCameraPose each tick. \n
 * @InputParameter CameraOffset: Offset to apply on the context pivot position in camera space. \n
 * @ContextParameter ContextPivotPosition: The pivot to read position to read.
 */
UCLASS(NotBlueprintable, ClassGroup =  ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraCameraOffsetNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;

public:
	// Offset to apply on the context pivot position in camera space.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	FVector CameraOffset;

	// The pivot to read position to read.
	UPROPERTY(EditAnywhere, Category = ContextParameters)
	FVector3dComposableCameraContextParameter ContextPivotPosition;
};
