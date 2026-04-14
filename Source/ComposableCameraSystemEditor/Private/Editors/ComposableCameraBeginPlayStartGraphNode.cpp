// Copyright Sulley. All rights reserved.

#include "Editors/ComposableCameraBeginPlayStartGraphNode.h"

#define LOCTEXT_NAMESPACE "ComposableCameraBeginPlayStartGraphNode"

void UComposableCameraBeginPlayStartGraphNode::AllocateDefaultPins()
{
	// Single execution output pin — chains to the first compute node on the
	// BeginPlay compute chain. Mirrors UComposableCameraStartGraphNode exactly;
	// the only thing that distinguishes this sentinel at runtime is its class
	// identity, which the sync phases use to discriminate which chain a walk
	// belongs to.
	CreateExecOutPin();
}

FText UComposableCameraBeginPlayStartGraphNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("BeginPlayStartNodeTitle", "BeginPlay");
}

FLinearColor UComposableCameraBeginPlayStartGraphNode::GetNodeTitleColor() const
{
	// Amber — visually distinct from the green main Start sentinel so the
	// two execution chains read at a glance on the canvas without the user
	// having to squint at titles.
	return FLinearColor(0.85f, 0.55f, 0.1f);
}

FText UComposableCameraBeginPlayStartGraphNode::GetTooltipText() const
{
	return LOCTEXT("BeginPlayStartNodeTooltip",
		"The starting point of the BeginPlay compute chain. Compute nodes wired here run exactly once during camera activation, before the first per-frame tick.");
}

#undef LOCTEXT_NAMESPACE
