// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraReceivePivotActorNode.h"

#include "Cameras/ComposableCameraCameraBase.h"

UComposableCameraReceivePivotActorNode::UComposableCameraReceivePivotActorNode(
	const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
}

void UComposableCameraReceivePivotActorNode::OnTickNode_Implementation(
	float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose,
	FComposableCameraPose& OutCameraPose)
{
	if (ContextPivotActor.Variable && ContextPivotActor.Variable->RuntimeValue)
	{
		ContextPivotPosition.Variable->RuntimeValue = ContextPivotActor.Variable->RuntimeValue->GetActorLocation();
	}
	else if (ContextPivotActor.Value)
	{
		ContextPivotPosition.Variable->RuntimeValue = ContextPivotActor.Value->GetActorLocation();
	}
	else
	{
		ContextPivotPosition.Variable->RuntimeValue = FVector::ZeroVector;
	}
}
