// Copyright Sulley. All rights reserved.

#include "Editors/SComposableCameraExposedPin.h"

void SComposableCameraExposedPin::Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
{
	SGraphPin::Construct(SGraphPin::FArguments(), InGraphPinObj);

	// Install a fixed-false hover attribute so SWidget::IsHovered() always
	// returns false for this widget - hiding it from SGraphPanel's wire-target
	// hover probe. When the user is dragging a live wire around the graph
	// looking for a drop target, the panel asks each pin "are you hovered?"
	// and picks the first that says yes; with a bound-false attribute we stay
	// invisible to that search and the panel refuses the drop.
	//
	// This is the correct mechanism for "permanently non-hoverable":
	// SWidget::IsHovered is non-virtual but reads from a TAttribute<bool> set
	// via SetHover, so an explicit SetHover override wins over the default
	// bIsHovered tracking that OnMouseEnter/OnMouseLeave would normally set.
	//
	// Visual hover styling (the "light up" effect when the cursor sits on top
	// of the pin) is the incidental casualty - we would rather the exposed
	// pin look inert than interactive-but-unusable.
	SetHover(TAttribute<bool>(false));
}

FSlateColor SComposableCameraExposedPin::GetPinColor() const
{
	// Muted grey. Alpha is kept at 1.0 so the icon stays fully opaque
	// (subtracting alpha tends to look like "the icon is broken" rather than
	// "the icon is disabled"). The value 0.4 is dark enough to read as inert
	// against the graph background while still being discernible from the
	// node's border on selected nodes.
	//
	// If the theme is ever overhauled to a light-mode editor, this constant
	// should be moved behind FAppStyle / a named style brush. For now it's
	// inline to keep the Item 3 change self-contained and to avoid adding a
	// style module for a single color.
	return FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f, 1.0f));
}

FReply SComposableCameraExposedPin::OnMouseButtonDown(const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	// Consume left-click-down on the pin widget so the default SGraphPin
	// drag-detection (which starts the wire-drop operation) never runs.
	// Returning FReply::Handled() without DetectDrag / CaptureMouse means the
	// click is absorbed and the user gets no wire even if they click-drag.
	//
	// Middle-click pans the graph and is routed through the panel, not the
	// pin, so we don't need to special-case it here.
	//
	// Right-click is explicitly passed through to the base class so the
	// containing SGraphNode's pin context menu dispatcher still receives the
	// event and can show "Unexpose Parameter" for this pin.
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		return FReply::Handled();
	}

	return SGraphPin::OnMouseButtonDown(MyGeometry, MouseEvent);
}
