// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraRelativeFixedPoseNode.h"

#include "Kismet/KismetMathLibrary.h"

void UComposableCameraRelativeFixedPoseNode::OnBeginPlayNode_Implementation(
	const FComposableCameraPose& CurrentCameraPose)
{
	if (Method == EComposableCameraRelativeFixedPoseMethod::RelativeToActor)
	{
		if (!RelativeActor)
		{
			return;
		}
		
		if (USkeletalMeshComponent* Comp = RelativeActor->GetComponentByClass<USkeletalMeshComponent>())
		{
			SkeletalMeshComponentForRelativeActor = Comp;
		}
	}
}

void UComposableCameraRelativeFixedPoseNode::OnTickNode_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	FTransform CurrentRelativeTransform = FTransform::Identity;
	
	if (Method == EComposableCameraRelativeFixedPoseMethod::RelativeToTransform)
	{
		CurrentRelativeTransform = RelativeTransform;	
	}
	else if (Method == EComposableCameraRelativeFixedPoseMethod::RelativeToActor)
	{
		if (SkeletalMeshComponentForRelativeActor && SkeletalMeshComponentForRelativeActor->DoesSocketExist(RelativeSocket))
		{
			CurrentRelativeTransform = SkeletalMeshComponentForRelativeActor->GetSocketTransform(RelativeSocket);
		}
		else if (RelativeActor)
		{
			CurrentRelativeTransform = RelativeActor->GetActorTransform();	
		}
	}
	
	FVector TargetLocation = UKismetMathLibrary::TransformLocation(CurrentRelativeTransform, TargetTransform.GetLocation());
	FRotator TargetRotation = UKismetMathLibrary::TransformRotation(CurrentRelativeTransform, TargetTransform.GetRotation().Rotator());

	OutCameraPose.Position = TargetLocation;
	OutCameraPose.Rotation = TargetRotation;
}

void UComposableCameraRelativeFixedPoseNode::ReceiveInitializerNode(UComposableCameraCameraNodeBase* Initializer)
{
	if (UComposableCameraRelativeFixedPoseNode* CastedInitializer = Cast<UComposableCameraRelativeFixedPoseNode>(Initializer))
	{
		Method = CastedInitializer->Method;
		RelativeTransform = CastedInitializer->RelativeTransform;
		RelativeActor = CastedInitializer->RelativeActor;
		RelativeSocket = CastedInitializer->RelativeSocket;
		TargetTransform = CastedInitializer->TargetTransform;
	}
}
