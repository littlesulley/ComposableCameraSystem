// Copyright 2026 Sulley. All Rights Reserved.

#include "MeshCamera/ComposableCameraMeshLayerModeToolkit.h"

#include "Editor.h"
#include "MeshCamera/ComposableCameraMeshLayerEdMode.h"
#include "MeshCamera/ComposableCameraMeshLayerToolSettings.h"
#include "IDetailsView.h"
#include "IStructureDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Styling/AppStyle.h"
#include "Toolkits/AssetEditorModeUILayer.h"
#include "UObject/StructOnScope.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "ComposableCameraMeshLayerModeToolkit"

FComposableCameraMeshLayerModeToolkit::FComposableCameraMeshLayerModeToolkit(
	FComposableCameraMeshLayerEdMode* InMode)
	: Mode(InMode)
{
}

FText FComposableCameraMeshLayerModeToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("ToolkitName", "Mesh Camera Layers");
}

FName FComposableCameraMeshLayerModeToolkit::GetToolkitFName() const
{
	return TEXT("ComposableCameraMeshLayerToolkit");
}

FEdMode* FComposableCameraMeshLayerModeToolkit::GetEditorMode() const
{
	return Mode;
}

void FComposableCameraMeshLayerModeToolkit::Init(
	const TSharedPtr<IToolkitHost>& InitToolkitHost)
{
	FPropertyEditorModule& PropertyEditorModule =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));

	FDetailsViewArgs PaintDetailsArgs;
	PaintDetailsArgs.bAllowSearch = false;
	PaintDetailsArgs.bHideSelectionTip = true;
	PaintDetailsArgs.bShowOptions = false;
	PaintDetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	const TSharedRef<IDetailsView> PaintDetailsView =
		PropertyEditorModule.CreateDetailView(PaintDetailsArgs);
	PaintDetailsView->SetObject(Mode ? Mode->GetSettings() : nullptr);

	FDetailsViewArgs LayerDetailsArgs;
	LayerDetailsArgs.bAllowSearch = false;
	LayerDetailsArgs.bHideSelectionTip = true;
	LayerDetailsArgs.bShowOptions = false;
	LayerDetailsArgs.bShowScrollBar = false;
	LayerDetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	FStructureDetailsViewArgs StructureDetailsArgs;
	LayerDetailsView = PropertyEditorModule.CreateStructureDetailView(
		LayerDetailsArgs,
		StructureDetailsArgs,
		nullptr);
	LayerDetailsView->GetOnFinishedChangingPropertiesDelegate().AddSP(
		SharedThis(this),
		&FComposableCameraMeshLayerModeToolkit::HandleLayerPropertyChanged);

	RefreshLayerItems();

	ToolkitContent =
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(8.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(LOCTEXT(
					"Instructions",
					"Select a Layer row, then paint with left mouse. Shift + left mouse temporarily erases. Higher rows display above lower rows."))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("LayersHeading", "Layers"))
					.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("AddLayer", "Add"))
					.ToolTipText(LOCTEXT("AddLayerTooltip", "Add and select a new mesh camera Layer."))
					.OnClicked(this, &FComposableCameraMeshLayerModeToolkit::HandleAddLayerClicked)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("DeleteLayer", "Delete"))
					.IsEnabled(this, &FComposableCameraMeshLayerModeToolkit::CanDeleteActiveLayer)
					.OnClicked(this, &FComposableCameraMeshLayerModeToolkit::HandleDeleteLayerClicked)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("MoveLayerUp", "Up"))
					.IsEnabled(this, &FComposableCameraMeshLayerModeToolkit::CanMoveActiveLayer, -1)
					.OnClicked(this, &FComposableCameraMeshLayerModeToolkit::HandleMoveLayerClicked, -1)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("MoveLayerDown", "Down"))
					.IsEnabled(this, &FComposableCameraMeshLayerModeToolkit::CanMoveActiveLayer, 1)
					.OnClicked(this, &FComposableCameraMeshLayerModeToolkit::HandleMoveLayerClicked, 1)
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBox)
				.MinDesiredHeight(72.0f)
				.MaxDesiredHeight(180.0f)
				[
					SAssignNew(LayerListView, SListView<FLayerListItem>)
					.ListItemsSource(&LayerItems)
					.SelectionMode(ESelectionMode::Single)
					.OnGenerateRow(this, &FComposableCameraMeshLayerModeToolkit::GenerateLayerRow)
					.OnSelectionChanged(this, &FComposableCameraMeshLayerModeToolkit::HandleLayerSelectionChanged)
				]
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("SelectedLayerHeading", "Selected Layer"))
						.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						LayerDetailsView->GetWidget().ToSharedRef()
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 4.0f)
					[
						SNew(SSeparator)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("PaintHeading", "Brush"))
						.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						PaintDetailsView
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 6.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text_Lambda([this]()
				{
					return Mode ? Mode->GetStatusText() : FText::GetEmpty();
				})
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SButton)
				.Text(LOCTEXT("Save", "Save Mesh Layers"))
				.IsEnabled_Lambda([this]() { return Mode && Mode->IsDirty(); })
				.OnClicked(this, &FComposableCameraMeshLayerModeToolkit::HandleSaveClicked)
			]
		];

	FModeToolkit::Init(InitToolkitHost);
	RefreshLayerItems();
}

void FComposableCameraMeshLayerModeToolkit::RequestModeUITabs()
{
	FModeToolkit::RequestModeUITabs();
	if (ModeUILayer.IsValid())
	{
		PrimaryTabInfo.OnSpawnTab = FOnSpawnTab::CreateSP(
			SharedThis(this),
			&FComposableCameraMeshLayerModeToolkit::SpawnPrimaryTab);
		ModeUILayer.Pin()->SetModePanelInfo(
			UAssetEditorUISubsystem::TopLeftTabID,
			PrimaryTabInfo);
	}
}

TSharedRef<SDockTab> FComposableCameraMeshLayerModeToolkit::SpawnPrimaryTab(
	const FSpawnTabArgs& Args)
{
	TSharedRef<SDockTab> Tab = FModeToolkit::CreatePrimaryModePanel(Args);
	Tab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateSP(
		SharedThis(this),
		&FComposableCameraMeshLayerModeToolkit::HandlePrimaryTabClosed));
	return Tab;
}

void FComposableCameraMeshLayerModeToolkit::HandlePrimaryTabClosed(
	TSharedRef<SDockTab> /*ClosedTab*/)
{
	if (Mode)
	{
		Mode->RequestCloseFromToolkit();
	}
}

void FComposableCameraMeshLayerModeToolkit::RefreshLayerItems()
{
	LayerItems.Reset();
	UComposableCameraMeshLayerToolSettings* Settings = Mode ? Mode->GetSettings() : nullptr;
	if (Settings)
	{
		LayerItems.Reserve(Settings->Layers.Num());
		for (const FComposableCameraMeshLayerDefinition& Layer : Settings->Layers)
		{
			LayerItems.Add(MakeShared<FGuid>(Layer.LayerId));
		}
	}

	if (LayerListView.IsValid())
	{
		LayerListView->ClearSelection();
		LayerListView->RequestListRefresh();
		if (Settings)
		{
			const FGuid ActiveLayerId = Settings->GetActiveLayerId();
			if (const FLayerListItem* ActiveItem = LayerItems.FindByPredicate(
				[ActiveLayerId](const FLayerListItem& Item)
				{
					return Item.IsValid() && *Item == ActiveLayerId;
				}))
			{
				LayerListView->SetSelection(*ActiveItem, ESelectInfo::Direct);
			}
		}
	}

	RefreshActiveLayerDetails();
}

void FComposableCameraMeshLayerModeToolkit::ReleaseActiveLayerDetails()
{
	if (LayerDetailsView.IsValid())
	{
		LayerDetailsView->SetStructureData(nullptr);
	}
	ActiveLayerScope.Reset();
}

void FComposableCameraMeshLayerModeToolkit::RefreshActiveLayerDetails()
{
	ReleaseActiveLayerDetails();
	UComposableCameraMeshLayerToolSettings* Settings = Mode ? Mode->GetSettings() : nullptr;
	if (!LayerDetailsView.IsValid() || !Settings
		|| !Settings->Layers.IsValidIndex(Settings->ActiveLayerIndex))
	{
		return;
	}

	FComposableCameraMeshLayerDefinition& Layer = Settings->Layers[Settings->ActiveLayerIndex];
	ActiveLayerScope = MakeShared<FStructOnScope>(
		FComposableCameraMeshLayerDefinition::StaticStruct(),
		reinterpret_cast<uint8*>(&Layer));
	LayerDetailsView->SetStructureData(ActiveLayerScope);
}

const FComposableCameraMeshLayerDefinition*
FComposableCameraMeshLayerModeToolkit::FindLayer(FLayerListItem Item) const
{
	const UComposableCameraMeshLayerToolSettings* Settings = Mode ? Mode->GetSettings() : nullptr;
	return Settings && Item.IsValid()
		? Settings->Layers.FindByPredicate(
			[LayerId = *Item](const FComposableCameraMeshLayerDefinition& Layer)
			{
				return Layer.LayerId == LayerId;
			})
		: nullptr;
}

TSharedRef<ITableRow> FComposableCameraMeshLayerModeToolkit::GenerateLayerRow(
	FLayerListItem Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	const FComposableCameraMeshLayerDefinition* Layer = FindLayer(Item);
	const FLinearColor LayerColor = Layer ? Layer->DebugColor : FLinearColor::Transparent;
	const FText LayerName = Layer ? FText::FromName(Layer->Name) : FText::GetEmpty();
	const ECheckBoxState EnabledState = Layer && Layer->bEnabled
		? ECheckBoxState::Checked
		: ECheckBoxState::Unchecked;

	return SNew(STableRow<FLayerListItem>, OwnerTable)
		.Padding(FMargin(4.0f, 2.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SColorBlock)
				.Color(LayerColor)
				.Size(FVector2D(14.0f, 14.0f))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SCheckBox)
				.IsChecked(EnabledState)
				.ToolTipText(LOCTEXT("LayerEnabledTooltip", "Enable this Layer for preview and runtime queries."))
				.OnCheckStateChanged(
					this,
					&FComposableCameraMeshLayerModeToolkit::HandleLayerEnabledChanged,
					Item)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LayerName)
			]
		];
}

void FComposableCameraMeshLayerModeToolkit::HandleLayerSelectionChanged(
	FLayerListItem Item,
	ESelectInfo::Type /*SelectInfo*/)
{
	UComposableCameraMeshLayerToolSettings* Settings = Mode ? Mode->GetSettings() : nullptr;
	if (Settings && Item.IsValid() && Settings->SelectLayer(*Item))
	{
		RefreshActiveLayerDetails();
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports();
		}
	}
}

void FComposableCameraMeshLayerModeToolkit::HandleLayerEnabledChanged(
	ECheckBoxState NewState,
	FLayerListItem Item)
{
	UComposableCameraMeshLayerToolSettings* Settings = Mode ? Mode->GetSettings() : nullptr;
	if (!Settings || !Item.IsValid())
	{
		return;
	}

	if (FComposableCameraMeshLayerDefinition* Layer = Settings->Layers.FindByPredicate(
		[LayerId = *Item](const FComposableCameraMeshLayerDefinition& Candidate)
		{
			return Candidate.LayerId == LayerId;
		}))
	{
		Layer->bEnabled = NewState == ECheckBoxState::Checked;
		Settings->NotifyLayerDataChanged();
		if (LayerListView.IsValid())
		{
			LayerListView->RequestListRefresh();
		}
	}
}

void FComposableCameraMeshLayerModeToolkit::HandleLayerPropertyChanged(
	const FPropertyChangedEvent& /*PropertyChangedEvent*/)
{
	if (UComposableCameraMeshLayerToolSettings* Settings = Mode ? Mode->GetSettings() : nullptr)
	{
		Settings->NotifyLayerDataChanged();
		if (LayerListView.IsValid())
		{
			LayerListView->RequestListRefresh();
		}
	}
}

FReply FComposableCameraMeshLayerModeToolkit::HandleAddLayerClicked()
{
	ReleaseActiveLayerDetails();
	if (UComposableCameraMeshLayerToolSettings* Settings = Mode ? Mode->GetSettings() : nullptr)
	{
		Settings->AddLayer();
		RefreshLayerItems();
	}
	return FReply::Handled();
}

FReply FComposableCameraMeshLayerModeToolkit::HandleDeleteLayerClicked()
{
	ReleaseActiveLayerDetails();
	if (UComposableCameraMeshLayerToolSettings* Settings = Mode ? Mode->GetSettings() : nullptr)
	{
		Settings->RemoveActiveLayer();
		RefreshLayerItems();
	}
	return FReply::Handled();
}

FReply FComposableCameraMeshLayerModeToolkit::HandleMoveLayerClicked(int32 Direction)
{
	ReleaseActiveLayerDetails();
	if (UComposableCameraMeshLayerToolSettings* Settings = Mode ? Mode->GetSettings() : nullptr)
	{
		Settings->MoveActiveLayer(Direction);
		RefreshLayerItems();
	}
	return FReply::Handled();
}

FReply FComposableCameraMeshLayerModeToolkit::HandleSaveClicked()
{
	if (Mode)
	{
		Mode->SaveWorkingData();
	}
	return FReply::Handled();
}

bool FComposableCameraMeshLayerModeToolkit::CanDeleteActiveLayer() const
{
	const UComposableCameraMeshLayerToolSettings* Settings = Mode ? Mode->GetSettings() : nullptr;
	return Settings && Settings->Layers.Num() > 1
		&& Settings->Layers.IsValidIndex(Settings->ActiveLayerIndex);
}

bool FComposableCameraMeshLayerModeToolkit::CanMoveActiveLayer(int32 Direction) const
{
	const UComposableCameraMeshLayerToolSettings* Settings = Mode ? Mode->GetSettings() : nullptr;
	return Settings && Settings->Layers.IsValidIndex(Settings->ActiveLayerIndex)
		&& Settings->Layers.IsValidIndex(Settings->ActiveLayerIndex + FMath::Sign(Direction));
}

#undef LOCTEXT_NAMESPACE
