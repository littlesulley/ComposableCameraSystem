// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Input/Reply.h"
#include "Styling/SlateTypes.h"
#include "Toolkits/BaseToolkit.h"
#include "Widgets/Views/SListView.h"

class FComposableCameraMeshLayerEdMode;
class IDetailsView;
class SDockTab;
struct FPropertyChangedEvent;
class FSpawnTabArgs;
struct FComposableCameraMeshLayerDefinition;
enum class EComposableCameraMeshToolMode : uint8;

class FComposableCameraMeshLayerModeToolkit : public FModeToolkit
{
public:
	explicit FComposableCameraMeshLayerModeToolkit(FComposableCameraMeshLayerEdMode* InMode);
	virtual ~FComposableCameraMeshLayerModeToolkit() override;

	virtual FText GetBaseToolkitName() const override;
	virtual FName GetToolkitFName() const override;
	virtual FEdMode* GetEditorMode() const override;
	virtual TSharedPtr<SWidget> GetInlineContent() const override { return ToolkitContent; }
	virtual void Init(const TSharedPtr<IToolkitHost>& InitToolkitHost) override;
	void RefreshDocument();
	void RefreshSelectionDetails();

protected:
	virtual void RequestModeUITabs() override;

private:
	friend class FComposableCameraMeshSelectionDetailsRefreshTest;
	friend class FComposableCameraMeshToolPanelsTest;
	friend class FComposableCameraMeshToolSliderTest;
	using FLayerListItem = TSharedPtr<FGuid>;

	TSharedRef<SDockTab> SpawnPrimaryTab(const FSpawnTabArgs& Args);
	void HandlePrimaryTabClosed(TSharedRef<SDockTab> ClosedTab);

	void RefreshLayerItems();
	void ReleaseActiveLayerDetails();
	void RefreshActiveLayerDetails();
	void InitializeDetailsViews();
	TSharedRef<SWidget> BuildToolPanel();
	TSharedRef<SWidget> BuildDrawToolMenu();
	void HandleToolModeChanged(EComposableCameraMeshToolMode NewMode);
	const FComposableCameraMeshLayerDefinition* FindLayer(FLayerListItem Item) const;
	TSharedRef<ITableRow> GenerateLayerRow(
		FLayerListItem Item,
		const TSharedRef<STableViewBase>& OwnerTable);
	void HandleLayerSelectionChanged(FLayerListItem Item, ESelectInfo::Type SelectInfo);
	void HandleLayerEnabledChanged(ECheckBoxState NewState, FLayerListItem Item);

	FReply HandleAddLayerClicked();
	FReply HandleDeleteLayerClicked();
	FReply HandleMoveLayerClicked(int32 Direction);
	FReply HandleSaveClicked();
	FReply HandleDiscardClicked();
	bool CanDeleteActiveLayer() const;
	bool CanMoveActiveLayer(int32 Direction) const;

	FComposableCameraMeshLayerEdMode* Mode = nullptr;
	TSharedPtr<SWidget> ToolkitContent;
	TArray<FLayerListItem> LayerItems;
	TSharedPtr<SListView<FLayerListItem>> LayerListView;
	TSharedPtr<IDetailsView> LayerDetailsView;
	TSharedPtr<IDetailsView> ShapeDetailsView;
	TSharedPtr<IDetailsView> ToolDetailsView;
	FTSTicker::FDelegateHandle SelectionRefreshHandle;
};
