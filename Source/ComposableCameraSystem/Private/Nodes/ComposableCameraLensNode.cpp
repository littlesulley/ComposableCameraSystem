// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraLensNode.h"

void UComposableCameraLensNode::OnTickNode_Implementation(float DeltaTime,
                                                         const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	// Positive-sentinel fallback pattern: pin returns 0 when unresolved (no wire,
	// no exposed param, no per-instance override). For physically meaningful
	// lens parameters 0 is never a valid value, so (v > 0) distinguishes
	// "wired / authored" from "use UPROPERTY default".
	const float PinFocalLength    = GetInputPinValue<float>("FocalLength");
	const float PinAperture       = GetInputPinValue<float>("Aperture");
	const float PinFocusDistance  = GetInputPinValue<float>("FocusDistance");
	const int32 PinBladeCount     = GetInputPinValue<int32>("DiaphragmBladeCount");
	const float PinBlendWeight    = GetInputPinValue<float>("PhysicalCameraBlendWeight");

	OutCameraPose.FocalLength           = (PinFocalLength > 0.f) ? PinFocalLength : FocalLength;
	OutCameraPose.Aperture              = (PinAperture    > 0.f) ? PinAperture    : Aperture;
	OutCameraPose.DiaphragmBladeCount   = (PinBladeCount  > 0)   ? PinBladeCount  : DiaphragmBladeCount;

	// FocusDistance has a real "no DoF override" sentinel at -1 that we must preserve.
	// Pin value > 0  → author wants a specific focus distance.
	// Pin value == 0 → unresolved → use UPROPERTY (which may itself be -1).
	// Pin value < 0  → author explicitly cleared the sentinel via the UPROPERTY path.
	OutCameraPose.FocusDistance = (PinFocusDistance > 0.f) ? PinFocusDistance : FocusDistance;

	// PhysicalCameraBlendWeight: 0 is a meaningful "disable physical contribution" value,
	// so we cannot use the positive-sentinel trick. Instead, use the pin value only if
	// it is strictly positive; a zero pin read means "unresolved, fall back to UPROPERTY".
	// Authors who actively want to disable physical contribution can set the UPROPERTY to 0.
	OutCameraPose.PhysicalCameraBlendWeight = (PinBlendWeight > 0.f) ? PinBlendWeight : PhysicalCameraBlendWeight;

	// FOV mode coupling — only clear FieldOfView if this node is the authoritative FOV source.
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
			"Override the focus distance (world units). Pass > 0 for an explicit focus distance; 0 means 'unresolved, use UPROPERTY default'.");
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
			"Override the physical-camera contribution weight [0,1]. Pass > 0 to drive DoF/exposure; 0 on the pin means 'use UPROPERTY'.");
		OutPins.Add(Pin);
	}
}
