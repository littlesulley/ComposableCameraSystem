// Copyright 2026 Sulley. All Rights Reserved.

#include "Widgets/SComposableCameraRuntimeDebugPanel.h"

#include "Cameras/ComposableCameraCameraBase.h"
#include "ComposableCameraEditorStyle.h"
#include "Editors/ComposableCameraNodeGraph.h"
#include "Editors/ComposableCameraNodeGraphNode.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"

#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SComposableCameraRuntimeDebugPanel"

namespace
{
constexpr double MembershipRefreshIntervalSeconds = 0.1;
constexpr float FocusTransitionSeconds = 1.25f;
constexpr float FocusContentTintStrength = 0.45f;
constexpr float FocusOverlayOpacity = 0.32f;
}

void SComposableCameraRuntimeDebugPanel::Construct(const FArguments& InArgs)
{
	NodeGraph = InArgs._NodeGraph;
	FocusAnimation = FCurveSequence(
		0.0f,
		FocusTransitionSeconds,
		ECurveEaseFunction::Linear);
	FocusAnimation.JumpToEnd();

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(6.f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 6.f)
			[
				SAssignNew(SearchBox, SSearchBox)
				.HintText(LOCTEXT("SearchHint", "Search active nodes..."))
				.DelayChangeNotificationsWhileTyping(false)
				.OnTextChanged(this, &SComposableCameraRuntimeDebugPanel::HandleSearchTextChanged)
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.f)
			[
				SNew(SOverlay)

				+ SOverlay::Slot()
				[
					SAssignNew(NodeListView, SListView<FNodeItem>)
					.ListItemsSource(&VisibleNodes)
					.SelectionMode(ESelectionMode::None)
					.OnGenerateRow(this, &SComposableCameraRuntimeDebugPanel::GenerateNodeRow)
					.OnItemsRebuilt(this, &SComposableCameraRuntimeDebugPanel::HandleItemsRebuilt)
				]

				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Padding(16.f)
				[
					SNew(STextBlock)
					.Text(this, &SComposableCameraRuntimeDebugPanel::GetEmptyStateText)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.ColorAndOpacity(FStyleColors::ForegroundHeader)
					.Justification(ETextJustify::Center)
					.AutoWrapText(true)
					.Visibility_Lambda([this]()
					{
						return VisibleNodes.IsEmpty()
							? EVisibility::HitTestInvisible
							: EVisibility::Collapsed;
					})
				]
			]
		]
	];

	RefreshVisibleNodes(true);
}

void SComposableCameraRuntimeDebugPanel::Tick(const FGeometry& AllottedGeometry,
	double InCurrentTime,
	float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (InCurrentTime - LastMembershipRefreshTime >= MembershipRefreshIntervalSeconds)
	{
		LastMembershipRefreshTime = InCurrentTime;
		RefreshVisibleNodes(false);
	}
}

void SComposableCameraRuntimeDebugPanel::Refresh()
{
	RefreshVisibleNodes(false);
}

bool SComposableCameraRuntimeDebugPanel::FocusNode(UComposableCameraNodeGraphNode* GraphNode)
{
	if (!GraphNode || !GraphNode->DebugState.bHasRuntimeData || !GraphNode->DebugState.bIsActive)
	{
		return false;
	}

	if (!SearchText.IsEmpty() && !PassesSearch(GraphNode))
	{
		SetSearchText(FText::GetEmpty());
	}
	RefreshVisibleNodes(true);

	const FNodeItem Item(GraphNode);
	if (!VisibleNodes.Contains(Item))
	{
		return false;
	}

	SetNodeExpanded(GraphNode, true);
	FocusedNode = Item;
	FocusAnimation.JumpToStart();
	FocusAnimation.Play(AsShared());

	if (NodeListView.IsValid())
	{
		NodeListView->RequestScrollIntoView(Item);
	}
	return true;
}

void SComposableCameraRuntimeDebugPanel::SetSearchText(const FText& InSearchText)
{
	if (SearchBox.IsValid() && !SearchBox->GetText().EqualTo(InSearchText))
	{
		SearchBox->SetText(InSearchText);
	}

	HandleSearchTextChanged(InSearchText);
}

void SComposableCameraRuntimeDebugPanel::SetNodeExpanded(
	UComposableCameraNodeGraphNode* GraphNode,
	bool bExpanded)
{
	if (!GraphNode)
	{
		return;
	}

	const FNodeItem Item(GraphNode);
	if (const TWeakPtr<SExpandableArea>* Area = NodeAreas.Find(Item))
	{
		if (const TSharedPtr<SExpandableArea> PinnedArea = Area->Pin())
		{
			if (PinnedArea->IsExpanded() != bExpanded)
			{
				// SetExpanded synchronously routes through HandleNodeExpansionChanged.
				PinnedArea->SetExpanded(bExpanded);
				return;
			}
		}
	}

	HandleNodeExpansionChanged(bExpanded, Item);
}

bool SComposableCameraRuntimeDebugPanel::IsNodeExpanded(
	const UComposableCameraNodeGraphNode* GraphNode) const
{
	return GraphNode && ExpandedNodes.Contains(
		FNodeItem(const_cast<UComposableCameraNodeGraphNode*>(GraphNode)));
}

bool SComposableCameraRuntimeDebugPanel::IsNodeFocusTransitionActive(
	const UComposableCameraNodeGraphNode* GraphNode) const
{
	return GraphNode && FocusedNode.Get() == GraphNode && FocusAnimation.IsPlaying();
}

int32 SComposableCameraRuntimeDebugPanel::GetSelectedNodeCount() const
{
	return NodeListView.IsValid() ? NodeListView->GetNumItemsSelected() : 0;
}

#if WITH_DEV_AUTOMATION_TESTS
bool SComposableCameraRuntimeDebugPanel::IsPostRebuildLayoutRefreshPendingForTesting() const
{
	return bRequestPostRebuildLayoutRefresh;
}

void SComposableCameraRuntimeDebugPanel::HandleItemsRebuiltForTesting()
{
	HandleItemsRebuilt();
}

FLinearColor SComposableCameraRuntimeDebugPanel::GetNodeFocusOverlayTintForTesting(
	const UComposableCameraNodeGraphNode* GraphNode) const
{
	return GraphNode
		? GetNodeFocusOverlayTint(FNodeItem(
			const_cast<UComposableCameraNodeGraphNode*>(GraphNode))).GetSpecifiedColor()
		: FLinearColor::Transparent;
}
#endif

void SComposableCameraRuntimeDebugPanel::RefreshVisibleNodes(bool bForceListRefresh)
{
	TArray<FNodeItem> NewVisibleNodes;
	TMap<FNodeItem, int32> NewParameterCounts;

	if (const UComposableCameraNodeGraph* Graph = NodeGraph.Get())
	{
		NewVisibleNodes.Reserve(Graph->Nodes.Num());
		for (UEdGraphNode* RawNode : Graph->Nodes)
		{
			UComposableCameraNodeGraphNode* CameraNode =
				Cast<UComposableCameraNodeGraphNode>(RawNode);
			if (!CameraNode || !CameraNode->DebugState.bHasRuntimeData ||
				!CameraNode->DebugState.bIsActive || !PassesSearch(CameraNode))
			{
				continue;
			}

			const FNodeItem Item(CameraNode);
			NewVisibleNodes.Add(Item);
			NewParameterCounts.Add(Item, CameraNode->DebugState.ParameterDisplayValues.Num());
			if (!KnownNodes.Contains(Item))
			{
				KnownNodes.Add(Item);
				ExpandedNodes.Add(Item);
			}
		}
	}

	NewVisibleNodes.Sort([](const FNodeItem& A, const FNodeItem& B)
	{
		const UComposableCameraNodeGraphNode* NodeA = A.Get();
		const UComposableCameraNodeGraphNode* NodeB = B.Get();
		return NodeA && NodeB ? NodeA->NodeIndex < NodeB->NodeIndex : NodeA != nullptr;
	});

	const bool bMembershipChanged = VisibleNodes != NewVisibleNodes;
	const bool bParameterShapeChanged = VisibleParameterCounts.OrderIndependentCompareEqual(
		NewParameterCounts) == false;
	if (bForceListRefresh || bMembershipChanged || bParameterShapeChanged)
	{
		VisibleNodes = MoveTemp(NewVisibleNodes);
		VisibleParameterCounts = MoveTemp(NewParameterCounts);
		RequestNodeListLayoutRefresh();
	}

	for (auto It = NodeAreas.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || !It.Value().IsValid())
		{
			It.RemoveCurrent();
		}
	}
	for (auto It = KnownNodes.CreateIterator(); It; ++It)
	{
		if (!(*It).IsValid())
		{
			It.RemoveCurrent();
		}
	}
	for (auto It = ExpandedNodes.CreateIterator(); It; ++It)
	{
		if (!(*It).IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

void SComposableCameraRuntimeDebugPanel::RequestNodeListLayoutRefresh()
{
	if (!NodeListView.IsValid())
	{
		return;
	}

	// Variable-height rows stabilize only after the first generation pass.
	// Membership, parameter shape, and expansion state all use this path.
	bRequestPostRebuildLayoutRefresh = !VisibleNodes.IsEmpty();
	NodeListView->RequestListRefresh();
}

void SComposableCameraRuntimeDebugPanel::HandleSearchTextChanged(const FText& InSearchText)
{
	SearchText = InSearchText.ToString();
	SearchText.TrimStartAndEndInline();
	RefreshVisibleNodes(true);
}

void SComposableCameraRuntimeDebugPanel::HandleItemsRebuilt()
{
	if (!bRequestPostRebuildLayoutRefresh || !NodeListView.IsValid())
	{
		return;
	}

	// One deferred layout pass reuses rows whose wrapped/expanded desired sizes
	// are now known. Clear first so this callback cannot create a refresh loop.
	bRequestPostRebuildLayoutRefresh = false;
	NodeListView->RequestListRefresh();
}

bool SComposableCameraRuntimeDebugPanel::PassesSearch(
	const UComposableCameraNodeGraphNode* GraphNode) const
{
	if (!GraphNode || SearchText.IsEmpty())
	{
		return GraphNode != nullptr;
	}

	if (GraphNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString().Contains(
		SearchText,
		ESearchCase::IgnoreCase))
	{
		return true;
	}
	if (GraphNode->NodeTemplate &&
		GraphNode->NodeTemplate->GetClass()->GetDisplayNameText().ToString().Contains(
			SearchText,
			ESearchCase::IgnoreCase))
	{
		return true;
	}
	for (const TPair<FString, FString>& Parameter : GraphNode->DebugState.ParameterDisplayValues)
	{
		if (Parameter.Key.Contains(SearchText, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

TSharedRef<ITableRow> SComposableCameraRuntimeDebugPanel::GenerateNodeRow(
	FNodeItem Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	const TSharedRef<FComposableCameraEditorStyle> Style = FComposableCameraEditorStyle::Get();
	TSharedRef<SExpandableArea> Area = SNew(SExpandableArea)
		.InitiallyCollapsed(!ExpandedNodes.Contains(Item))
		.AllowAnimatedTransition(false)
		.BorderImage(Style->GetBrush(TEXT("DebugTooltip.Header")))
		.BodyBorderImage(Style->GetBrush(TEXT("DebugTooltip.Background")))
		.HeaderPadding(FMargin(6.f, 5.f))
		.Padding(FMargin(0.f, 2.f, 0.f, 5.f))
		.OnAreaExpansionChanged(FOnBooleanValueChanged::CreateSP(
			this,
			&SComposableCameraRuntimeDebugPanel::HandleNodeExpansionChanged,
			Item))
		.HeaderContent()
		[
			BuildNodeHeader(Item)
		]
		.BodyContent()
		[
			BuildNodeBody(Item)
		];

	NodeAreas.Add(Item, Area);
	return SNew(STableRow<FNodeItem>, OwnerTable)
		.Padding(FMargin(0.f, 0.f, 0.f, 5.f))
		.ShowSelection(false)
		[
			SNew(SOverlay)

			+ SOverlay::Slot()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetNoBrush())
				.Padding(0.f)
				.ColorAndOpacity(this,
					&SComposableCameraRuntimeDebugPanel::GetNodeFocusTint,
					Item)
				[
					Area
				]
			]

			+ SOverlay::Slot()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush(TEXT("WhiteBrush")))
				.BorderBackgroundColor(this,
					&SComposableCameraRuntimeDebugPanel::GetNodeFocusOverlayTint,
					Item)
				.Visibility(EVisibility::HitTestInvisible)
			]
		];
}

TSharedRef<SWidget> SComposableCameraRuntimeDebugPanel::BuildNodeHeader(FNodeItem Item) const
{
	const UComposableCameraNodeGraphNode* Node = Item.Get();
	const FText NodeTitle = Node
		? Node->GetNodeTitle(ENodeTitleType::FullTitle)
		: LOCTEXT("UnknownNode", "Camera Node");
	const FLinearColor Accent = Node
		? Node->GetNodeTitleColor()
		: FComposableCameraEditorColors::CameraNodeTitle;

	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.f, 0.f, 7.f, 0.f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush(TEXT("WhiteBrush")))
			.BorderBackgroundColor(Accent)
			.Padding(FMargin(2.f, 10.f))
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(NodeTitle)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			.ColorAndOpacity(FStyleColors::Foreground)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBorder)
			.BorderImage(FComposableCameraEditorStyle::Get()->GetBrush(
				TEXT("DebugTooltip.ActiveBadge")))
			.Padding(FMargin(7.f, 2.f))
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ActiveBadge", "ACTIVE"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 7))
				.ColorAndOpacity(FStyleColors::White)
			]
		];
}

TSharedRef<SWidget> SComposableCameraRuntimeDebugPanel::BuildNodeBody(FNodeItem Item) const
{
	const TSharedRef<FComposableCameraEditorStyle> Style = FComposableCameraEditorStyle::Get();
	const UComposableCameraNodeGraphNode* Node = Item.Get();
	const int32 ParameterCount = Node
		? Node->DebugState.ParameterDisplayValues.Num()
		: 0;
	const FSlateFontInfo LabelFont = FCoreStyle::GetDefaultFontStyle("Regular", 8);
	const FSlateFontInfo ValueFont = FCoreStyle::GetDefaultFontStyle("Mono", 8);

	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
	Body->AddSlot()
	.AutoHeight()
	.Padding(8.f, 5.f, 8.f, 7.f)
	[
		SNew(STextBlock)
		.Text_Lambda([Item]()
		{
			const UComposableCameraNodeGraphNode* LiveNode = Item.Get();
			if (!LiveNode || !LiveNode->DebugState.bHasRuntimeData)
			{
				return LOCTEXT("NoRuntimeData", "No runtime data");
			}
			const FComposableCameraPose& Pose = LiveNode->DebugState.PoseAfterNode;
			return FText::FromString(FString::Printf(
				TEXT("Pos (%.0f, %.0f, %.0f)  Rot (P=%.1f, Y=%.1f, R=%.1f)  FOV %.1f"),
				Pose.Position.X,
				Pose.Position.Y,
				Pose.Position.Z,
				Pose.Rotation.Pitch,
				Pose.Rotation.Yaw,
				Pose.Rotation.Roll,
				Pose.GetEffectiveFieldOfView()));
		})
		.Font(ValueFont)
		.ColorAndOpacity(FStyleColors::ForegroundHeader)
		.AutoWrapText(true)
	];

	if (ParameterCount == 0)
	{
		Body->AddSlot()
		.AutoHeight()
		.Padding(8.f, 0.f, 8.f, 6.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoParameters", "No editable parameters"))
			.Font(LabelFont)
			.ColorAndOpacity(FStyleColors::ForegroundHeader)
		];
	}
	else
	{
		for (int32 ParameterIndex = 0; ParameterIndex < ParameterCount; ++ParameterIndex)
		{
			const FName BrushName = ParameterIndex % 2 == 0
				? TEXT("DebugTooltip.Row")
				: TEXT("DebugTooltip.RowAlternate");
			Body->AddSlot()
			.AutoHeight()
			.Padding(6.f, 0.f, 6.f, 2.f)
			[
				SNew(SBorder)
				.BorderImage(Style->GetBrush(BrushName))
				.Padding(FMargin(6.f, 4.f))
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.FillWidth(0.45f)
					.VAlign(VAlign_Top)
					[
						SNew(STextBlock)
						.Text_Lambda([Item, ParameterIndex]()
						{
							const UComposableCameraNodeGraphNode* LiveNode = Item.Get();
							return LiveNode && LiveNode->DebugState.ParameterDisplayValues.IsValidIndex(ParameterIndex)
								? FText::FromString(LiveNode->DebugState.ParameterDisplayValues[ParameterIndex].Key)
								: FText::GetEmpty();
						})
						.Font(LabelFont)
						.ColorAndOpacity(FStyleColors::Foreground)
						.AutoWrapText(true)
					]

					+ SHorizontalBox::Slot()
					.FillWidth(0.55f)
					.Padding(8.f, 0.f, 0.f, 0.f)
					.VAlign(VAlign_Top)
					[
						SNew(STextBlock)
						.Text_Lambda([Item, ParameterIndex]()
						{
							const UComposableCameraNodeGraphNode* LiveNode = Item.Get();
							return LiveNode && LiveNode->DebugState.ParameterDisplayValues.IsValidIndex(ParameterIndex)
								? FText::FromString(LiveNode->DebugState.ParameterDisplayValues[ParameterIndex].Value)
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

	return Body;
}

void SComposableCameraRuntimeDebugPanel::HandleNodeExpansionChanged(
	bool bExpanded,
	FNodeItem Item)
{
	const bool bWasExpanded = ExpandedNodes.Contains(Item);
	if (bExpanded)
	{
		ExpandedNodes.Add(Item);
	}
	else
	{
		ExpandedNodes.Remove(Item);
	}

	if (bWasExpanded != bExpanded && VisibleNodes.Contains(Item))
	{
		// SListView caches generated variable-height rows. Rebuild after the
		// body changes size so scroll range and lower-row virtualization agree.
		RequestNodeListLayoutRefresh();
	}
}

FLinearColor SComposableCameraRuntimeDebugPanel::GetNodeFocusTint(FNodeItem Item) const
{
	if (FocusedNode != Item || !FocusAnimation.IsPlaying())
	{
		return FLinearColor::White;
	}

	const UComposableCameraNodeGraphNode* Node = Item.Get();
	FLinearColor Accent = Node
		? Node->GetNodeTitleColor()
		: FComposableCameraEditorColors::CameraNodeTitle;
	Accent.A = 1.0f;
	const FLinearColor Highlight = FMath::Lerp(
		FLinearColor::White,
		Accent,
		FocusContentTintStrength);
	return FMath::Lerp(Highlight, FLinearColor::White, FocusAnimation.GetLerp());
}

FSlateColor SComposableCameraRuntimeDebugPanel::GetNodeFocusOverlayTint(
	FNodeItem Item) const
{
	if (FocusedNode != Item || !FocusAnimation.IsPlaying())
	{
		return FSlateColor(FLinearColor::Transparent);
	}

	const UComposableCameraNodeGraphNode* Node = Item.Get();
	FLinearColor Accent = Node
		? Node->GetNodeTitleColor()
		: FComposableCameraEditorColors::CameraNodeTitle;
	Accent.A = FocusOverlayOpacity * (1.0f - FocusAnimation.GetLerp());
	return FSlateColor(Accent);
}

FText SComposableCameraRuntimeDebugPanel::GetEmptyStateText() const
{
	return SearchText.IsEmpty()
		? LOCTEXT("NoActiveNodes", "No active runtime nodes.\nStart PIE and select a debug camera.")
		: LOCTEXT("NoSearchMatches", "No active nodes match this search.");
}

#undef LOCTEXT_NAMESPACE
