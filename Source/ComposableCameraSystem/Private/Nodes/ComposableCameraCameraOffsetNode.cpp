// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraCameraOffsetNode.h"

#include "Kismet/KismetMathLibrary.h"

void UComposableCameraCameraOffsetNode::OnTickNode_Implementation(
	float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	// PivotPosition and CameraOffset are pin-matched UPROPERTYs. The base
	// TickNode prologue calls ResolveAllInputPins() before OnTickNode_Implementation
	// runs, so the members already reflect the wired / exposed / default value.
	FRotator CameraRotation = OutCameraPose.Rotation;

	FVector OutPosition = PivotPosition
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
		Pin.DefaultValueString = PivotPosition.ToString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "CamOffset_PivotInTooltip",
			"The pivot position to apply camera offset from (in camera space).");
		OutPins.Add(Pin);
	}

	// Input: camera-space offset applied on top of the pivot.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "CameraOffset";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "CamOffset_OffsetIn", "Camera Offset");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Vector3D;
		Pin.bRequired = false;
		Pin.DefaultValueString = CameraOffset.ToString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "CamOffset_OffsetInTooltip",
			"Offset applied on top of the pivot in camera space (X=forward, Y=right, Z=up).");
		OutPins.Add(Pin);
	}
}
