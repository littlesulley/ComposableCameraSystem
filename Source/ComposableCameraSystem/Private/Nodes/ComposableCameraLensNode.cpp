// Copyright 2026 Sulley. All Rights Reserved.

#include "Nodes/ComposableCameraLensNode.h"

void UComposableCameraLensNode::OnTickNode_Implementation(float DeltaTime,
                                                         const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	// Input pins matching UPROPERTY names have already been resolved onto members
	// by the base TickNode prologue. Read members directly so a wired value of 0
	// stays meaningful for blend weights.
	OutCameraPose.FocalLength = FMath::Max(FocalLength, 1.f);
	OutCameraPose.Aperture = FMath::Max(Aperture, 0.1f);
	OutCameraPose.DiaphragmBladeCount = FMath::Max(DiaphragmBladeCount, 3);
	OutCameraPose.FocusDistance = FocusDistance;
	OutCameraPose.PhysicalCameraBlendWeight = FMath::Clamp(PhysicalCameraBlendWeight, 0.f, 1.f);

	if (bOverrideFieldOfViewFromFocalLength)
	{
		OutCameraPose.FieldOfView = -1.0;
	}
}

void UComposableCameraLensNode::GetPinDeclarations_Implementation(
	TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	// Input: FocalLength
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "FocalLength";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Lens_FocalLengthIn", "Focal Length");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.DefaultValueString = FString::SanitizeFloat(FocalLength);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Lens_FocalLengthTooltip",
			"Override the focal length (mm). If not connected, the node's FocalLength property is used.");
		OutPins.Add(Pin);
	}

	// Input: Aperture
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "Aperture";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Lens_ApertureIn", "Aperture");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.DefaultValueString = FString::SanitizeFloat(Aperture);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Lens_ApertureTooltip",
			"Override the aperture (f-stop). If not connected, the node's Aperture property is used.");
		OutPins.Add(Pin);
	}

	// Input: FocusDistance
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "FocusDistance";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Lens_FocusDistanceIn", "Focus Distance");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.DefaultValueString = FString::SanitizeFloat(FocusDistance);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Lens_FocusDistanceTooltip",
			"Override the focus distance (world units). Use <= 0 to leave the pose's no-DoF-focus sentinel in place.");
		OutPins.Add(Pin);
	}

	// Input: DiaphragmBladeCount
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "DiaphragmBladeCount";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Lens_BladeCountIn", "Diaphragm Blade Count");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Int32;
		Pin.bRequired = false;
		Pin.DefaultValueString = FString::FromInt(DiaphragmBladeCount);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Lens_BladeCountTooltip",
			"Override the diaphragm blade count. Affects bokeh polygon shape when DoF is active.");
		OutPins.Add(Pin);
	}

	// Input: PhysicalCameraBlendWeight
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "PhysicalCameraBlendWeight";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Lens_BlendWeightIn", "Physical Camera Blend Weight");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.DefaultValueString = FString::SanitizeFloat(PhysicalCameraBlendWeight);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Lens_BlendWeightTooltip",
			"Override the lens-driven DoF contribution weight [0,1]. Exposure is controlled by ExposureNode.");
		OutPins.Add(Pin);
	}
}
