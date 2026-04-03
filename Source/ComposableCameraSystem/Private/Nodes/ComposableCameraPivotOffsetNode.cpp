// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraPivotOffsetNode.h"

#include "Core/ComposableCameraPlayerCameraManager.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"

void UComposableCameraPivotOffsetNode::OnBeginPlayNode_Implementation(const FComposableCameraPose& CurrentCameraPose)
{
	UpdatePivotOffset(CurrentCameraPose);
}

void UComposableCameraPivotOffsetNode::OnTickNode_Implementation(float DeltaTime,
                                                                 const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	UpdatePivotOffset(CurrentCameraPose);
}

void UComposableCameraPivotOffsetNode::ReceiveInitializerNode(UComposableCameraCameraNodeBase* Initializer)
{
	if (UComposableCameraPivotOffsetNode* CastedInitializer = Cast<UComposableCameraPivotOffsetNode>(Initializer))
	{
		PivotOffsetType = CastedInitializer->PivotOffsetType;
		ActorForLocalSpace = CastedInitializer->ActorForLocalSpace;
		PivotOffset = CastedInitializer->PivotOffset;
	}
}

void UComposableCameraPivotOffsetNode::UpdatePivotOffset(const FComposableCameraPose& CurrentCameraPose)
{
	FVector Pivot = ContextPivotPosition.Value;
	if (ContextPivotPosition.Variable)
	{
		Pivot = ContextPivotPosition.Variable->RuntimeValue;
	}
	
	switch (PivotOffsetType)
	{
	case ECameraPivotOffset::ActorLocalSpace:
		if (ActorForLocalSpace.Get())
		{
			Pivot += ActorForLocalSpace.Get()->GetActorForwardVector() * PivotOffset[0];
			Pivot += ActorForLocalSpace.Get()->GetActorRightVector() * PivotOffset[1];
			Pivot += ActorForLocalSpace.Get()->GetActorUpVector() * PivotOffset[2];
		}
		else
		{
			Pivot += PivotOffset;
		}
		break;
	case ECameraPivotOffset::CameraSpace:
		{
			FRotator CameraRotation = CurrentCameraPose.Rotation;
			Pivot += UKismetMathLibrary::GetForwardVector(CameraRotation) * PivotOffset[0];
			Pivot += UKismetMathLibrary::GetRightVector(CameraRotation) * PivotOffset[1];
			Pivot += UKismetMathLibrary::GetUpVector(CameraRotation) * PivotOffset[2];
		}
		break;
	case ECameraPivotOffset::WorldSpace:
		Pivot += PivotOffset;
		break;
	}

	if (ContextPivotPosition.Variable)
	{
		ContextPivotPosition.Variable->RuntimeValue = Pivot;
	}
	else
	{
		ContextPivotPosition.Value = Pivot;
	}

	if (OwningPlayerCameraManager && OwningPlayerCameraManager->bDrawDebugInformation)
	{
		UKismetSystemLibrary::DrawDebugSphere(this, Pivot, 20, 12, FLinearColor::Yellow, 0, 1);
	}
}
