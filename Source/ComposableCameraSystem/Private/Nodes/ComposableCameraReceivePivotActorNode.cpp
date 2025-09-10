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
	if (ContextPivotActor.Variable)
	{
		ContextPivotActor.Variable->RuntimeValue = PivotActor.Get();
	}
	else
	{
		ContextPivotActor.Value = PivotActor.Get();
	}

	if (ContextPivotPosition.Variable)
	{
		ContextPivotPosition.Variable->RuntimeValue =
			PivotActor.IsValid()
			? PivotActor.Get()->GetActorLocation()
			: FVector::ZeroVector;
	}
	else
	{
		ContextPivotPosition.Value =
			PivotActor.IsValid()
			? PivotActor.Get()->GetActorLocation()
			: FVector::ZeroVector;
	}
}
