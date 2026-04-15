// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraFieldOfViewNode.h"

#include "Field/FieldSystemNoiseAlgo.h"

void UComposableCameraFieldOfViewNode::OnTickNode_Implementation(float DeltaTime,
                                                                 const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	// FieldOfView and every dynamic-FoV parameter are pin-matched UPROPERTYs —
	// ResolveAllInputPins() in the base TickNode prologue has already written
	// any wired / exposed / default-override value into the members. Read them
	// directly.
	// Use SetFieldOfViewDegrees so the FocalLength sentinel is cleared and this pose is unambiguously
	// in degrees mode — otherwise GetEffectiveFieldOfView() downstream might resolve to the focal length
	// value instead of our explicit degrees.
	OutCameraPose.SetFieldOfViewDegrees(FieldOfView);
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

	// Input: toggle dynamic FOV.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "bDynamicFov";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "FOV_DynIn", "Dynamic FOV");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Bool;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = bDynamicFov ? TEXT("true") : TEXT("false");
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "FOV_DynInTooltip",
			"When true, FOV is driven dynamically based on the actors tracked in ActorsForDynamicFoV.");
		OutPins.Add(Pin);
	}

	// Input: min FOV for dynamic mode.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "MinFoV";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "FOV_MinIn", "Min FOV");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = FString::SanitizeFloat(MinFoV);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "FOV_MinInTooltip",
			"Lower bound of the dynamic FOV range (degrees).");
		OutPins.Add(Pin);
	}

	// Input: max FOV for dynamic mode.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "MaxFoV";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "FOV_MaxIn", "Max FOV");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = FString::SanitizeFloat(MaxFoV);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "FOV_MaxInTooltip",
			"Upper bound of the dynamic FOV range (degrees).");
		OutPins.Add(Pin);
	}

	// Input: FoV damping.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "FoVDamping";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "FOV_DampIn", "FOV Damping");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = FString::SanitizeFloat(FoVDamping);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "FOV_DampInTooltip",
			"Damping factor applied to dynamic FOV updates.");
		OutPins.Add(Pin);
	}

	// Input: desired target viewport size.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "DesiredTargetViewportSize";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "FOV_VPSizeIn", "Desired Target Viewport Size");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = FString::SanitizeFloat(DesiredTargetViewportSize);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "FOV_VPSizeInTooltip",
			"Target screen-space size (percent) the subject should occupy when dynamic FOV stops zooming.");
		OutPins.Add(Pin);
	}
}

