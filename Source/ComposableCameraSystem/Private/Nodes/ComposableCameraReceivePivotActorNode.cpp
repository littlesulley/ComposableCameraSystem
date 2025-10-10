// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraReceivePivotActorNode.h"

#include "Cameras/ComposableCameraCameraBase.h"

void UComposableCameraReceivePivotActorNode::OnBeginPlayNode_Implementation(
	const FComposableCameraPose& CurrentCameraPose)
{
	if (bUseBoneForPivot)
	{
		if (ContextPivotActor.Variable && ContextPivotActor.Variable->RuntimeValue)
		{
			SkeletalMeshComponentForPivotActor = ContextPivotActor.Variable->RuntimeValue->GetComponentByClass<USkeletalMeshComponent>();
		}
		else if (ContextPivotActor.Value)
		{
			SkeletalMeshComponentForPivotActor = ContextPivotActor.Value->GetComponentByClass<USkeletalMeshComponent>();
		}
	}
}

void UComposableCameraReceivePivotActorNode::OnTickNode_Implementation(
	float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose,
	FComposableCameraPose& OutCameraPose)
{
	if (bUseBoneForPivot && SkeletalMeshComponentForPivotActor)
	{
		ContextPivotPosition.Variable->RuntimeValue = SkeletalMeshComponentForPivotActor->GetSocketLocation(BoneName);
	}
	else if (ContextPivotActor.Variable && ContextPivotActor.Variable->RuntimeValue)
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
