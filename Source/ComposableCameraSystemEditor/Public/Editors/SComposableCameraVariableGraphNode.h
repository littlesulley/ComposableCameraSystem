// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "SGraphNode.h"

class UComposableCameraVariableGraphNode;

/**
 * Custom SGraphNode for variable Get/Set graph nodes.
 *
 * When the debug ticker is active during PIE, this widget paints a footer
 * overlay below the node showing the variable's current runtime value,
 * and a green border around the node to indicate it's live.
 *
 * The overlay uses the same OnPaint approach as SComposableCameraGraphNode:
 * drawn outside the layout system so it never affects node width or
 * interaction hit-testing.
 */
class COMPOSABLECAMERASYSTEMEDITOR_API SComposableCameraVariableGraphNode : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SComposableCameraVariableGraphNode) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UComposableCameraVariableGraphNode* InNode);

	virtual void UpdateGraphNode() override;
	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

private:
	UComposableCameraVariableGraphNode* VariableGraphNode = nullptr;

	bool IsDebugActive() const;
	void PaintDebugFooter(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
};
