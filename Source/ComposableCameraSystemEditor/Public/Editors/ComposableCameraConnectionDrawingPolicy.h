// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConnectionDrawingPolicy.h"

/**
 * Custom connection drawing policy for the Camera Type Asset graph.
 *
 * During PIE debug monitoring, exec wires between two active (ticked-this-frame)
 * nodes are drawn with a highlighted color and increased thickness to visualize
 * the live execution flow. Non-exec wires and wires touching inactive nodes
 * use the default appearance.
 *
 * The policy reads debug-active state from UComposableCameraNodeGraphNode::DebugState
 * and UComposableCameraVariableGraphNode::DebugState. Nodes that don't carry debug
 * state (sentinels, begin-play start) are considered always-active so the highlight
 * chain doesn't break at the Start → first-camera-node boundary.
 */
class FComposableCameraConnectionDrawingPolicy : public FConnectionDrawingPolicy
{
public:
	FComposableCameraConnectionDrawingPolicy(
		int32 InBackLayerID,
		int32 InFrontLayerID,
		float InZoomFactor,
		const FSlateRect& InClippingRect,
		FSlateWindowElementList& InDrawElements);

	// ─── FConnectionDrawingPolicy Interface ───────────────────────────────

	virtual void DetermineWiringStyle(
		UEdGraphPin* OutputPin,
		UEdGraphPin* InputPin,
		FConnectionParams& Params) override;

private:
	/** Check if a graph node is debug-active (its runtime counterpart was ticked).
	 *  Nodes without debug state (sentinels) return true so the highlight chain
	 *  doesn't break at entry/exit points. */
	static bool IsNodeDebugActive(const UEdGraphNode* Node);

	/** Check if a pin is an exec pin (ExecIn / ExecOut category). */
	static bool IsExecPin(const UEdGraphPin* Pin);
};
