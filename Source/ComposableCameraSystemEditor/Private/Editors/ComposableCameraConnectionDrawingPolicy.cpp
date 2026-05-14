// Copyright Sulley. All rights reserved.

#include "Editors/ComposableCameraConnectionDrawingPolicy.h"
#include "Editors/ComposableCameraNodeGraphNode.h"
#include "Editors/ComposableCameraVariableGraphNode.h"

#include "EdGraphSchema_K2.h"

FComposableCameraConnectionDrawingPolicy::FComposableCameraConnectionDrawingPolicy(int32 InBackLayerID,
	int32 InFrontLayerID,
	float InZoomFactor,
	const FSlateRect& InClippingRect,
	FSlateWindowElementList& InDrawElements)
	: FConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements)
{
}

void FComposableCameraConnectionDrawingPolicy::DetermineWiringStyle(UEdGraphPin* OutputPin,
	UEdGraphPin* InputPin,
	FConnectionParams& Params)
{
	// Let the base class set up defaults first.
	FConnectionDrawingPolicy::DetermineWiringStyle(OutputPin, InputPin, Params);

	// Only highlight exec wires - data wires keep their default appearance.
	if (!OutputPin || !InputPin || !IsExecPin(OutputPin))
	{
		return;
	}

	// If both connected nodes are debug-active, highlight the wire.
	const UEdGraphNode* SourceNode = OutputPin->GetOwningNode();
	const UEdGraphNode* TargetNode = InputPin->GetOwningNode();

	if (IsNodeDebugActive(SourceNode) && IsNodeDebugActive(TargetNode))
	{
		Params.WireColor = FLinearColor(0.2f, 0.85f, 0.4f, 1.0f); // bright green
		Params.WireThickness = 3.0f;
		Params.bDrawBubbles = true;
	}
}

bool FComposableCameraConnectionDrawingPolicy::IsNodeDebugActive(const UEdGraphNode* Node)
{
	if (!Node)
	{
		return false;
	}

	// Camera graph nodes - check DebugState.bIsActive.
	if (const UComposableCameraNodeGraphNode* CameraNode = Cast<UComposableCameraNodeGraphNode>(Node))
	{
		return CameraNode->DebugState.bIsActive;
	}

	// Variable graph nodes - check DebugState.bIsActive.
	if (const UComposableCameraVariableGraphNode* VarNode = Cast<UComposableCameraVariableGraphNode>(Node))
	{
		return VarNode->DebugState.bIsActive;
	}

	// Sentinel nodes (Start, Output, BeginPlay Start) don't carry debug state.
	// Treat them as always-active so the highlight chain doesn't break at the
	// Start->first-camera-node or last-camera-node -> Output boundary.
	return true;
}

bool FComposableCameraConnectionDrawingPolicy::IsExecPin(const UEdGraphPin* Pin)
{
	return Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
}
