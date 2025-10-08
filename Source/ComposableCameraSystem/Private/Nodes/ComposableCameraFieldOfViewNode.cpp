// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraFieldOfViewNode.h"

#include "Field/FieldSystemNoiseAlgo.h"

void UComposableCameraFieldOfViewNode::OnTickNode_Implementation(float DeltaTime,
                                                                 const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	OutCameraPose.FieldOfView = FieldOfView;
}

void UComposableCameraFieldOfViewNode::ReceiveInitializerNode(UComposableCameraCameraNodeBase* Initializer)
{
	if (UComposableCameraFieldOfViewNode* CastedInitializer = Cast<UComposableCameraFieldOfViewNode>(Initializer))
	{
		FieldOfView = CastedInitializer->FieldOfView;
	}
}
