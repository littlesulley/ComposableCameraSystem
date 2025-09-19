// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraCollisionPushNode.h"

void UComposableCameraCollisionPushNode::OnTickNode_Implementation(float DeltaTime,
                                                                   const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	if (UWorld* World = GetWorld())
	{
		//World->LineTraceSingleByChannel()
	}
	
}
