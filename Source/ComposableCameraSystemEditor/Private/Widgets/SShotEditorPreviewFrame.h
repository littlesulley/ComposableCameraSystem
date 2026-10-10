// Copyright 2026 Sulley. All Rights Reserved.
#pragma once

#include "Brushes/SlateColorBrush.h"
#include "Layout/ArrangedChildren.h"
#include "Rendering/DrawElements.h"
#include "Styling/WidgetStyle.h"
#include "Widgets/Layout/SBox.h"

/** Camera-aspect image inside a narrow screen bezel, aligned to the right. */
class SShotEditorPreviewFrame : public SBox
{
public:
	static constexpr float BezelThickness = 6.f;

	SLATE_BEGIN_ARGS(SShotEditorPreviewFrame) : _AspectRatio(16.f / 9.f)
	{
		_Visibility = EVisibility::SelfHitTestInvisible;
	}
		SLATE_ATTRIBUTE(FOptionalSize, AspectRatio)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()
	void Construct(const FArguments& Args)
	{
		PaintChildren.Reserve(1);
		// Aspect fitting applies to the actual image after reserving bezel space.
		// Keep Fill alignment: a native viewport can request zero desired size.
		SBox::Construct(SBox::FArguments().Padding(BezelThickness)
			.MinAspectRatio(Args._AspectRatio).MaxAspectRatio(Args._AspectRatio)[Args._Content.Widget]);
	}
protected:
	virtual void OnArrangeChildren(const FGeometry& Geometry, FArrangedChildren& Children) const override
	{
		const FVector2D Available = Geometry.GetLocalSize();
		if (Available.X <= BezelThickness * 2.f || Available.Y <= BezelThickness * 2.f) return;
		const int32 FirstChild = Children.Num();
		SBox::OnArrangeChildren(Geometry, Children);
		if (Children.Num() == FirstChild + 1)
		{
			const FVector2D Size = Children[FirstChild].Geometry.GetLocalSize();
			const FVector2D Position(FMath::Max(double(BezelThickness), Available.X - BezelThickness - Size.X),
				FMath::Max(double(BezelThickness), (Available.Y - Size.Y) * .5));
			Children[FirstChild] = Geometry.MakeChild(Children[FirstChild].Widget, Position, Size);
		}
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
		FSlateWindowElementList& DrawElements, int32 LayerId, const FWidgetStyle& WidgetStyle, bool bParentEnabled) const override
	{
		// Reuse one reserved slot; derive the bezel from current image geometry.
		PaintChildren.Empty();
		ArrangeChildren(Geometry, PaintChildren);
		if (PaintChildren.Num() == 1)
		{
			const FGeometry& Image = PaintChildren[0].Geometry;
			const FVector2D Size = Image.GetLocalSize();
			const FVector2D Origin = Geometry.AbsoluteToLocal(Image.LocalToAbsolute(FVector2D::ZeroVector));
			const ESlateDrawEffect Effects = ShouldBeEnabled(bParentEnabled) ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
			const auto PaintRim = [&](float Outset, int32 Layer, const FLinearColor& Color)
			{
				const FVector2f Offset(Origin - FVector2D(Outset, Outset));
				const FVector2f RimSize(Size + FVector2D(Outset * 2.f, Outset * 2.f));
				FSlateDrawElement::MakeBox(DrawElements, Layer,
					Geometry.ToPaintGeometry(RimSize, FSlateLayoutTransform(Offset)), &BezelBrush, Effects,
					Color * WidgetStyle.GetColorAndOpacityTint());
			};
			PaintRim(BezelThickness, LayerId, FLinearColor(.18f, .20f, .22f));
			PaintRim(BezelThickness - 1.f, LayerId + 1, FLinearColor(.025f, .03f, .035f));
			PaintRim(1.f, LayerId + 2, FLinearColor(.10f, .12f, .13f));
		}
		// Native viewport, overlays and input geometry remain inside the clear image.
		return SBox::OnPaint(Args, Geometry, CullingRect, DrawElements, LayerId + 3, WidgetStyle, bParentEnabled);
	}
private:
	FSlateColorBrush BezelBrush { FLinearColor::White };
	mutable FArrangedChildren PaintChildren { EVisibility::Visible };
};
