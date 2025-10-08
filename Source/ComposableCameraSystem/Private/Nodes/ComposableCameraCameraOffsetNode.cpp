// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraCameraOffsetNode.h"

#include "Kismet/KismetMathLibrary.h"

void UComposableCameraCameraOffsetNode::OnTickNode_Implementation(
	float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	FVector Pivot {};
	
	if (ContextPivotPosition.Variable)
	{
		Pivot = ContextPivotPosition.Variable->RuntimeValue;
	}
	else
	{
		Pivot = ContextPivotPosition.Value;
	}

	FRotator CameraRotation = OutCameraPose.Rotation;

	FVector OutPosition = Pivot
	+ UKismetMathLibrary::GetRightVector(CameraRotation) * CameraOffset.Y
	+ UKismetMathLibrary::GetUpVector(CameraRotation) * CameraOffset.Z
	+ UKismetMathLibrary::GetForwardVector(CameraRotation) * CameraOffset.X;

	OutCameraPose.Position = OutPosition;
}

void UComposableCameraCameraOffsetNode::ReceiveInitializerNode(UComposableCameraCameraNodeBase* Initializer)
{
	if (UComposableCameraCameraOffsetNode* CastedInitializer = Cast<UComposableCameraCameraOffsetNode>(Initializer))
	{
		CameraOffset = CastedInitializer->CameraOffset;
	}
}
