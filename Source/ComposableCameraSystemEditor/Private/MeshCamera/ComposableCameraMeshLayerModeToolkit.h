// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Styling/SlateTypes.h"
#include "Toolkits/BaseToolkit.h"
#include "Widgets/Views/SListView.h"

class FComposableCameraMeshLayerEdMode;
class FStructOnScope;
class IStructureDetailsView;
class SDockTab;
struct FPropertyChangedEvent;
class FSpawnTabArgs;
struct FComposableCameraMeshLayerDefinition;

class FComposableCameraMeshLayerModeToolkit : public FModeToolkit
{
public:
	explicit FComposableCameraMeshLayerModeToolkit(FComposableCameraMeshLayerEdMode* InMode);

	virtual FText GetBaseToolkitName() const override;
	virtual FName GetToolkitFName() const override;
	virtual FEdMode* GetEditorMode() const override;
	virtual TSharedPtr<SWidget> GetInlineContent() const override { return ToolkitContent; }
	virtual void Init(const TSharedPtr<IToolkitHost>& InitToolkitHost) override;

protected:
	virtual void RequestModeUITabs() override;

private:
	using FLayerListItem = TSharedPtr<FGuid>;

	TSharedRef<SDockTab> SpawnPrimaryTab(const FSpawnTabArgs& Args);
	void HandlePrimaryTabClosed(TSharedRef<SDockTab> ClosedTab);

	void RefreshLayerItems();
	void ReleaseActiveLayerDetails();
	void RefreshActiveLayerDetails();
	const FComposableCameraMeshLayerDefinition* FindLayer(FLayerListItem Item) const;
	TSharedRef<ITableRow> GenerateLayerRow(
		FLayerListItem Item,
		const TSharedRef<STableViewBase>& OwnerTable);
	void HandleLayerSelectionChanged(FLayerListItem Item, ESelectInfo::Type SelectInfo);
	void HandleLayerEnabledChanged(ECheckBoxState NewState, FLayerListItem Item);
	void HandleLayerPropertyChanged(const FPropertyChangedEvent& PropertyChangedEvent);

	FReply HandleAddLayerClicked();
	FReply HandleDeleteLayerClicked();
	FReply HandleMoveLayerClicked(int32 Direction);
	FReply HandleSaveClicked();
	bool CanDeleteActiveLayer() const;
	bool CanMoveActiveLayer(int32 Direction) const;

	FComposableCameraMeshLayerEdMode* Mode = nullptr;
	TSharedPtr<SWidget> ToolkitContent;
	TArray<FLayerListItem> LayerItems;
	TSharedPtr<SListView<FLayerListItem>> LayerListView;
	TSharedPtr<IStructureDetailsView> LayerDetailsView;
	TSharedPtr<FStructOnScope> ActiveLayerScope;
};
