// Copyright 2026 Sulley. All Rights Reserved.

#include "Editors/ComposableCameraBeginPlayStartGraphNode.h"
#include "ComposableCameraEditorStyle.h"

#define LOCTEXT_NAMESPACE "ComposableCameraBeginPlayStartGraphNode"

void UComposableCameraBeginPlayStartGraphNode::AllocateDefaultPins()
{
	// Single execution output pin - chains to the first compute node on the
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
	// Amber - visually distinct from the green main Start sentinel so the
	// two execution chains read at a glance on the canvas without the user
	// having to squint at titles. Palette lives in
	// FComposableCameraEditorColors (ComposableCameraEditorStyle.h).
	return FComposableCameraEditorColors::BeginPlayStartNodeTitle;
}

FText UComposableCameraBeginPlayStartGraphNode::GetTooltipText() const
{
	return LOCTEXT("BeginPlayStartNodeTooltip",
		"The starting point of the BeginPlay compute chain. Compute nodes wired here run exactly once during camera activation, before the first per-frame tick.");
}

#undef LOCTEXT_NAMESPACE
