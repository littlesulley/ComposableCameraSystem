// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraphUtilities.h"

/**
 * Graph-panel node factory that provides SComposableCameraGraphNode for
 * UComposableCameraNodeGraphNode instances in the Camera Type Asset editor.
 *
 * The custom SGraphNode is visually identical to the default during normal
 * editing. During PIE, when the toolkit's debug ticker pushes FDebugState
 * into graph nodes, the widget renders additional overlays: an active-node
 * border glow, output pin value badges, and a pose footer.
 *
 * Registered with FEdGraphUtilities::RegisterVisualNodeFactory from
 * FComposableCameraSystemEditorModule::StartupModule, alongside the existing
 * pin factory.
 */
class FComposableCameraGraphNodeFactory: public FGraphPanelNodeFactory
{
public:
	virtual TSharedPtr<class SGraphNode> CreateNode(class UEdGraphNode* InNode) const override;
};
