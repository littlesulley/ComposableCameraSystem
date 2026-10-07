// Copyright 2026 Sulley. All Rights Reserved.

#include "MeshCamera/ComposableCameraMeshLayerModeToolkit.h"

#include "Editor.h"
#include "MeshCamera/ComposableCameraMeshLayerEdMode.h"
#include "MeshCamera/ComposableCameraMeshLayerToolSettings.h"
#include "IDetailsView.h"
#include "IDetailCustomization.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailPropertyRow.h"
#include "PropertyHandle.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ScopedTransaction.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "UObject/UnrealType.h"
#include "Styling/AppStyle.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Toolkits/AssetEditorModeUILayer.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "ComposableCameraMeshLayerModeToolkit"

namespace
{
	constexpr float MeshActionWidth = 125.0f;
	constexpr float MeshActionHeight = 24.0f;
	const FButtonStyle& GetMeshSaveButtonStyle()
	{
		static const FButtonStyle Style = []()
		{
			FButtonStyle Result = FAppStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
			Result.SetNormal(FSlateRoundedBoxBrush(FLinearColor(0.055f, 0.28f, 0.09f), 5.0f));
			Result.SetHovered(FSlateRoundedBoxBrush(FLinearColor(0.075f, 0.38f, 0.12f), 5.0f));
			Result.SetPressed(FSlateRoundedBoxBrush(FLinearColor(0.035f, 0.20f, 0.06f), 5.0f));
			Result.SetDisabled(FSlateRoundedBoxBrush(FLinearColor(0.045f, 0.12f, 0.055f), 5.0f));
			Result.SetNormalPadding(FMargin(0.0f)).SetPressedPadding(FMargin(0.0f));
			return Result;
		}();
		return Style;
	}
	const FButtonStyle& GetMeshDiscardButtonStyle()
	{
		static const FButtonStyle Style = []()
		{
			FButtonStyle Result = FAppStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
			Result.SetNormal(FSlateRoundedBoxBrush(FLinearColor(0.12f, 0.12f, 0.12f), 5.0f));
			Result.SetHovered(FSlateRoundedBoxBrush(FLinearColor(0.18f, 0.18f, 0.18f), 5.0f));
			Result.SetPressed(FSlateRoundedBoxBrush(FLinearColor(0.08f, 0.08f, 0.08f), 5.0f));
			Result.SetDisabled(FSlateRoundedBoxBrush(FLinearColor(0.065f, 0.065f, 0.065f), 5.0f));
			Result.SetNormalPadding(FMargin(0.0f)).SetPressedPadding(FMargin(0.0f));
			return Result;
		}();
		return Style;
	}
	class FMeshToolDetailsCustomization : public IDetailCustomization
	{
	public:
		FMeshToolDetailsCustomization(TWeakObjectPtr<UComposableCameraMeshLayerToolSettings> InSettings,
			TWeakPtr<FComposableCameraMeshLayerModeToolkit> InToolkit) : Settings(InSettings), Toolkit(InToolkit) {}
		virtual void CustomizeDetails(IDetailLayoutBuilder& Builder) override
		{
			FText Heading = LOCTEXT("DrawOptions", "Draw Options");
			if (const auto* Pinned = Settings.Get())
			{
				if (Pinned->GetToolMode() == EComposableCameraMeshToolMode::Select) { Heading = LOCTEXT("SelectOptions", "Select Options"); }
				else if (Pinned->GetToolMode() == EComposableCameraMeshToolMode::Erase) { Heading = LOCTEXT("EraseOptions", "Erase Options"); }
			}
			IDetailCategoryBuilder& Category = Builder.EditCategory(TEXT("Drawing"), Heading);
			if (Settings.IsValid() && Settings->GetToolMode() == EComposableCameraMeshToolMode::Select)
			{
				const TWeakPtr<FComposableCameraMeshLayerModeToolkit> WeakToolkit = Toolkit;
				Category.AddCustomRow(LOCTEXT("DeleteShape", "Delete Selected Shape"))
				.NameContent().MinDesiredWidth(MeshActionWidth).MaxDesiredWidth(MeshActionWidth).HAlign(HAlign_Left)
				[
					SNew(SBox).WidthOverride(MeshActionWidth).HeightOverride(MeshActionHeight)
					[
					SNew(SButton)
					.HAlign(HAlign_Center).VAlign(VAlign_Center)
					.ContentPadding(FMargin(4.0f, 0.0f))
					.Text(LOCTEXT("DeleteShape", "Delete Selected Shape"))
					.ToolTipText(LOCTEXT("DeleteShapeTooltip", "Delete the selected Shape and its coverage (Delete).\nCtrl+Z restores it; Ctrl+Y reapplies deletion."))
					.IsEnabled_Lambda([WeakToolkit]()
					{
						const auto Pinned = WeakToolkit.Pin();
						const auto* Mode = Pinned ? static_cast<FComposableCameraMeshLayerEdMode*>(Pinned->GetEditorMode()) : nullptr;
						return Mode && Mode->HasSelectedShape();
					})
					.OnClicked_Lambda([WeakToolkit]()
					{
						const auto Pinned = WeakToolkit.Pin();
						auto* Mode = Pinned ? static_cast<FComposableCameraMeshLayerEdMode*>(Pinned->GetEditorMode()) : nullptr;
						if (Mode) { Mode->DeleteSelectedShape(); }
						return FReply::Handled();
					})
					]
				];
				const auto GridProperty = Builder.GetProperty(GET_MEMBER_NAME_CHECKED(UComposableCameraMeshLayerToolSettings, ShapeGridSize));
				FDetailWidgetRow& GridRow = Category.AddProperty(GridProperty).CustomWidget();
				GridRow.NameContent()[GridProperty->CreatePropertyNameWidget()];
				GridRow.ValueContent().MinDesiredWidth(MeshActionWidth).MaxDesiredWidth(MeshActionWidth).HAlign(HAlign_Left)
				[
					SNew(SBox).WidthOverride(MeshActionWidth).HeightOverride(MeshActionHeight)
					[GridProperty->CreatePropertyValueWidget()]
				];
			}
		}
	private:
		TWeakObjectPtr<UComposableCameraMeshLayerToolSettings> Settings;
		TWeakPtr<FComposableCameraMeshLayerModeToolkit> Toolkit;
	};

	FName GetRootPropertyName(const FPropertyAndParent& Property)
	{
		return Property.ParentProperties.IsEmpty()
			? Property.Property.GetFName() : Property.ParentProperties.Last()->GetFName();
	}
}

FComposableCameraMeshLayerModeToolkit::FComposableCameraMeshLayerModeToolkit(
	FComposableCameraMeshLayerEdMode* InMode)
	: Mode(InMode)
{
}

FComposableCameraMeshLayerModeToolkit::~FComposableCameraMeshLayerModeToolkit()
{
	if (SelectionRefreshHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(SelectionRefreshHandle);
	}
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
	InitializeDetailsViews();
	RefreshLayerItems();

	ToolkitContent =
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(8.0f)
		[
			SNew(SVerticalBox)
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
					.ToolTipText(LOCTEXT("LayersTooltip", "Select a Layer row to draw or edit its coverage.\nHigher rows display above lower rows."))
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
					.ToolTipText(LOCTEXT("DeleteLayerTooltip", "Delete the selected Layer and its coverage. Ctrl+Z restores it."))
					.IsEnabled(this, &FComposableCameraMeshLayerModeToolkit::CanDeleteActiveLayer)
					.OnClicked(this, &FComposableCameraMeshLayerModeToolkit::HandleDeleteLayerClicked)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("MoveLayerUp", "Up"))
					.ToolTipText(LOCTEXT("MoveLayerUpTooltip", "Move the selected Layer above the previous row. Higher rows display on top."))
					.IsEnabled(this, &FComposableCameraMeshLayerModeToolkit::CanMoveActiveLayer, -1)
					.OnClicked(this, &FComposableCameraMeshLayerModeToolkit::HandleMoveLayerClicked, -1)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("MoveLayerDown", "Down"))
					.ToolTipText(LOCTEXT("MoveLayerDownTooltip", "Move the selected Layer below the next row. Higher rows display on top."))
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
						LayerDetailsView.ToSharedRef()
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
						BuildToolPanel()
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 6.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Visibility_Lambda([this]() { return Mode && !Mode->GetStatusText().IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; })
				.ToolTipText_Lambda([this]() { return Mode ? Mode->GetDocumentInfoText() : FText::GetEmpty(); })
				.Text_Lambda([this]()
				{
					return Mode ? Mode->GetStatusText() : FText::GetEmpty();
				})
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Left)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox).WidthOverride(MeshActionWidth).HeightOverride(MeshActionHeight)
					[
						SNew(SButton)
						.ButtonStyle(&GetMeshSaveButtonStyle())
						.ForegroundColor(FLinearColor::White)
						.HAlign(HAlign_Center).VAlign(VAlign_Center)
						.ContentPadding(FMargin(4.0f, 0.0f))
						.Text(LOCTEXT("Save", "Save"))
						.ToolTipText_Lambda([this]()
						{
							return FText::Format(LOCTEXT("SaveTooltip", "Save mesh camera Layers and coverage to the current Level.\n\n{0}"),
								Mode ? Mode->GetDocumentInfoText() : FText::GetEmpty());
						})
						.IsEnabled_Lambda([this]() { return Mode && Mode->IsDirty() && !Mode->IsCreatingShapes(); })
						.OnClicked(this, &FComposableCameraMeshLayerModeToolkit::HandleSaveClicked)
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(8.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SBox).WidthOverride(MeshActionWidth).HeightOverride(MeshActionHeight)
					[
						SNew(SButton)
						.ButtonStyle(&GetMeshDiscardButtonStyle())
						.ForegroundColor(FLinearColor::White)
						.HAlign(HAlign_Center).VAlign(VAlign_Center)
						.ContentPadding(FMargin(4.0f, 0.0f))
						.Text(LOCTEXT("Discard", "Discard"))
						.ToolTipText(LOCTEXT("DiscardTooltip", "Restore mesh camera Layers and coverage to the last successful Save, or the opening document if never saved. Cancel unfinished drawing. Ctrl+Z undoes Discard."))
						.IsEnabled_Lambda([this]() { return Mode && Mode->CanDiscardWorkingData(); })
						.OnClicked(this, &FComposableCameraMeshLayerModeToolkit::HandleDiscardClicked)
					]
				]
			]
		];

	FModeToolkit::Init(InitToolkitHost);
	GetToolkitCommands()->MapAction(FGenericCommands::Get().Undo, FExecuteAction::CreateLambda([this]()
	{
		if (Mode) { Mode->CancelInteraction(); }
		if (GEditor) { GEditor->UndoTransaction(); }
	}));
	GetToolkitCommands()->MapAction(FGenericCommands::Get().Redo, FExecuteAction::CreateLambda([this]()
	{
		if (Mode) { Mode->CancelInteraction(); }
		if (GEditor) { GEditor->RedoTransaction(); }
	}));
	RefreshLayerItems();
}

void FComposableCameraMeshLayerModeToolkit::InitializeDetailsViews()
{
	FPropertyEditorModule& Properties = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	FDetailsViewArgs Args;
	Args.bAllowSearch = false;
	Args.bHideSelectionTip = true;
	Args.bShowOptions = false;
	Args.bShowScrollBar = false;
	Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;

	LayerDetailsView = Properties.CreateDetailView(Args);
	LayerDetailsView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateLambda([](const FPropertyAndParent& Property)
	{
		return GetRootPropertyName(Property) == GET_MEMBER_NAME_CHECKED(UComposableCameraMeshLayerSelection, Layer);
	}));
	LayerDetailsView->SetObject(Mode ? Mode->GetSelectionEditor() : nullptr);

	ShapeDetailsView = Properties.CreateDetailView(Args);
	ShapeDetailsView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateLambda([](const FPropertyAndParent& Property)
	{
		return GetRootPropertyName(Property) != GET_MEMBER_NAME_CHECKED(UComposableCameraMeshLayerSelection, Layer);
	}));
	ShapeDetailsView->SetObject(Mode ? Mode->GetSelectionEditor() : nullptr);

	ToolDetailsView = Properties.CreateDetailView(Args);
	const TWeakObjectPtr<UComposableCameraMeshLayerToolSettings> WeakSettings = Mode ? Mode->GetSettings() : nullptr;
	const TWeakPtr<FComposableCameraMeshLayerModeToolkit> WeakToolkit = SharedThis(this);
	ToolDetailsView->RegisterInstancedCustomPropertyLayout(UComposableCameraMeshLayerToolSettings::StaticClass(),
		FOnGetDetailCustomizationInstance::CreateLambda([WeakSettings, WeakToolkit]() -> TSharedRef<IDetailCustomization>
		{
			return MakeShared<FMeshToolDetailsCustomization>(WeakSettings, WeakToolkit);
		}));
	ToolDetailsView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateLambda([WeakSettings](const FPropertyAndParent& Property)
	{
		const auto* Settings = WeakSettings.Get();
		return Settings && Settings->IsToolPropertyVisible(GetRootPropertyName(Property));
	}));
	ToolDetailsView->SetObject(Mode ? Mode->GetSettings() : nullptr);
}

void FComposableCameraMeshLayerModeToolkit::HandleToolModeChanged(EComposableCameraMeshToolMode NewMode)
{
	if (Mode && Mode->GetSettings() && Mode->GetSettings()->GetToolMode() != NewMode)
	{
		// Complete a brush stroke / cancel a draft while the old tool is still active.
		Mode->ResetInteraction();
		Mode->GetSettings()->SetToolMode(NewMode);
		RefreshSelectionDetails();
	}
}

TSharedRef<SWidget> FComposableCameraMeshLayerModeToolkit::BuildDrawToolMenu()
{
	FMenuBuilder Menu(true, nullptr);
	const TWeakPtr<FComposableCameraMeshLayerModeToolkit> WeakToolkit = SharedThis(this);
	for (EComposableCameraMeshDrawTool Tool : { EComposableCameraMeshDrawTool::Brush,
		EComposableCameraMeshDrawTool::Rectangle, EComposableCameraMeshDrawTool::Circle, EComposableCameraMeshDrawTool::Polygon })
	{
		Menu.AddMenuEntry(StaticEnum<EComposableCameraMeshDrawTool>()->GetDisplayNameTextByValue(static_cast<int64>(Tool)),
			FComposableCameraMeshLayerEdMode::GetToolInstructions(Tool), FSlateIcon(), FUIAction(
				FExecuteAction::CreateLambda([WeakToolkit, Tool]()
				{
					const auto Pinned = WeakToolkit.Pin();
					if (Pinned && Pinned->Mode && Pinned->Mode->GetSettings())
					{
						Pinned->Mode->ResetInteraction();
						Pinned->Mode->GetSettings()->SetDrawTool(Tool);
						Pinned->RefreshSelectionDetails();
					}
				}), FCanExecuteAction(), FIsActionChecked::CreateLambda([WeakToolkit, Tool]()
				{
					const auto Pinned = WeakToolkit.Pin();
					return Pinned && Pinned->Mode && Pinned->Mode->GetSettings() && Pinned->Mode->GetSettings()->GetDrawTool() == Tool;
				})), NAME_None, EUserInterfaceActionType::RadioButton);
	}
	return Menu.MakeWidget();
}

TSharedRef<SWidget> FComposableCameraMeshLayerModeToolkit::BuildToolPanel()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SSegmentedControl<EComposableCameraMeshToolMode>)
			.Value_Lambda([this]()
			{
				return Mode && Mode->GetSettings() ? Mode->GetSettings()->GetToolMode() : EComposableCameraMeshToolMode::Draw;
			})
			.OnValueChanged(this, &FComposableCameraMeshLayerModeToolkit::HandleToolModeChanged)
			.UniformPadding(FMargin(12.0f, 6.0f))
			+ SSegmentedControl<EComposableCameraMeshToolMode>::Slot(EComposableCameraMeshToolMode::Draw)
				.Text(LOCTEXT("DrawMode", "Draw"))
				.ToolTip(LOCTEXT("DrawModeTooltip", "Draw coverage in the selected Layer. Choose Brush, Rectangle, Circle or Polygon below.\nCtrl+Z / Ctrl+Y undo / redo."))
			+ SSegmentedControl<EComposableCameraMeshToolMode>::Slot(EComposableCameraMeshToolMode::Select)
				.Text(LOCTEXT("SelectMode", "Select"))
				.ToolTip(FComposableCameraMeshLayerEdMode::GetToolInstructions(EComposableCameraMeshDrawTool::Select))
			+ SSegmentedControl<EComposableCameraMeshToolMode>::Slot(EComposableCameraMeshToolMode::Erase)
				.Text(LOCTEXT("EraseMode", "Erase"))
				.ToolTip(FComposableCameraMeshLayerEdMode::GetToolInstructions(EComposableCameraMeshDrawTool::Erase))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)
			.Visibility_Lambda([this]()
			{
				return Mode && Mode->GetSettings() && Mode->GetSettings()->GetToolMode() == EComposableCameraMeshToolMode::Draw
					? EVisibility::Visible : EVisibility::Collapsed;
			})
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(LOCTEXT("DrawType", "Drawing Type"))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SComboButton)
				.OnGetMenuContent(this, &FComposableCameraMeshLayerModeToolkit::BuildDrawToolMenu)
				.ToolTipText_Lambda([this]()
				{
					const auto* Settings = Mode ? Mode->GetSettings() : nullptr;
					return FComposableCameraMeshLayerEdMode::GetToolInstructions(Settings ? Settings->GetDrawTool() : EComposableCameraMeshDrawTool::Brush);
				})
				.ButtonContent()
				[
					SNew(STextBlock).Text_Lambda([this]()
					{
						const auto* Settings = Mode ? Mode->GetSettings() : nullptr;
						return Settings ? StaticEnum<EComposableCameraMeshDrawTool>()->GetDisplayNameTextByValue(static_cast<int64>(Settings->GetDrawTool())) : FText::GetEmpty();
					})
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			ToolDetailsView.ToSharedRef()
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SVerticalBox)
			.Visibility_Lambda([this]()
			{
				return Mode && Mode->GetSettings() && Mode->GetSettings()->GetToolMode() == EComposableCameraMeshToolMode::Select
					? EVisibility::Visible : EVisibility::Collapsed;
			})
			+ SVerticalBox::Slot().AutoHeight()
			[
				ShapeDetailsView.ToSharedRef()
			]
		];
}

void FComposableCameraMeshLayerModeToolkit::RefreshDocument()
{
	RefreshLayerItems();
}

void FComposableCameraMeshLayerModeToolkit::RefreshSelectionDetails()
{
	if ((!LayerDetailsView.IsValid() && !ShapeDetailsView.IsValid() && !ToolDetailsView.IsValid()) || SelectionRefreshHandle.IsValid()) { return; }
	// IDetailsView has only ForceRefresh. Defer it beyond property/Undo callbacks.
	const TWeakPtr<FComposableCameraMeshLayerModeToolkit> WeakToolkit = SharedThis(this);
	SelectionRefreshHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[WeakToolkit](float)
		{
			if (const TSharedPtr<FComposableCameraMeshLayerModeToolkit> Pinned = WeakToolkit.Pin())
			{
				// A queued request can predate slider capture. Never replace its widget
				// before PropertyEditor closes the transaction on mouse release.
				if (GEditor && GEditor->IsTransactionActive()) { return true; }
				Pinned->SelectionRefreshHandle.Reset();
				if (Pinned->LayerDetailsView.IsValid()) { Pinned->LayerDetailsView->ForceRefresh(); }
				if (Pinned->ShapeDetailsView.IsValid()) { Pinned->ShapeDetailsView->ForceRefresh(); }
				if (Pinned->ToolDetailsView.IsValid()) { Pinned->ToolDetailsView->ForceRefresh(); }
			}
			return false;
		}));
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
	// The UObject proxy remains valid when document arrays relocate or undo restores them.
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

	Mode->RefreshSelectionEditor();
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
		if (Layer->bEnabled == (NewState == ECheckBoxState::Checked)) { return; }
		Settings->OnBeforeEdit.ExecuteIfBound();
		const FScopedTransaction Transaction(LOCTEXT("EnableLayerTransaction", "Toggle Mesh Camera Layer"));
		Settings->Modify();
		Layer->bEnabled = NewState == ECheckBoxState::Checked;
		Settings->NotifyLayerDataChanged();
		Mode->RefreshSelectionEditor();
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

FReply FComposableCameraMeshLayerModeToolkit::HandleDiscardClicked()
{
	ReleaseActiveLayerDetails();
	if (!Mode || !Mode->DiscardWorkingData()) { RefreshLayerItems(); }
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
