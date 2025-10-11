// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraFixedPoseNode.h"

void UComposableCameraFixedPoseNode::OnBeginPlayNode_Implementation(const FComposableCameraPose& CurrentCameraPose)
{
	FixedPose = OwningCamera->CameraPose;
}

void UComposableCameraFixedPoseNode::OnTickNode_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	OutCameraPose = FixedPose;
}
