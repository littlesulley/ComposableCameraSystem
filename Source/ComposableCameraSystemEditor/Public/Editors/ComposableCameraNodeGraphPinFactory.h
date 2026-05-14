// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraphUtilities.h"

/**
 * Graph-panel pin factory scoped to the ComposableCameraSystemEditor custom
 * graph (the UEdGraph owned by UComposableCameraTypeAsset). Dispatches
 * SGraphPin widget creation based on per-pin state that the default
 * SGraphPin cannot express:
 *
 * - **Exposed-as-parameter input pins** receive SComposableCameraExposedPin,
 * a grey, non-interactive subclass. Exposed state lives on the owning
 * type asset's ExposedParameters array, not on the pin itself, so the
 * factory looks up the owning UComposableCameraNodeGraphNode and asks
 * IsInputPinExposed(PinName).
 *
 * Every other pin (exec pins, output data pins, non-exposed input data pins,
 * pins on variable graph nodes, pins on Start/Output sentinel nodes) falls
 * through with a null return, and SGraphNode::CreatePinWidget instantiates
 * the default SGraphPin from the engine's own factory chain.
 *
 * The factory is registered with FEdGraphUtilities::RegisterVisualPinFactory
 * from FComposableCameraSystemEditorModule::StartupModule and unregistered
 * in ShutdownModule. Unlike FComposableCameraGraphPanelPinFactory (which
 * lives in ComposableCameraSystemUncookedOnly and targets the Blueprint-side
 * UK2Node pins), this factory lives in the Editor module because the graph
 * nodes it inspects (UComposableCameraNodeGraphNode) are editor-only types.
 */
class FComposableCameraNodeGraphPinFactory: public FGraphPanelPinFactory
{
public:
	virtual TSharedPtr<class SGraphPin> CreatePin(class UEdGraphPin* InPin) const override;
};
