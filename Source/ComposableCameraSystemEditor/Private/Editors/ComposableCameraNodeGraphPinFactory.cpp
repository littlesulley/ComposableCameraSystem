// Copyright Sulley. All rights reserved.

#include "Editors/ComposableCameraNodeGraphPinFactory.h"

#include "Editors/ComposableCameraNodeGraphNode.h"
#include "Editors/SComposableCameraExposedPin.h"
#include "EdGraph/EdGraphPin.h"

TSharedPtr<SGraphPin> FComposableCameraNodeGraphPinFactory::CreatePin(UEdGraphPin* InPin) const
{
	if (!InPin)
	{
		return nullptr;
	}

	// Only input pins on our custom graph-node type can be exposed. Output
	// pins, exec pins, variable-node pins, and sentinel (Start/Output) pins
	// all fall through to the default factory chain.
	if (InPin->Direction != EGPD_Input)
	{
		return nullptr;
	}

	UComposableCameraNodeGraphNode* CameraGraphNode =
		Cast<UComposableCameraNodeGraphNode>(InPin->GetOuter());
	if (!CameraGraphNode)
	{
		return nullptr;
	}

	// Exposure state is authored on the owning type asset's ExposedParameters
	// array, not on the pin. IsInputPinExposed walks up the graph to the type
	// asset and checks for a matching TargetNodeIndex + TargetPinName. Because
	// ReconstructPins drops and recreates every SGraphPin widget on the node
	// when exposure state changes, the factory is guaranteed to re-dispatch
	// with a fresh answer each time a user toggles Expose/Unexpose — there's
	// no need to cache the result or listen for exposure-change broadcasts.
	if (CameraGraphNode->IsInputPinExposed(InPin->PinName))
	{
		return SNew(SComposableCameraExposedPin, InPin);
	}

	return nullptr;
}
