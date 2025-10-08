// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraAutoRotateNode.h"

void UComposableCameraAutoRotateNode::OnBeginPlayNode_Implementation(const FComposableCameraPose& CurrentCameraPose)
{

}

void UComposableCameraAutoRotateNode::OnTickNode_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{

}

void UComposableCameraAutoRotateNode::ReceiveInitializerNode(UComposableCameraCameraNodeBase* Initializer)
{
	if (UComposableCameraAutoRotateNode* CastedInitializer = Cast<UComposableCameraAutoRotateNode>(Initializer))
	{
		
	}
}
