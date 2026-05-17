// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SGraphPin.h"

/**
 * SGraphPin subclass used for input pins on UComposableCameraNodeGraphNode
 * whose corresponding C++ pin declaration is currently *exposed as a camera
 * parameter*. The widget differs from the default SGraphPin in two ways:
 *
 * 1. Visual: the pin connector icon is rendered in a muted grey rather than
 * the type-driven color. This is the signal that the pin is not driven
 * by any upstream wire - its value comes from the caller via the exposed
 * parameter block, not from anything in this graph. The existing
 * "{DisplayName} (Exposed)" label on the pin text remains intact and is
 * the primary textual cue; the grey color is the peripheral visual cue.
 *
 * 2. Interaction: the pin is fully non-interactive as a wire source or
 * target. Left-click-drag cannot start a wire, and any attempt to drop
 * another wire on it is refused. This is enforced on top of the schema's
 * ArePinsCompatible check (which already refuses wires to/from exposed
 * pins) so the user gets immediate widget-level feedback rather than
 * seeing a hover indication and then having the drop rejected.
 *
 * Right-click still passes through to the containing SGraphNode, which
 * routes to UComposableCameraNodeGraphSchema::BuildPinContextMenuActions
 * - so the "Unexpose Parameter" context menu remains reachable on the
 * exposed pin itself, which is the only place it makes sense to live.
 *
 * The widget is instantiated by FComposableCameraNodeGraphPinFactory, which
 * inspects the owning UComposableCameraNodeGraphNode's IsInputPinExposed
 * query each time the graph rebuilds pin widgets. Toggling expose / unexpose
 * on a pin triggers UComposableCameraNodeGraphNode::ReconstructPins, which
 * drops and re-creates the SGraphPin widgets, giving the factory a fresh
 * chance to dispatch between this subclass and the default SGraphPin.
 */
class COMPOSABLECAMERASYSTEMEDITOR_API SComposableCameraExposedPin: public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SComposableCameraExposedPin) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj);

	// SGraphPin 

	/** Grey-tinted color for the pin connector icon. Returning the same color
	 * for connected and disconnected variants is fine because exposed pins
	 * are, by construction, never wired - PinConnections for the target pin
	 * are removed when ExposePinAsParameter is called. */
	virtual FSlateColor GetPinColor() const override;

	/** Swallow left-mouse events so SGraphPanel's drag-detection never gets a
	 * chance to promote a click on this pin into a wire-drag. Right-click
	 * still propagates up to the containing SGraphNode so the pin context
	 * menu (Unexpose Parameter) remains reachable.
	 *
	 * Note: SWidget::IsHovered is non-virtual in UE5 so we cannot override it
	 * to force hover-reporting off. Instead, Construct() installs a fixed
	 * `false` hover attribute via SWidget::SetHover, which achieves the same
	 * effect - SGraphPanel's wire-target probe walks pin widgets via the
	 * shared SWidget::IsHovered() accessor, which respects the SetHover
	 * attribute when one is bound. */
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override;
};
