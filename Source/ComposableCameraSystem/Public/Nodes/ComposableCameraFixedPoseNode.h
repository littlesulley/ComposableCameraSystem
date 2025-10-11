// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "ComposableCameraFixedPoseNode.generated.h"

/**
 * Node for keeping a fixed pose camera, i.e., keeping its position, rotation and FOV.
 */
UCLASS(NotBlueprintable, ClassGroup =  ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraFixedPoseNode : public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	virtual void OnBeginPlayNode_Implementation(const FComposableCameraPose& CurrentCameraPose) override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;

private:
	FComposableCameraPose FixedPose {};
};
