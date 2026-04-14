// Copyright Sulley. All rights reserved.

#include "Editors/ComposableCameraOutputGraphNode.h"

#define LOCTEXT_NAMESPACE "ComposableCameraOutputGraphNode"

void UComposableCameraOutputGraphNode::AllocateDefaultPins()
{
	// Execution input pin — receives the execution chain from the last
	// camera node. PN_ExecIn and the boilerplate live on the shared base.
	CreateExecInPin();
}

FText UComposableCameraOutputGraphNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("OutputNodeTitle", "Output");
}

FLinearColor UComposableCameraOutputGraphNode::GetNodeTitleColor() const
{
	// Red to indicate the terminal point.
	return FLinearColor(0.7f, 0.1f, 0.1f);
}

FText UComposableCameraOutputGraphNode::GetTooltipText() const
{
	return LOCTEXT("OutputNodeTooltip", "The camera pipeline output. Terminates the execution chain.");
}

#undef LOCTEXT_NAMESPACE
