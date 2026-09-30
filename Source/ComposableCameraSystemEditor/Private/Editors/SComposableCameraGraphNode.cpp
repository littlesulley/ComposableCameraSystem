// Copyright 2026 Sulley. All Rights Reserved.

#include "Editors/SComposableCameraGraphNode.h"
#include "Editors/ComposableCameraNodeGraphNode.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "ComposableCameraEditorStyle.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"

#include "SGraphPin.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SToolTip.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/AppStyle.h"
#include "Fonts/FontMeasure.h"
#include "Logging/TokenizedMessage.h"

#define LOCTEXT_NAMESPACE "SComposableCameraGraphNode"

namespace
{
constexpr double RuntimeDebugToolTipLeaveGraceSeconds = 0.12;
}

SComposableCameraGraphNode::~SComposableCameraGraphNode()
{
	ClosePinnedRuntimeDebugWindow();
}

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

TSharedPtr<IToolTip> SComposableCameraGraphNode::GetToolTip()
{
	if (!CameraGraphNode || !CameraGraphNode->DebugState.bHasRuntimeData)
	{
		RuntimeDebugToolTip.Reset();
		return SGraphNode::GetToolTip();
	}

	if (!RuntimeDebugToolTip.IsValid())
	{
		RuntimeDebugToolTip = BuildRuntimeDebugToolTip();
	}
	return RuntimeDebugToolTip;
}

void SComposableCameraGraphNode::OnToolTipClosing()
{
	RuntimeDebugToolTip.Reset();
	LastRuntimeDebugHoverTime = 0.0;
	SGraphNode::OnToolTipClosing();
}

void SComposableCameraGraphNode::Tick(const FGeometry& AllottedGeometry,
	double InCurrentTime,
	float InDeltaTime)
{
	SGraphNode::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (!RuntimeDebugToolTip.IsValid())
	{
		return;
	}

	const bool bSourceHovered = IsHovered();
	const bool bToolTipHovered = RuntimeDebugToolTip->AsWidget()->IsHovered();
	if (bSourceHovered || bToolTipHovered)
	{
		LastRuntimeDebugHoverTime = InCurrentTime;
		return;
	}

	if (LastRuntimeDebugHoverTime <= 0.0)
	{
		LastRuntimeDebugHoverTime = InCurrentTime;
		return;
	}

	if (ShouldCloseRuntimeDebugToolTip(
		bSourceHovered,
		bToolTipHovered,
		InCurrentTime - LastRuntimeDebugHoverTime))
	{
		if (FSlateApplication::IsInitialized() &&
			FSlateApplication::Get().FindWidgetWindow(RuntimeDebugToolTip->AsWidget()).IsValid())
		{
			FSlateApplication::Get().CloseToolTip();
		}
		RuntimeDebugToolTip.Reset();
		LastRuntimeDebugHoverTime = 0.0;
	}
}

bool SComposableCameraGraphNode::ShouldCloseRuntimeDebugToolTip(
	bool bSourceHovered,
	bool bToolTipHovered,
	double SecondsSinceLastHover)
{
	return !bSourceHovered && !bToolTipHovered &&
		SecondsSinceLastHover >= RuntimeDebugToolTipLeaveGraceSeconds;
}

#if WITH_DEV_AUTOMATION_TESTS
bool SComposableCameraGraphNode::ShouldCloseRuntimeDebugToolTipForTesting(
	bool bSourceHovered,
	bool bToolTipHovered,
	double SecondsSinceLastHover)
{
	return ShouldCloseRuntimeDebugToolTip(
		bSourceHovered,
		bToolTipHovered,
		SecondsSinceLastHover);
}
#endif

TSharedRef<SToolTip> SComposableCameraGraphNode::BuildRuntimeDebugToolTip()
{
	const TSharedRef<FComposableCameraEditorStyle> Style = FComposableCameraEditorStyle::Get();

	return SNew(SToolTip)
		.BorderImage(Style->GetBrush(TEXT("DebugTooltip.Background")))
		.TextMargin(FMargin(0.f))
		.IsInteractive(true)
		[
			BuildRuntimeDebugCard(true)
		];
}

TSharedRef<SWidget> SComposableCameraGraphNode::BuildRuntimeDebugCard(bool bShowPinAction)
{
	const TSharedRef<FComposableCameraEditorStyle> Style = FComposableCameraEditorStyle::Get();
	const TWeakObjectPtr<UComposableCameraNodeGraphNode> WeakNode(CameraGraphNode);
	const int32 ParameterCount = CameraGraphNode
		? CameraGraphNode->DebugState.ParameterDisplayValues.Num()
		: 0;

	FText NodeTitle = LOCTEXT("RuntimeTooltipUnknownNode", "Camera Node");
	FText Description = FText::GetEmpty();
	if (CameraGraphNode)
	{
		NodeTitle = CameraGraphNode->GetNodeTitle(ENodeTitleType::FullTitle);
		if (CameraGraphNode->NodeTemplate)
		{
			const UClass* NodeClass = CameraGraphNode->NodeTemplate->GetClass();
			const FString& ClassToolTip = NodeClass->GetMetaData(TEXT("ToolTip"));
			Description = ClassToolTip.IsEmpty()
				? NodeClass->GetDisplayNameText()
				: FText::FromString(ClassToolTip);
		}
	}

	const FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 11);
	const FSlateFontInfo SectionFont = FCoreStyle::GetDefaultFontStyle("Bold", 9);
	const FSlateFontInfo LabelFont = FCoreStyle::GetDefaultFontStyle("Regular", 9);
	const FSlateFontInfo ValueFont = FCoreStyle::GetDefaultFontStyle("Mono", 9);

	TSharedRef<SVerticalBox> ParameterRows = SNew(SVerticalBox);
	if (ParameterCount == 0)
	{
		ParameterRows->AddSlot()
		.AutoHeight()
		.Padding(4.f, 3.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("RuntimeTooltipNoParameters", "No editable parameters"))
			.Font(LabelFont)
			.ColorAndOpacity(FStyleColors::ForegroundHeader)
		];
	}
	else
	{
		for (int32 ParameterIndex = 0; ParameterIndex < ParameterCount; ++ParameterIndex)
		{
			const FName RowBrushName = (ParameterIndex % 2 == 0)
				? TEXT("DebugTooltip.Row")
				: TEXT("DebugTooltip.RowAlternate");

			ParameterRows->AddSlot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 3.f)
			[
				SNew(SBorder)
				.BorderImage(Style->GetBrush(RowBrushName))
				.Padding(FMargin(8.f, 5.f))
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Top)
					[
						SNew(SBox)
						.WidthOverride(180.f)
						[
							SNew(STextBlock)
							.Text_Lambda([WeakNode, ParameterIndex]()
							{
								const UComposableCameraNodeGraphNode* Node = WeakNode.Get();
								return Node && Node->DebugState.ParameterDisplayValues.IsValidIndex(ParameterIndex)
									? FText::FromString(Node->DebugState.ParameterDisplayValues[ParameterIndex].Key)
									: FText::GetEmpty();
							})
							.Font(LabelFont)
							.ColorAndOpacity(FStyleColors::Foreground)
						]
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.Padding(12.f, 0.f, 0.f, 0.f)
					.VAlign(VAlign_Top)
					[
						SNew(STextBlock)
						.Text_Lambda([WeakNode, ParameterIndex]()
						{
							const UComposableCameraNodeGraphNode* Node = WeakNode.Get();
							return Node && Node->DebugState.ParameterDisplayValues.IsValidIndex(ParameterIndex)
								? FText::FromString(Node->DebugState.ParameterDisplayValues[ParameterIndex].Value)
								: FText::GetEmpty();
						})
						.Font(ValueFont)
						.ColorAndOpacity(FStyleColors::AccentBlue)
						.AutoWrapText(true)
					]
				]
			];
		}
	}

	TSharedRef<SVerticalBox> Card = SNew(SVerticalBox);
	Card->AddSlot()
	.AutoHeight()
	[
		SNew(SBorder)
		.BorderImage(Style->GetBrush(TEXT("DebugTooltip.Header")))
		.Padding(FMargin(10.f, 8.f))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.f, 0.f, 9.f, 0.f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush(TEXT("WhiteBrush")))
				.BorderBackgroundColor(CameraGraphNode
					? CameraGraphNode->GetNodeTitleColor()
					: FComposableCameraEditorColors::CameraNodeTitle)
				.Padding(FMargin(2.f, 12.f))
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(NodeTitle)
				.Font(TitleFont)
				.ColorAndOpacity(FStyleColors::Foreground)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.f, 0.f, 8.f, 0.f)
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
				.ContentPadding(3.f)
				.ToolTipText(LOCTEXT("RuntimeTooltipPinAction", "Pin debug window"))
				.Visibility_Lambda([this, bShowPinAction]()
				{
					return bShowPinAction && !PinnedRuntimeDebugWindow.IsValid()
						? EVisibility::Visible
						: EVisibility::Collapsed;
				})
				.OnClicked(this, &SComposableCameraGraphNode::HandlePinRuntimeDebugWindow)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush(TEXT("Icons.Unpinned")))
					.ColorAndOpacity(FStyleColors::Foreground)
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBorder)
				.BorderImage_Lambda([WeakNode, Style]()
				{
					const UComposableCameraNodeGraphNode* Node = WeakNode.Get();
					return Style->GetBrush(Node && Node->DebugState.bHasRuntimeData && Node->DebugState.bIsActive
						? TEXT("DebugTooltip.ActiveBadge")
						: TEXT("DebugTooltip.InactiveBadge"));
				})
				.Padding(FMargin(8.f, 2.f))
				[
					SNew(STextBlock)
					.Text_Lambda([WeakNode]()
					{
						const UComposableCameraNodeGraphNode* Node = WeakNode.Get();
						if (!Node || !Node->DebugState.bHasRuntimeData)
						{
							return LOCTEXT("RuntimeTooltipNoData", "NO DATA");
						}
						return Node->DebugState.bIsActive
							? LOCTEXT("RuntimeTooltipActive", "ACTIVE")
							: LOCTEXT("RuntimeTooltipIdle", "IDLE");
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
					.ColorAndOpacity(FStyleColors::White)
				]
			]
		]
	];

	if (!Description.IsEmpty())
	{
		Card->AddSlot()
		.AutoHeight()
		.Padding(12.f, 10.f, 12.f, 4.f)
		[
			SNew(STextBlock)
			.Text(Description)
			.Font(LabelFont)
			.ColorAndOpacity(FStyleColors::ForegroundHeader)
			.AutoWrapText(true)
		];
	}

	if (CameraGraphNode && CameraGraphNode->bHasCompilerMessage && !CameraGraphNode->ErrorMsg.IsEmpty())
	{
		Card->AddSlot()
		.AutoHeight()
		.Padding(12.f, 6.f, 12.f, 2.f)
		[
			SNew(SBorder)
			.BorderImage(Style->GetBrush(TEXT("DebugTooltip.ErrorPanel")))
			.Padding(FMargin(8.f, 6.f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(CameraGraphNode->ErrorMsg))
				.Font(LabelFont)
				.ColorAndOpacity(FStyleColors::Error)
				.AutoWrapText(true)
			]
		];
	}

	Card->AddSlot()
	.AutoHeight()
	.Padding(12.f, 10.f, 12.f, 6.f)
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("RuntimeTooltipParametersHeader", "RUNTIME PARAMETERS"))
			.Font(SectionFont)
			.ColorAndOpacity(FStyleColors::AccentBlue)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(STextBlock)
			.Text(FText::AsNumber(ParameterCount))
			.Font(LabelFont)
			.ColorAndOpacity(FStyleColors::ForegroundHeader)
		]
	];

	Card->AddSlot()
	.AutoHeight()
	.Padding(12.f, 0.f, 12.f, 8.f)
	[
		SNew(SBox)
		.MaxDesiredHeight(360.f)
		[
			SNew(SScrollBox)
			.ConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible)

			+ SScrollBox::Slot()
			[
				ParameterRows
			]
		]
	];

	Card->AddSlot()
	.AutoHeight()
	.Padding(12.f, 0.f, 12.f, 10.f)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("RuntimeTooltipLiveHint", "Live values - scroll for more"))
		.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
		.ColorAndOpacity(FStyleColors::ForegroundHeader)
	];

	return SNew(SBox)
		.MinDesiredWidth(440.f)
		.MaxDesiredWidth(620.f)
		[
			Card
		];
}

FReply SComposableCameraGraphNode::HandlePinRuntimeDebugWindow()
{
	PinRuntimeDebugWindow();
	return FReply::Handled();
}

void SComposableCameraGraphNode::PinRuntimeDebugWindow()
{
	if (PinnedRuntimeDebugWindow.IsValid())
	{
		PinnedRuntimeDebugWindow->BringToFront(true);
		return;
	}

	if (!CameraGraphNode || !CameraGraphNode->DebugState.bHasRuntimeData || !FSlateApplication::IsInitialized())
	{
		return;
	}

	bLastPinReusedHoverCard = false;
	bLastPinPreservedScreenPosition = false;
	TSharedPtr<SWidget> PinnedCard;
	FVector2D WindowPosition = FSlateApplication::Get().GetCursorPos() + FVector2D(16.f, 16.f);
	if (RuntimeDebugToolTip.IsValid())
	{
		PinnedCard = RuntimeDebugToolTip->GetContentWidget();
		bLastPinReusedHoverCard = PinnedCard.IsValid();

		const TSharedPtr<SWindow> ToolTipWindow =
			FSlateApplication::Get().FindWidgetWindow(RuntimeDebugToolTip->AsWidget());
		if (ToolTipWindow.IsValid())
		{
			WindowPosition = ToolTipWindow->GetPositionInScreen();
		}

		// Detach the exact card before Slate hides its reusable tooltip window.
		RuntimeDebugToolTip->ResetContentWidget();
		if (ToolTipWindow.IsValid())
		{
			FSlateApplication::Get().CloseToolTip();
		}
		RuntimeDebugToolTip.Reset();
		LastRuntimeDebugHoverTime = 0.0;
	}

	if (!PinnedCard.IsValid())
	{
		PinnedCard = BuildRuntimeDebugCard(false);
	}

	const FText NodeTitle = CameraGraphNode->GetNodeTitle(ENodeTitleType::FullTitle);
	const FText WindowTitle = FText::Format(
		LOCTEXT("PinnedRuntimeDebugWindowTitle", "{0} - Runtime Debug"),
		NodeTitle);

	TSharedRef<SWindow> DebugWindow = SNew(SWindow)
		.Title(WindowTitle)
		.AutoCenter(EAutoCenter::None)
		.ScreenPosition(WindowPosition)
		.AdjustInitialSizeAndPositionForDPIScale(false)
		.SizingRule(ESizingRule::Autosized)
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		.FocusWhenFirstShown(false)
		[
			SNew(SBorder)
			.BorderImage(FComposableCameraEditorStyle::Get()->GetBrush(TEXT("DebugTooltip.Background")))
			.Padding(0.f)
			[
				PinnedCard.ToSharedRef()
			]
		];
	bLastPinPreservedScreenPosition = FVector2D(
		DebugWindow->GetInitialDesiredPositionInScreen()).Equals(WindowPosition, 0.5f);

	PinnedRuntimeDebugWindow = DebugWindow;
	DebugWindow->SetOnWindowClosed(FOnWindowClosed::CreateSP(
		this,
		&SComposableCameraGraphNode::HandlePinnedRuntimeDebugWindowClosed));

	const TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());
	if (ParentWindow.IsValid())
	{
		FSlateApplication::Get().AddWindowAsNativeChild(DebugWindow, ParentWindow.ToSharedRef());
	}
	else
	{
		FSlateApplication::Get().AddWindow(DebugWindow);
	}
}

void SComposableCameraGraphNode::ClosePinnedRuntimeDebugWindow()
{
	if (!PinnedRuntimeDebugWindow.IsValid())
	{
		return;
	}

	const TSharedPtr<SWindow> WindowToClose = MoveTemp(PinnedRuntimeDebugWindow);
	WindowToClose->SetOnWindowClosed(FOnWindowClosed());
	if (FSlateApplication::IsInitialized())
	{
		WindowToClose->RequestDestroyWindow();
	}
}

void SComposableCameraGraphNode::HandlePinnedRuntimeDebugWindowClosed(const TSharedRef<SWindow>& ClosedWindow)
{
	if (PinnedRuntimeDebugWindow == ClosedWindow)
	{
		PinnedRuntimeDebugWindow.Reset();
	}
}

int32 SComposableCameraGraphNode::OnPaint(const FPaintArgs& Args,
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

	// Paint an inline warning / error badge if the backing GraphNode has a
	// validation message. Reads the UEdGraphNode error fields fresh every
	// frame, so fixing the issue removes the badge on the next sync.
	if (CameraGraphNode && CameraGraphNode->bHasCompilerMessage)
	{
		PaintValidationBadge(AllottedGeometry, OutDrawElements, LayerId);
	}

	return LayerId;
}

void SComposableCameraGraphNode::PaintDebugFooter(const FGeometry& AllottedGeometry,
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
		Lines.Emplace(FString::Printf(TEXT("Pos (%.0f, %.0f, %.0f) Rot (P=%.1f, Y=%.1f) FOV %.0f"),
				Pose.Position.X, Pose.Position.Y, Pose.Position.Z,
				Pose.Rotation.Pitch, Pose.Rotation.Yaw,
				Pose.GetEffectiveFieldOfView()),
			FLinearColor(0.4f, 0.85f, 1.0f));
	}

	// One line per output pin value.
	for (const TPair<FName, FString>& PinValue: CameraGraphNode->DebugState.OutputPinDisplayValues)
	{
		Lines.Emplace(FString::Printf(TEXT("%s: %s"), *PinValue.Key.ToString(), *PinValue.Value),
			FLinearColor(0.7f, 1.0f, 0.5f));
	}

	if (Lines.Num() == 0)
	{
		return;
	}

	// Measure the widest line to determine the box width. The graph editor
	// applies a zoom transform that is baked into AllottedGeometry's
	// accumulated render transform. FSlateFontMeasure::Measure defaults to
	// scale 1.0, but the actual text rasterises at the accumulated scale -
	// font hinting / kerning at larger raster sizes can make the rendered
	// text wider than a nave linear scale of the 1x measurement. Measure
	// at the real render scale and convert back to local space so the box
	// is always wide enough.
	const TSharedRef<FSlateFontMeasure> FontMeasure =
		FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FScale2f ZoomScale = AllottedGeometry.GetAccumulatedRenderTransform().GetMatrix().GetScale();
	const float RenderScale = FMath::Max(FMath::Max(FMath::Abs(ZoomScale.GetVector().X), FMath::Abs(ZoomScale.GetVector().Y)), 1.0f);

	float MaxTextWidth = 0.0f;
	for (const TPair<FString, FLinearColor>& Line: Lines)
	{
		const FVector2D TextSize = FontMeasure->Measure(Line.Key, MonoFont, RenderScale);
		MaxTextWidth = FMath::Max(MaxTextWidth, static_cast<float>(TextSize.X) / RenderScale);
	}

	// Box width: at least as wide as the node, or wider if text requires it.
	const float BoxWidth = FMath::Max(static_cast<float>(NodeSize.X), MaxTextWidth + Padding * 2.0f);
	const float BoxHeight = (Lines.Num() * LineHeight) + (Padding * 2.0f);
	const float BoxY = static_cast<float>(NodeSize.Y) + 2.0f; // 2px gap below node.

	const FPaintGeometry BackgroundGeometry = AllottedGeometry.ToPaintGeometry(FVector2f(BoxWidth, BoxHeight),
		FSlateLayoutTransform(FVector2f(0.0f, BoxY)));

	FSlateDrawElement::MakeBox(OutDrawElements,
		LayerId,
		BackgroundGeometry,
		FAppStyle::GetBrush("Graph.Node.Body"),
		ESlateDrawEffect::None,
		FLinearColor(0.02f, 0.02f, 0.02f, 0.85f));

	// Draw each text line.
	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		const float TextY = BoxY + Padding + (i * LineHeight);

		const FPaintGeometry TextGeometry = AllottedGeometry.ToPaintGeometry(FVector2f(MaxTextWidth + Padding, LineHeight),
			FSlateLayoutTransform(FVector2f(Padding, TextY)));

		FSlateDrawElement::MakeText(OutDrawElements,
			LayerId + 1,
			TextGeometry,
			Lines[i].Key,
			MonoFont,
			ESlateDrawEffect::None,
			Lines[i].Value);
	}
}

void SComposableCameraGraphNode::PaintValidationBadge(const FGeometry& AllottedGeometry,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId) const
{
	if (!CameraGraphNode)
	{
		return;
	}

	// Severity->brush + tint. ErrorType stores a raw EMessageSeverity
	// integer; any value at or below Error (CriticalError also maps here) is
	// treated as an error, Warning is its own tier, and anything else (Info /
	// PerformanceWarning) uses the neutral info icon so authors still see
	// that the node has something the Build Messages tab wants to tell them.
	//
	// The no-suffix `Icons.{Error,Warning,Info}` brushes are monochrome
	// glyphs that always exist in every FAppStyle flavour we target; we tint
	// them at draw time with `FSlateDrawElement::MakeBox`'s InTint so the
	// badge colour tracks severity without depending on the `*WithColor`
	// brush variants (which come and go across UE versions).
	const FSlateBrush* BadgeBrush = nullptr;
	FLinearColor BadgeTint = FLinearColor::White;
	if (CameraGraphNode->ErrorType <= EMessageSeverity::Error)
	{
		BadgeBrush = FAppStyle::GetBrush("Icons.Error");
		BadgeTint = FLinearColor(1.0f, 0.2f, 0.2f);
	}
	else if (CameraGraphNode->ErrorType == EMessageSeverity::Warning)
	{
		BadgeBrush = FAppStyle::GetBrush("Icons.Warning");
		BadgeTint = FLinearColor(1.0f, 0.8f, 0.0f);
	}
	else
	{
		BadgeBrush = FAppStyle::GetBrush("Icons.Info");
		BadgeTint = FLinearColor(0.4f, 0.7f, 1.0f);
	}

	if (!BadgeBrush)
	{
		return;
	}

	// Anchor the badge to the top-right of the node with a small inset so it
	// overlaps the title bar border without spilling past the node's click
	// area. The node's own width comes from AllottedGeometry; we size the
	// badge in local space so SGraphPanel's zoom transform handles scaling
	// automatically.
	constexpr float BadgeSize = 16.0f;
	constexpr float Inset = 2.0f;
	const FVector2D NodeSize = AllottedGeometry.GetLocalSize();
	const FVector2f BadgePos(static_cast<float>(NodeSize.X) - BadgeSize - Inset,
		Inset);

	const FPaintGeometry BadgeGeometry = AllottedGeometry.ToPaintGeometry(FVector2f(BadgeSize, BadgeSize),
		FSlateLayoutTransform(BadgePos));

	FSlateDrawElement::MakeBox(OutDrawElements,
		LayerId + 10, // above content, below any debug footer overlays
		BadgeGeometry,
		BadgeBrush,
		ESlateDrawEffect::None,
		BadgeTint);
}

#undef LOCTEXT_NAMESPACE
