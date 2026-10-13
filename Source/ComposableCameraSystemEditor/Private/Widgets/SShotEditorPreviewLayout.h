// Copyright 2026 Sulley. All Rights Reserved.
#pragma once

#include "Layout/ArrangedChildren.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/ComposableCameraShotEditorLayoutState.h"

/** Full-height parameter pane on the left, native camera preview on the right. */
class SShotEditorPreviewLayout : public SSplitter
{
public:
	SLATE_BEGIN_ARGS(SShotEditorPreviewLayout) : _AuthoringFraction(.4f) {}
		SLATE_ARGUMENT(float, AuthoringFraction)
		SLATE_EVENT(SSplitter::FOnSlotResized, OnAuthoringFractionChanged)
		SLATE_NAMED_SLOT(FArguments, Authoring)
		SLATE_NAMED_SLOT(FArguments, Preview)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args)
	{
		AuthoringFraction = ComposableCameraSystem::ShotEditorLayout::ValidAuthoringFraction(Args._AuthoringFraction);
		OnAuthoringFractionChanged = Args._OnAuthoringFractionChanged;
		SSplitter::Construct(SSplitter::FArguments().Orientation(Orient_Horizontal)
			.PhysicalSplitterHandleSize(DividerSize)
			+ SSplitter::Slot().Value_Lambda([this]() { return AuthoringFraction; }).MinSize(420.f)
				.OnSlotResized_Lambda([this](float Value) { SetAuthoringFraction(Value); })[Args._Authoring.Widget]
			+ SSplitter::Slot().Value_Lambda([this]() { return 1.f - AuthoringFraction; }).MinSize(280.f)
				.OnSlotResized_Lambda([this](float Value) { SetAuthoringFraction(1.f - Value); })[Args._Preview.Widget]);
	}
protected:
	virtual void OnArrangeChildren(const FGeometry& Geometry, FArrangedChildren& Arranged) const override
	{
		const FVector2D Available = Geometry.GetLocalSize();
		if (Available.X <= DividerSize || Available.Y <= 0.0) return;
		SSplitter::OnArrangeChildren(Geometry, Arranged);
	}
private:
	float AuthoringFraction = .4f;
	SSplitter::FOnSlotResized OnAuthoringFractionChanged;
	void SetAuthoringFraction(float Value)
	{
		AuthoringFraction = FMath::Clamp(Value, .15f, .85f);
		OnAuthoringFractionChanged.ExecuteIfBound(AuthoringFraction);
	}
	static constexpr float DividerSize = 5.f;
};
