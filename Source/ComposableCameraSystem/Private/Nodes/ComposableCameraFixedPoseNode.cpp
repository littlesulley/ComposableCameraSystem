// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraFixedPoseNode.h"

void UComposableCameraFixedPoseNode::OnTickNode_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	OutCameraPose = OwningCamera->CameraPose;
}
