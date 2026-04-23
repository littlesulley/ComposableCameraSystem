// Copyright Sulley. All rights reserved.

#include "Editors/ComposableCameraStartGraphNode.h"
#include "ComposableCameraEditorStyle.h"

#define LOCTEXT_NAMESPACE "ComposableCameraStartGraphNode"

void UComposableCameraStartGraphNode::AllocateDefaultPins()
{
	// Single execution output pin — chains to the first camera node.
	// PN_ExecOut and the boilerplate live on UComposableCameraGraphNodeBase.
	CreateExecOutPin();
}

FText UComposableCameraStartGraphNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("StartNodeTitle", "Start");
}

FLinearColor UComposableCameraStartGraphNode::GetNodeTitleColor() const
{
	// Green to indicate the entry point. Palette lives in
	// FComposableCameraEditorColors (ComposableCameraEditorStyle.h).
	return FComposableCameraEditorColors::StartNodeTitle;
}

FText UComposableCameraStartGraphNode::GetTooltipText() const
{
	return LOCTEXT("StartNodeTooltip", "The starting point of the camera pipeline. Nodes execute in order from left to right.");
}

#undef LOCTEXT_NAMESPACE
