// Copyright 2026 Sulley. All Rights Reserved.

#include "Nodes/ComposableCameraExposureNode.h"

void UComposableCameraExposureNode::OnTickNode_Implementation(float DeltaTime,
                                                             const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	OutCameraPose.ISO = FMath::Max(ISO, 1.f);
	OutCameraPose.ShutterSpeed = FMath::Max(ShutterSpeed, 1.f);
	OutCameraPose.ExposureBlendWeight = FMath::Clamp(ExposureBlendWeight, 0.f, 1.f);
}

void UComposableCameraExposureNode::GetPinDeclarations_Implementation(
	TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	// Input: ISO
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "ISO";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Exposure_ISOIn", "ISO");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.DefaultValueString = FString::SanitizeFloat(ISO);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Exposure_ISOTooltip",
			"Override sensor sensitivity.");
		OutPins.Add(Pin);
	}

	// Input: ShutterSpeed
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "ShutterSpeed";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Exposure_ShutterSpeedIn", "Shutter Speed");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.DefaultValueString = FString::SanitizeFloat(ShutterSpeed);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Exposure_ShutterSpeedTooltip",
			"Override shutter speed in 1/seconds. 60 means 1/60s.");
		OutPins.Add(Pin);
	}

	// Input: ExposureBlendWeight
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "ExposureBlendWeight";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Exposure_BlendWeightIn", "Exposure Blend Weight");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.DefaultValueString = FString::SanitizeFloat(ExposureBlendWeight);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Exposure_BlendWeightTooltip",
			"Override exposure contribution weight [0,1]. 0 leaves ISO/Shutter untouched.");
		OutPins.Add(Pin);
	}
}
