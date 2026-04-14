// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraFieldOfViewNode.h"

#include "Field/FieldSystemNoiseAlgo.h"

void UComposableCameraFieldOfViewNode::OnTickNode_Implementation(float DeltaTime,
                                                                 const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	// Read FOV from pin (overrides the UPROPERTY if connected/exposed).
	float FOV = GetInputPinValue<float>("FieldOfView");
	if (FOV > 0.f)
	{
		OutCameraPose.FieldOfView = FOV;
	}
	else
	{
		OutCameraPose.FieldOfView = FieldOfView;
	}
}

void UComposableCameraFieldOfViewNode::GetPinDeclarations_Implementation(
	TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	// Input: override FOV value.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "FieldOfView";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "FOV_In", "Field Of View");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.DefaultValueString = FString::SanitizeFloat(FieldOfView);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "FOV_InTooltip",
			"Override the field of view. If not connected, the node's FieldOfView property is used.");
		OutPins.Add(Pin);
	}
}

