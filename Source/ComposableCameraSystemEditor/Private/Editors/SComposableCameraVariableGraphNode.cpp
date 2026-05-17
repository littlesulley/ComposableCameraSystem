// Copyright 2026 Sulley. All Rights Reserved.

#include "Editors/SComposableCameraVariableGraphNode.h"
#include "Editors/ComposableCameraVariableGraphNode.h"

#include "Styling/AppStyle.h"
#include "Fonts/FontMeasure.h"

#define LOCTEXT_NAMESPACE "SComposableCameraVariableGraphNode"

void SComposableCameraVariableGraphNode::Construct(const FArguments& InArgs, UComposableCameraVariableGraphNode* InNode)
{
	VariableGraphNode = InNode;
	GraphNode = InNode;
	SetCursor(EMouseCursor::CardinalCross);
	UpdateGraphNode();
}

void SComposableCameraVariableGraphNode::UpdateGraphNode()
{
	SGraphNode::UpdateGraphNode();
}

bool SComposableCameraVariableGraphNode::IsDebugActive() const
{
	return VariableGraphNode && VariableGraphNode->DebugState.bIsActive;
}

int32 SComposableCameraVariableGraphNode::OnPaint(const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	LayerId = SGraphNode::OnPaint(Args, AllottedGeometry, MyCullingRect,
		OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	if (IsDebugActive())
	{
		PaintDebugFooter(AllottedGeometry, MyCullingRect, OutDrawElements, LayerId);
	}

	return LayerId;
}

void SComposableCameraVariableGraphNode::PaintDebugFooter(const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId) const
{
	if (!VariableGraphNode || VariableGraphNode->DebugState.DisplayValue.IsEmpty())
	{
		return;
	}

	const FSlateFontInfo MonoFont = FCoreStyle::GetDefaultFontStyle("Mono", 7);
	const float LineHeight = 12.0f;
	const float Padding = 4.0f;
	const FVector2D NodeSize = AllottedGeometry.GetLocalSize();

	// Single line: "VariableName: Value"
	const FString DisplayText = FString::Printf(TEXT("%s: %s"),
		*VariableGraphNode->VariableName.ToString(),
		*VariableGraphNode->DebugState.DisplayValue);

	// Measure text width for adaptive box sizing. Measure at the actual
	// render scale (which includes graph zoom) so font hinting / kerning at
	// the rasterised size is accounted for, then convert back to local space.
	const TSharedRef<FSlateFontMeasure> FontMeasure =
		FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FScale2f ZoomScale = AllottedGeometry.GetAccumulatedRenderTransform().GetMatrix().GetScale();
	const float RenderScale = FMath::Max(FMath::Max(FMath::Abs(ZoomScale.GetVector().X), FMath::Abs(ZoomScale.GetVector().Y)), 1.0f);
	const FVector2D TextSize = FontMeasure->Measure(DisplayText, MonoFont, RenderScale);
	const float LocalTextWidth = static_cast<float>(TextSize.X) / RenderScale;

	const float BoxWidth = FMath::Max(static_cast<float>(NodeSize.X),
		LocalTextWidth + Padding * 2.0f);
	const float BoxHeight = LineHeight + Padding * 2.0f;
	const float BoxY = static_cast<float>(NodeSize.Y) + 2.0f;

	// Background box.
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
		AllottedGeometry.ToPaintGeometry(FVector2f(BoxWidth, BoxHeight),
			FSlateLayoutTransform(FVector2f(0.0f, BoxY))),
		FAppStyle::GetBrush("Graph.Node.Body"),
		ESlateDrawEffect::None,
		FLinearColor(0.02f, 0.02f, 0.02f, 0.85f));

	// Value text - yellow-green for variables.
	FSlateDrawElement::MakeText(OutDrawElements, LayerId + 1,
		AllottedGeometry.ToPaintGeometry(FVector2f(static_cast<float>(TextSize.X) + Padding, LineHeight),
			FSlateLayoutTransform(FVector2f(Padding, BoxY + Padding))),
		DisplayText,
		MonoFont,
		ESlateDrawEffect::None,
		FLinearColor(0.85f, 0.9f, 0.3f));
}

#undef LOCTEXT_NAMESPACE
