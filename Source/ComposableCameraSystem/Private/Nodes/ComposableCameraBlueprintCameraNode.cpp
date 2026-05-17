// Copyright 2026 Sulley. All Rights Reserved.

#include "Nodes/ComposableCameraBlueprintCameraNode.h"

#include "Core/ComposableCameraPlayerCameraManager.h"

FVector2D UComposableCameraBlueprintCameraNode::GetInputPinValueVector2D(FName PinName) const
{
	return GetInputPinValue<FVector2D>(PinName);
}

void UComposableCameraBlueprintCameraNode::SetOutputPinValueVector2D(FName PinName, FVector2D Value)
{
	SetOutputPinValue<FVector2D>(PinName, Value);
}

FComposableCameraPose UComposableCameraBlueprintCameraNode::GetCurrentCameraPose() const
{
	if (OwningPlayerCameraManager)
	{
		return OwningPlayerCameraManager->GetCurrentCameraPose();
	}
	return FComposableCameraPose();
}
