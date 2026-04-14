// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraCameraOffsetNode.h"

#include "Kismet/KismetMathLibrary.h"

void UComposableCameraCameraOffsetNode::OnTickNode_Implementation(
	float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	FVector Pivot = GetInputPinValue<FVector>("PivotPosition");

	FRotator CameraRotation = OutCameraPose.Rotation;

	FVector OutPosition = Pivot
		+ UKismetMathLibrary::GetRightVector(CameraRotation) * CameraOffset.Y
		+ UKismetMathLibrary::GetUpVector(CameraRotation) * CameraOffset.Z
		+ UKismetMathLibrary::GetForwardVector(CameraRotation) * CameraOffset.X;

	OutCameraPose.Position = OutPosition;
}


void UComposableCameraCameraOffsetNode::GetPinDeclarations_Implementation(
	TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	// Input: pivot position to offset from.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "PivotPosition";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "CamOffset_PivotIn", "Pivot Position");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Vector3D;
		Pin.bRequired = true;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "CamOffset_PivotInTooltip",
			"The pivot position to apply camera offset from (in camera space).");
		OutPins.Add(Pin);
	}
}
