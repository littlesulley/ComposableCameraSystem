// Copyright 2026 Sulley. All Rights Reserved.
#pragma once

#include "Layout/ArrangedChildren.h"
#include "Widgets/Layout/SSplitter.h"

/** Full-height parameter pane on the left, native camera preview on the right. */
class SShotEditorPreviewLayout : public SSplitter
{
public:
	SLATE_BEGIN_ARGS(SShotEditorPreviewLayout) {}
		SLATE_NAMED_SLOT(FArguments, Authoring)
		SLATE_NAMED_SLOT(FArguments, Preview)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args)
	{
		SSplitter::Construct(SSplitter::FArguments().Orientation(Orient_Horizontal)
			.PhysicalSplitterHandleSize(DividerSize)
			+ SSplitter::Slot().Value(.4f).MinSize(420.f)[Args._Authoring.Widget]
			+ SSplitter::Slot().Value(.6f).MinSize(280.f)[Args._Preview.Widget]);
	}
protected:
	virtual void OnArrangeChildren(const FGeometry& Geometry, FArrangedChildren& Arranged) const override
	{
		const FVector2D Available = Geometry.GetLocalSize();
		if (Available.X <= DividerSize || Available.Y <= 0.0) return;
		SSplitter::OnArrangeChildren(Geometry, Arranged);
	}
private:
	static constexpr float DividerSize = 5.f;
};
