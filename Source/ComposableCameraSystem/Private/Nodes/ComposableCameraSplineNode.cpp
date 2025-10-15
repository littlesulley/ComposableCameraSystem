// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraSplineNode.h"

#include "ComposableCameraSystemModule.h"

void UComposableCameraSplineNode::OnBeginPlayNode_Implementation(const FComposableCameraPose& CurrentCameraPose)
{
	
}

void UComposableCameraSplineNode::OnTickNode_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	bool bShouldProceed = true;
	
	switch (SplineType)
	{
	case EComposableCameraSplineNodeSplineType::BuiltInSpline:
		{
			if (!Rail)
			{
				UE_LOG(LogComposableCameraSystem, Error, TEXT("BuiltInSpline is used but Rail is nullptr, will not proceed."))
				bShouldProceed = false;
			}
		}
	// @TODO: implement all splines
	case EComposableCameraSplineNodeSplineType::BasicSpline:
		break;
	case EComposableCameraSplineNodeSplineType::Bezier:
		break;
	case EComposableCameraSplineNodeSplineType::CubicHermite:
		break;
	case EComposableCameraSplineNodeSplineType::NURBS:
		break;
	}
}

void UComposableCameraSplineNode::ReceiveInitializerNode(UComposableCameraCameraNodeBase* Initializer)
{
	
}
