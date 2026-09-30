// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "Animation/CurveSequence.h"
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class SExpandableArea;
class SSearchBox;
class UComposableCameraNodeGraph;
class UComposableCameraNodeGraphNode;

/**
 * Dockable overview of active runtime camera nodes for the Camera Type editor.
 *
 * Reads only transient graph-node debug copies. It never retains or follows a
 * runtime camera/node pointer. Active membership is refreshed independently
 * from value attributes, so live values do not rebuild the Slate list.
 */
class COMPOSABLECAMERASYSTEMEDITOR_API SComposableCameraRuntimeDebugPanel:
	public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SComposableCameraRuntimeDebugPanel) {}
		SLATE_ARGUMENT(UComposableCameraNodeGraph*, NodeGraph)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual void Tick(const FGeometry& AllottedGeometry,
		double InCurrentTime,
		float InDeltaTime) override;

	/** Immediately refresh active/filter membership. */
	void Refresh();

	/** Reveal and expand one active node. Clears search if it hides the node. */
	bool FocusNode(UComposableCameraNodeGraphNode* GraphNode);

	/** Set the same filter text exposed by the search box. */
	void SetSearchText(const FText& InSearchText);
	void SetNodeExpanded(UComposableCameraNodeGraphNode* GraphNode, bool bExpanded);

	int32 GetVisibleNodeCount() const { return VisibleNodes.Num(); }
	bool IsNodeExpanded(const UComposableCameraNodeGraphNode* GraphNode) const;
	bool IsNodeFocusTransitionActive(const UComposableCameraNodeGraphNode* GraphNode) const;
	int32 GetSelectedNodeCount() const;

#if WITH_DEV_AUTOMATION_TESTS
	bool IsPostRebuildLayoutRefreshPendingForTesting() const;
	void HandleItemsRebuiltForTesting();
	FLinearColor GetNodeFocusOverlayTintForTesting(
		const UComposableCameraNodeGraphNode* GraphNode) const;
#endif

private:
	using FNodeItem = TWeakObjectPtr<UComposableCameraNodeGraphNode>;

	void RefreshVisibleNodes(bool bForceListRefresh);
	void RequestNodeListLayoutRefresh();
	void HandleSearchTextChanged(const FText& InSearchText);
	void HandleItemsRebuilt();
	bool PassesSearch(const UComposableCameraNodeGraphNode* GraphNode) const;

	TSharedRef<ITableRow> GenerateNodeRow(
		FNodeItem Item,
		const TSharedRef<STableViewBase>& OwnerTable);
	TSharedRef<SWidget> BuildNodeHeader(FNodeItem Item) const;
	TSharedRef<SWidget> BuildNodeBody(FNodeItem Item) const;
	void HandleNodeExpansionChanged(bool bExpanded, FNodeItem Item);
	FLinearColor GetNodeFocusTint(FNodeItem Item) const;
	FSlateColor GetNodeFocusOverlayTint(FNodeItem Item) const;
	FText GetEmptyStateText() const;

	TWeakObjectPtr<UComposableCameraNodeGraph> NodeGraph;
	TArray<FNodeItem> VisibleNodes;
	TMap<FNodeItem, int32> VisibleParameterCounts;
	TSet<FNodeItem> ExpandedNodes;
	TSet<FNodeItem> KnownNodes;
	TMap<FNodeItem, TWeakPtr<SExpandableArea>> NodeAreas;

	TSharedPtr<SSearchBox> SearchBox;
	TSharedPtr<SListView<FNodeItem>> NodeListView;
	FNodeItem FocusedNode;
	FCurveSequence FocusAnimation;
	FString SearchText;
	double LastMembershipRefreshTime = 0.0;
	bool bRequestPostRebuildLayoutRefresh = false;
};
