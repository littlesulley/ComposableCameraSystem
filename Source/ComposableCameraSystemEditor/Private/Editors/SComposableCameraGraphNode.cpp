// Copyright Sulley. All rights reserved.

#include "Editors/SComposableCameraGraphNode.h"
#include "Editors/ComposableCameraNodeGraphNode.h"
#include "Cameras/ComposableCameraCameraBase.h"

#include "SGraphPin.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/AppStyle.h"
#include "Fonts/FontMeasure.h"

#define LOCTEXT_NAMESPACE "SComposableCameraGraphNode"

void SComposableCameraGraphNode::Construct(const FArguments& InArgs, UComposableCameraNodeGraphNode* InNode)
{
	CameraGraphNode = InNode;
	GraphNode = InNode;
	SetCursor(EMouseCursor::CardinalCross);
	UpdateGraphNode();
}

void SComposableCameraGraphNode::UpdateGraphNode()
{
	// Let the base class build all default content (title, pins, comments, etc.).
	SGraphNode::UpdateGraphNode();
}

bool SComposableCameraGraphNode::IsDebugActive() const
{
	return CameraGraphNode && CameraGraphNode->DebugState.bIsActive;
}

int32 SComposableCameraGraphNode::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	// Paint the standard node first.
	LayerId = SGraphNode::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	// Paint debug data footer if this node was ticked this frame.
	if (IsDebugActive())
	{
		PaintDebugFooter(AllottedGeometry, MyCullingRect, OutDrawElements, LayerId);
	}

	return LayerId;
}

void SComposableCameraGraphNode::PaintDebugFooter(
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId) const
{
	if (!CameraGraphNode)
	{
		return;
	}

	const FSlateFontInfo MonoFont = FCoreStyle::GetDefaultFontStyle("Mono", 7);
	const float LineHeight = 12.0f;
	const float Padding = 4.0f;
	const FVector2D NodeSize = AllottedGeometry.GetLocalSize();

	// Collect lines to draw.
	TArray<TPair<FString, FLinearColor>> Lines;

	// Pose line.
	{
		const FComposableCameraPose& Pose = CameraGraphNode->DebugState.PoseAfterNode;
		Lines.Emplace(
			FString::Printf(
				TEXT("Pos (%.0f, %.0f, %.0f)  Rot (P=%.1f, Y=%.1f)  FOV %.0f"),
				Pose.Position.X, Pose.Position.Y, Pose.Position.Z,
				Pose.Rotation.Pitch, Pose.Rotation.Yaw,
				Pose.GetEffectiveFieldOfView()),
			FLinearColor(0.4f, 0.85f, 1.0f));
	}

	// One line per output pin value.
	for (const TPair<FName, FString>& PinValue : CameraGraphNode->DebugState.OutputPinDisplayValues)
	{
		Lines.Emplace(
			FString::Printf(TEXT("%s: %s"), *PinValue.Key.ToString(), *PinValue.Value),
			FLinearColor(0.7f, 1.0f, 0.5f));
	}

	if (Lines.Num() == 0)
	{
		return;
	}

	// Measure the widest line to determine the box width. The graph editor
	// applies a zoom transform that is baked into AllottedGeometry's
	// accumulated render transform. FSlateFontMeasure::Measure defaults to
	// scale 1.0, but the actual text rasterises at the accumulated scale —
	// font hinting / kerning at larger raster sizes can make the rendered
	// text wider than a naïve linear scale of the 1× measurement. Measure
	// at the real render scale and convert back to local space so the box
	// is always wide enough.
	const TSharedRef<FSlateFontMeasure> FontMeasure =
		FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FScale2f ZoomScale = AllottedGeometry.GetAccumulatedRenderTransform().GetMatrix().GetScale();
	const float RenderScale = FMath::Max(FMath::Max(FMath::Abs(ZoomScale.GetVector().X), FMath::Abs(ZoomScale.GetVector().Y)), 1.0f);

	float MaxTextWidth = 0.0f;
	for (const TPair<FString, FLinearColor>& Line : Lines)
	{
		const FVector2D TextSize = FontMeasure->Measure(Line.Key, MonoFont, RenderScale);
		MaxTextWidth = FMath::Max(MaxTextWidth, static_cast<float>(TextSize.X) / RenderScale);
	}

	// Box width: at least as wide as the node, or wider if text requires it.
	const float BoxWidth = FMath::Max(static_cast<float>(NodeSize.X), MaxTextWidth + Padding * 2.0f);
	const float BoxHeight = (Lines.Num() * LineHeight) + (Padding * 2.0f);
	const float BoxY = static_cast<float>(NodeSize.Y) + 2.0f; // 2px gap below node.

	const FPaintGeometry BackgroundGeometry = AllottedGeometry.ToPaintGeometry(
		FVector2f(BoxWidth, BoxHeight),
		FSlateLayoutTransform(FVector2f(0.0f, BoxY)));

	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId,
		BackgroundGeometry,
		FAppStyle::GetBrush("Graph.Node.Body"),
		ESlateDrawEffect::None,
		FLinearColor(0.02f, 0.02f, 0.02f, 0.85f));

	// Draw each text line.
	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		const float TextY = BoxY + Padding + (i * LineHeight);

		const FPaintGeometry TextGeometry = AllottedGeometry.ToPaintGeometry(
			FVector2f(MaxTextWidth + Padding, LineHeight),
			FSlateLayoutTransform(FVector2f(Padding, TextY)));

		FSlateDrawElement::MakeText(
			OutDrawElements,
			LayerId + 1,
			TextGeometry,
			Lines[i].Key,
			MonoFont,
			ESlateDrawEffect::None,
			Lines[i].Value);
	}
}

#undef LOCTEXT_NAMESPACE
