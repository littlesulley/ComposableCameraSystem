// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "SGraphNode.h"

class UComposableCameraNodeGraphNode;

/**
 * Custom SGraphNode widget for UComposableCameraNodeGraphNode instances.
 *
 * During normal editing the node looks identical to the default SGraphNode.
 * When the toolkit's debug ticker is active and pushes FDebugState into the
 * backing UComposableCameraNodeGraphNode, this widget adds visual overlays:
 *
 *   - **Pose footer:** a compact readout of the camera pose after the node
 *     executed (Position, Rotation, FOV), drawn below the node body.
 *
 *   - **Output pin values:** one line per output pin showing the live
 *     formatted value from DebugState.OutputPinDisplayValues.
 *
 * The debug footer is rendered as an overlay that does NOT participate in the
 * node's width calculation. It can extend vertically and horizontally beyond
 * the node body without forcing the node itself to grow. This prevents the
 * debug info from widening all nodes during PIE.
 *
 * Instantiated by FComposableCameraGraphNodeFactory, which is registered with
 * FEdGraphUtilities from the editor module's StartupModule.
 */
class COMPOSABLECAMERASYSTEMEDITOR_API SComposableCameraGraphNode : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SComposableCameraGraphNode) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UComposableCameraNodeGraphNode* InNode);

	// ─── SGraphNode Interface ──────────────────────────────────────────

	virtual void UpdateGraphNode() override;

	// ─── SWidget Interface ─────────────────────────────────────────────

	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

private:
	/** The backing graph node (typed accessor to avoid repeated casts). */
	UComposableCameraNodeGraphNode* CameraGraphNode = nullptr;

	/** Whether the debug state indicates this node was active last tick. */
	bool IsDebugActive() const;

	/** Paint the debug footer below the node body. */
	void PaintDebugFooter(
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId) const;

	/** Paint a small warning / error icon at the top-right of the node header
	 *  when the backing GraphNode has a non-empty `ErrorMsg`. Driven by the
	 *  build messages pushed onto GraphNode via
	 *  `UComposableCameraNodeGraph::ApplyBuildMessagesToGraphNodes`. Reads the
	 *  fields fresh every frame, so fixing a validation issue removes the
	 *  badge on the next `SyncToTypeAsset`. */
	void PaintValidationBadge(
		const FGeometry& AllottedGeometry,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId) const;
};
