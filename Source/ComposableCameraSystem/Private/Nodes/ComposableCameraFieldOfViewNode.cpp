// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraFieldOfViewNode.h"

void UComposableCameraFieldOfViewNode::OnTickNode_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	OutCameraPose.FieldOfView = FieldOfView;
}
