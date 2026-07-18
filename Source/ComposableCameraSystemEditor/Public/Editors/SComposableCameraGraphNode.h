// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SGraphNode.h"

class UComposableCameraNodeGraphNode;
class SToolTip;
class SWindow;

/**
 * Custom SGraphNode widget for UComposableCameraNodeGraphNode instances.
 *
 * During normal editing the node looks identical to the default SGraphNode.
 * When the toolkit's debug ticker is active and pushes FDebugState into the
 * backing UComposableCameraNodeGraphNode, this widget adds visual overlays:
 *
 * - **Pose footer:** a compact readout of the camera pose after the node
 * executed (Position, Rotation, FOV), drawn below the node body.
 *
 * - **Output pin values:** one line per output pin showing the live
 * formatted value from DebugState.OutputPinDisplayValues.
 *
 * The debug footer is rendered as an overlay that does NOT participate in the
 * node's width calculation. It can extend vertically and horizontally beyond
 * the node body without forcing the node itself to grow. This prevents the
 * debug info from widening all nodes during PIE.
 *
 * Instantiated by FComposableCameraGraphNodeFactory, which is registered with
 * FEdGraphUtilities from the editor module's StartupModule.
 */
class COMPOSABLECAMERASYSTEMEDITOR_API SComposableCameraGraphNode: public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SComposableCameraGraphNode) {}
	SLATE_END_ARGS()

	virtual ~SComposableCameraGraphNode() override;

	void Construct(const FArguments& InArgs, UComposableCameraNodeGraphNode* InNode);

	/** Promote the transient hover card into one movable, persistent window. */
	void PinRuntimeDebugWindow();

	/** Close this node's persistent runtime-debug window, if any. */
	void ClosePinnedRuntimeDebugWindow();

	/** Whether this node currently owns a persistent runtime-debug window. */
	bool IsRuntimeDebugWindowPinned() const { return PinnedRuntimeDebugWindow.IsValid(); }

#if WITH_DEV_AUTOMATION_TESTS
	/** Test seam for the interactive-tooltip leave grace rule. */
	static bool ShouldCloseRuntimeDebugToolTipForTesting(
		bool bSourceHovered,
		bool bToolTipHovered,
		double SecondsSinceLastHover);

	/** Whether the last Pin operation moved the existing hover card. */
	bool DidLastPinReuseHoverCardForTesting() const { return bLastPinReusedHoverCard; }

	/** Whether the pinned window kept the captured physical screen position. */
	bool DidLastPinPreserveScreenPositionForTesting() const
	{
		return bLastPinPreservedScreenPosition;
	}
#endif

	// SGraphNode Interface

	virtual void UpdateGraphNode() override;

	// SWidget Interface

	virtual TSharedPtr<IToolTip> GetToolTip() override;
	virtual void OnToolTipClosing() override;
	virtual void Tick(const FGeometry& AllottedGeometry,
		double InCurrentTime,
		float InDeltaTime) override;

	virtual int32 OnPaint(const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

private:
	/** The backing graph node (typed accessor to avoid repeated casts). */
	UComposableCameraNodeGraphNode* CameraGraphNode = nullptr;

	/** Lazily-built dark runtime tooltip. Reset when hover closes so the next
	 *  opening reflects any parameter-list shape change. */
	TSharedPtr<SToolTip> RuntimeDebugToolTip;

	/** Independent debug observer created from the hover card's Pin action. */
	TSharedPtr<SWindow> PinnedRuntimeDebugWindow;

	/** Last time either the graph node or interactive card was under the cursor. */
	double LastRuntimeDebugHoverTime = 0.0;

	/** Test-visible record of whether Pin promoted the existing card widget. */
	bool bLastPinReusedHoverCard = false;

	/** Test-visible record that SWindow did not apply a second DPI transform. */
	bool bLastPinPreservedScreenPosition = false;

	/** Whether the debug state indicates this node was active last tick. */
	bool IsDebugActive() const;

	/** Build the styled runtime parameter card shown during PIE debugging. */
	TSharedRef<SToolTip> BuildRuntimeDebugToolTip();

	/** Build shared card content for transient tooltip or pinned window. */
	TSharedRef<SWidget> BuildRuntimeDebugCard(bool bShowPinAction);

	/** Handle the Pin button embedded in the transient hover card. */
	FReply HandlePinRuntimeDebugWindow();

	/** Interactive tooltips persist by default; apply CCS leave semantics. */
	static bool ShouldCloseRuntimeDebugToolTip(
		bool bSourceHovered,
		bool bToolTipHovered,
		double SecondsSinceLastHover);

	/** Release state after the user closes the pinned window title bar. */
	void HandlePinnedRuntimeDebugWindowClosed(const TSharedRef<SWindow>& ClosedWindow);

	/** Paint the debug footer below the node body. */
	void PaintDebugFooter(const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId) const;

	/** Paint a small warning / error icon at the top-right of the node header
	 * when the backing GraphNode has a non-empty `ErrorMsg`. Driven by the
	 * build messages pushed onto GraphNode via
	 * `UComposableCameraNodeGraph::ApplyBuildMessagesToGraphNodes`. Reads the
	 * fields fresh every frame, so fixing a validation issue removes the
	 * badge on the next `SyncToTypeAsset`. */
	void PaintValidationBadge(const FGeometry& AllottedGeometry,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId) const;
};
