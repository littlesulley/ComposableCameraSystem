// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "ComposableCameraFixedPoseNode.generated.h"

/**
 * Node for keeping a fixed pose camera, i.e., keeping its position, rotation and FOV. \n
 * This node simply uses the current camera's CameraPose as the output pose. So it's not rigorously "fixed".
 */
UCLASS(NotBlueprintable, ClassGroup =  ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraFixedPoseNode : public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
};
