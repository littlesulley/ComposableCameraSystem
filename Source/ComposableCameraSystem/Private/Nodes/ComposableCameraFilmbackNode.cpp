// Copyright 2026 Sulley. All Rights Reserved.

#include "Nodes/ComposableCameraFilmbackNode.h"

void UComposableCameraFilmbackNode::OnTickNode_Implementation(float DeltaTime,
                                                              const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	// NOTE: Pin names must match UPROPERTY FNames verbatim (including the
	// `b` prefix on bools) so the node Details customization pairs them into
	// a single unified row. Dropping the `b` here would cause the Details
	// panel to render the UPROPERTY row AND a plain-text SEditableTextBox
	// fallback row for the pin, side by side. Exactly the "duplicate
	// parameter" bug this node was fixed for.
	const float PinSensorWidth   = GetInputPinValue<float>("SensorWidth");
	const float PinSensorHeight  = GetInputPinValue<float>("SensorHeight");
	const float PinSqueeze       = GetInputPinValue<float>("SqueezeFactor");
	const float PinOverscan      = GetInputPinValue<float>("Overscan");
	const bool  PinConstrainAR   = GetInputPinValue<bool>("bConstrainAspectRatio");
	const bool  PinOverrideAxis  = GetInputPinValue<bool>("bOverrideAspectRatioAxisConstraint");
	const int32 PinAxis          = GetInputPinValue<int32>("AspectRatioAxisConstraint");

	// Positive-sentinel fallback: sensor dimensions and squeeze must be > 0.
	OutCameraPose.SensorWidth   = (PinSensorWidth  > 0.f) ? PinSensorWidth  : SensorWidth;
	OutCameraPose.SensorHeight  = (PinSensorHeight > 0.f) ? PinSensorHeight : SensorHeight;
	OutCameraPose.SqueezeFactor = (PinSqueeze      > 0.f) ? PinSqueeze      : SqueezeFactor;

	// Overscan: 0 is a valid "no overscan" value and is the UPROPERTY default.
	// An unresolved pin also returns 0, so in either case writing 0 is correct.
	// A wired non-zero pin overrides; otherwise the UPROPERTY value is used.
	OutCameraPose.Overscan = (PinOverscan > 0.f) ? PinOverscan : Overscan;

	// Bools: pin returns false when unresolved. To keep the UPROPERTY meaningful
	// when no wire is attached, we OR the pin and UPROPERTY values. I.e. either
	// path can set the flag to true. This matches the "pin overrides if truthy"
	// convention used elsewhere; a wire that explicitly wants false should be
	// achieved by setting the UPROPERTY to false and leaving the pin unwired.
	OutCameraPose.ConstrainAspectRatio             = PinConstrainAR   || bConstrainAspectRatio;
	OutCameraPose.OverrideAspectRatioAxisConstraint = PinOverrideAxis || bOverrideAspectRatioAxisConstraint;

	// Enum axis: pin returning 0 (= AspectRatio_MaintainYFOV) is the engine default.
	// If the pin resolves to a non-default value we assume intent; otherwise fall
	// back to the UPROPERTY. Authors who deliberately want MaintainYFOV via a wire
	// should set the UPROPERTY to the same value.
	if (PinAxis > 0 && PinAxis < EAspectRatioAxisConstraint::AspectRatio_MAX)
	{
		OutCameraPose.AspectRatioAxisConstraint = static_cast<EAspectRatioAxisConstraint>(PinAxis);
	}
	else
	{
		OutCameraPose.AspectRatioAxisConstraint = AspectRatioAxisConstraint;
	}
}

void UComposableCameraFilmbackNode::GetPinDeclarations_Implementation(
	TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	// Input: SensorWidth
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "SensorWidth";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Filmback_SensorWIn", "Sensor Width");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.DefaultValueString = FString::SanitizeFloat(SensorWidth);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Filmback_SensorWTooltip",
			"Sensor width in millimetres. Feeds FOV resolution when the pose is in focal-length mode.");
		OutPins.Add(Pin);
	}

	// Input: SensorHeight
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "SensorHeight";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Filmback_SensorHIn", "Sensor Height");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.DefaultValueString = FString::SanitizeFloat(SensorHeight);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Filmback_SensorHTooltip",
			"Sensor height in millimetres.");
		OutPins.Add(Pin);
	}

	// Input: SqueezeFactor
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "SqueezeFactor";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Filmback_SqueezeIn", "Squeeze Factor");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.DefaultValueString = FString::SanitizeFloat(SqueezeFactor);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Filmback_SqueezeTooltip",
			"Anamorphic squeeze factor. 1.0 = spherical lens, 2.0 = classic 2x anamorphic.");
		OutPins.Add(Pin);
	}

	// Input: Overscan
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "Overscan";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Filmback_OverscanIn", "Overscan");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.DefaultValueString = FString::SanitizeFloat(Overscan);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Filmback_OverscanTooltip",
			"Sensor overscan percentage (0 = none).");
		OutPins.Add(Pin);
	}

	// Input: bConstrainAspectRatio. Pin name keeps the `b` prefix so it matches
	// the UPROPERTY FName exactly (required by the Details customization).
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "bConstrainAspectRatio";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Filmback_ConstrainARIn", "Constrain Aspect Ratio");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Bool;
		Pin.bRequired = false;
		Pin.DefaultValueString = bConstrainAspectRatio ? TEXT("true") : TEXT("false");
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Filmback_ConstrainARTooltip",
			"When true, letterbox / pillarbox so the viewport matches the sensor aspect ratio.");
		OutPins.Add(Pin);
	}

	// Input: bOverrideAspectRatioAxisConstraint. Same b-prefix rule.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "bOverrideAspectRatioAxisConstraint";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Filmback_OverrideAxisIn", "Override Aspect Ratio Axis Constraint");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Bool;
		Pin.bRequired = false;
		Pin.DefaultValueString = bOverrideAspectRatioAxisConstraint ? TEXT("true") : TEXT("false");
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Filmback_OverrideAxisTooltip",
			"When true, AspectRatioAxisConstraint overrides the project's default axis constraint.");
		OutPins.Add(Pin);
	}

	// Input: AspectRatioAxisConstraint (as int32. Enum index into EAspectRatioAxisConstraint)
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "AspectRatioAxisConstraint";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Filmback_AxisIn", "Aspect Ratio Axis Constraint");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Int32;
		Pin.bRequired = false;
		Pin.DefaultValueString = FString::FromInt(static_cast<int32>(AspectRatioAxisConstraint.GetValue()));
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "Filmback_AxisTooltip",
			"Axis constraint index (EAspectRatioAxisConstraint). Only honoured when OverrideAspectRatioAxisConstraint is true.");
		OutPins.Add(Pin);
	}
}
