// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraReceivePivotActorNode.h"

#include "Cameras/ComposableCameraCameraBase.h"

UComposableCameraReceivePivotActorNode::UComposableCameraReceivePivotActorNode(
	const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	RequiredContextClasses = {
		UComposableCameraPoseContextPivotOnly::StaticClass()
	};
}

void UComposableCameraReceivePivotActorNode::OnBeginPlayNode_Implementation()
{
	if (!PivotOnlyContext)
	{
		PivotOnlyContext = CastChecked<UComposableCameraPoseContextPivotOnly>(GetOwningCameraPoseContextByClass(UComposableCameraPoseContextPivotOnly::StaticClass()));
	}
}

void UComposableCameraReceivePivotActorNode::OnTickNode_Implementation(
	float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose,
	FComposableCameraPose& OutCameraPose)
{
	PivotOnlyContext->PivotActor = PivotActor.Get();
	PivotOnlyContext->PivotPosition = PivotActor.Get() ? PivotActor.Get()->GetActorLocation() : PivotOnlyContext->PivotPosition;
}
