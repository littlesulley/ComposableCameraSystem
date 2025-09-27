// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "ComposableCameraFieldOfViewNode.generated.h"

/**
 * Node for adjusting field of view. This FOV is directly set to the CameraPose each frame.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraFieldOfViewNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	float FieldOfView { 79.f };
};
