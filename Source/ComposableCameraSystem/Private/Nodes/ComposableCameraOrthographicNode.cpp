// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraOrthographicNode.h"

void UComposableCameraOrthographicNode::OnTickNode_Implementation(float DeltaTime,
                                                                  const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	const int32 PinProjectionMode = GetInputPinValue<int32>("ProjectionMode");
	const float PinOrthoWidth     = GetInputPinValue<float>("OrthographicWidth");
	const float PinNearClip       = GetInputPinValue<float>("OrthoNearClipPlane");
	const float PinFarClip        = GetInputPinValue<float>("OrthoFarClipPlane");

	// ProjectionMode: Perspective = 0, Orthographic = 1, so a wired pin of 0
	// is indistinguishable from "unresolved". Fall back to UPROPERTY when the
	// pin reads 0. Authors who want to drive back to Perspective via wire
	// should set the UPROPERTY to Perspective and leave the pin unwired.
	if (PinProjectionMode > 0 && PinProjectionMode <= static_cast<int32>(ECameraProjectionMode::Orthographic))
	{
		OutCameraPose.ProjectionMode = static_cast<ECameraProjectionMode::Type>(PinProjectionMode);
	}
	else
	{
		OutCameraPose.ProjectionMode = ProjectionMode;
	}

	// Positive-sentinel fallback: view width and far clip must be > 0.
	OutCameraPose.OrthographicWidth = (PinOrthoWidth > 0.f) ? PinOrthoWidth : OrthographicWidth;
	OutCameraPose.OrthoFarClipPlane = (PinFarClip    > 0.f) ? PinFarClip    : OrthoFarClipPlane;

	// Near clip: 0 is a valid default value, so we cannot use the positive-sentinel
	// trick. Use pin value if strictly positive; otherwise fall back to UPROPERTY
	// (which itself may legitimately be 0). A wired-but-zero pin collapses to
	// the UPROPERTY. This is an accepted ambiguity for "0 is meaningful" fields.
	OutCameraPose.OrthoNearClipPlane = (PinNearClip > 0.f) ? PinNearClip : OrthoNearClipPlane;
}

void UComposableCameraOrthographicNode::GetPinDeclarations_Implementation(
	TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	// Input: ProjectionMode (as int32. Enum index into ECameraProjectionMode)
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "ProjectionMode";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Ortho_ProjectionIn", "Projection Mode");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Int32;
		Pin.bRequired = false;
		Pin.DefaultValueString = FString::FromInt(static_cast<int32>(ProjectionMode.GetValue()));
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Ortho_ProjectionTooltip",
			"Projection mode index (ECameraProjectionMode: 0 = Perspective, 1 = Orthographic). Pin value 0 falls back to UPROPERTY.");
		OutPins.Add(Pin);
	}

	// Input: OrthographicWidth
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "OrthographicWidth";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Ortho_WidthIn", "Orthographic Width");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.DefaultValueString = FString::SanitizeFloat(OrthographicWidth);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Ortho_WidthTooltip",
			"Orthographic view width in world units.");
		OutPins.Add(Pin);
	}

	// Input: OrthoNearClipPlane
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "OrthoNearClipPlane";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Ortho_NearIn", "Near Clip Plane");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.DefaultValueString = FString::SanitizeFloat(OrthoNearClipPlane);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Ortho_NearTooltip",
			"Ortho near clip plane in world units. Pin value 0 falls back to UPROPERTY.");
		OutPins.Add(Pin);
	}

	// Input: OrthoFarClipPlane
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "OrthoFarClipPlane";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Ortho_FarIn", "Far Clip Plane");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.DefaultValueString = FString::SanitizeFloat(OrthoFarClipPlane);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Ortho_FarTooltip",
			"Ortho far clip plane in world units.");
		OutPins.Add(Pin);
	}
}
