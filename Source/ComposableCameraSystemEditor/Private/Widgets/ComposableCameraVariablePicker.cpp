// Copyright Sulley. All rights reserved.

#include "Widgets/ComposableCameraVariablePicker.h"

#include "ContentBrowserModule.h"
#include "Variables/ComposableCameraVariable.h"
#include "Variables/ComposableCameraVariableCollection.h"

#define LOCTEXT_NAMESPACE "SComposableCameraVariablePicker"

void SComposableCameraVariablePicker::Construct(const FArguments& InArgs)
{
	const FComposableCameraVariablePickerConfig& PickerConfig = InArgs._ComposableCameraVariablePickerConfig;

	VariableClass = PickerConfig.ComposableCameraVariableClass;
	OnCameraVariableSelected = PickerConfig.OnCameraVariableSelected;

	ChildSlot
	[
		SNew(SBox)
		.HeightOverride(400)
		.WidthOverride(350)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Menu.Background"))
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.FillHeight(0.55f)
				[
					BuildVariableCollectionAssetPicker(PickerConfig)
				]
				+SVerticalBox::Slot()
				.FillHeight(0.45f)
				.Padding(0.f, 3.f)
				[
					SAssignNew(ComposableCameraVariableListView, SListView<UComposableCameraVariable*>)
					.ListItemsSource(&ComposableCameraVariableItemsSource)
					.OnGenerateRow(this, &SComposableCameraVariablePicker::OnVariableListGenerateRow)
					.OnSelectionChanged(this, &SComposableCameraVariablePicker::OnVariableListSelectionChanged)
				]
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.FillWidth(1.f)
					.VAlign(VAlign_Center)
					.Padding(8, 5)
					[
						SNew(STextBlock)
						.Text(this, &SComposableCameraVariablePicker::GetCameraVariableCountText)
					]
				]
			]
		]
	];

	if (PickerConfig.InitialComposableCameraVariableSelection)
	{
		SetupInitialSelection(PickerConfig.InitialComposableCameraVariableSelection);
	}
}

void SComposableCameraVariablePicker::SetupInitialSelection(
	UComposableCameraVariable* InInitialComposableCameraVariable)
{
	UComposableCameraVariableCollection* InitialVariableCollection = nullptr;
	if (InInitialComposableCameraVariable)
	{
		InitialVariableCollection = InInitialComposableCameraVariable->GetTypedOuter<UComposableCameraVariableCollection>();
	}

	UpdateVariableListItemsSource(InitialVariableCollection);

	ComposableCameraVariableListView->SetSelection(InInitialComposableCameraVariable);
	ComposableCameraVariableListView->RequestScrollIntoView(InInitialComposableCameraVariable);
}

TSharedRef<SWidget> SComposableCameraVariablePicker::BuildVariableCollectionAssetPicker(const FComposableCameraVariablePickerConfig& InPickerConfig)
{
	FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	FAssetPickerConfig AssetPickerConfig;
	FARFilter ARFilter;
	ARFilter.bRecursiveClasses = true;
	ARFilter.ClassPaths.Add(FTopLevelAssetPath(UComposableCameraVariableCollection::StaticClass()->GetPathName()));

	FAssetData InitialVariableCollection = InPickerConfig.InitialComposableCameraVariableCollectionSelection;
	if (InPickerConfig.InitialComposableCameraVariableSelection)
	{
		InitialVariableCollection = InPickerConfig.InitialComposableCameraVariableSelection->GetTypedOuter<UComposableCameraVariableCollection>();
	}

	AssetPickerConfig.bAllowDragging = false;
	AssetPickerConfig.bCanShowClasses = false;
	AssetPickerConfig.bAllowNullSelection = false;
	AssetPickerConfig.bShowBottomToolbar = true;
	AssetPickerConfig.bFocusSearchBoxWhenOpened = true;
	AssetPickerConfig.SelectionMode = ESelectionMode::Single;
	AssetPickerConfig.Filter = ARFilter;
	AssetPickerConfig.SaveSettingsName = InPickerConfig.ComposableCameraVariableCollectionSaveSettingsName;
	AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
	AssetPickerConfig.InitialAssetSelection = InitialVariableCollection;
	AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateSP(this, &SComposableCameraVariablePicker::OnAssetSelected);
	AssetPickerConfig.GetCurrentSelectionDelegates.Add(&GetCurrentAssetPickerSelection);

	return ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig);
}

void SComposableCameraVariablePicker::OnAssetSelected(const FAssetData& AssetData)
{
	UpdateVariableListItemsSource();
}

void SComposableCameraVariablePicker::UpdateVariableListItemsSource(
	UComposableCameraVariableCollection* InVariableCollection)
{
	UComposableCameraVariableCollection* Collection = InVariableCollection;
	if (!InVariableCollection)
	{
		TArray<FAssetData> SelectedAssets;
		if (GetCurrentAssetPickerSelection.IsBound())
		{
			SelectedAssets = GetCurrentAssetPickerSelection.Execute();
		}
		if (!SelectedAssets.IsEmpty())
		{
			Collection = Cast<UComposableCameraVariableCollection>(SelectedAssets[0].GetAsset());
		}
	}

	ComposableCameraVariableItemsSource.Reset();
	if (Collection)
	{
		if (VariableClass)
		{
			ComposableCameraVariableItemsSource = Collection->Variables.FilterByPredicate(
				[this](UComposableCameraVariable* Item) { return Item->GetClass() == VariableClass; });
		}
		else
		{
			ComposableCameraVariableItemsSource = Collection->Variables;
		}
	}

	ComposableCameraVariableListView->RequestListRefresh();
}

TSharedRef<ITableRow> SComposableCameraVariablePicker::OnVariableListGenerateRow(UComposableCameraVariable* Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	const FText DisplayName = Item->DisplayName.IsEmpty() ? FText::FromName(Item->GetFName()) : FText::FromString(Item->DisplayName);

	return SNew(STableRow<UComposableCameraVariable*>, OwnerTable)
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SImage)
				.ColorAndOpacity(FSlateColor::UseForeground())
				.Image(FAppStyle::GetBrush("Menu.Background"))
			]
			+SHorizontalBox::Slot()
			.FillWidth(1.f)
			.Padding(4.f, 2.f)
			[
				SNew(STextBlock)
				.Text(DisplayName)
			]
		];
}

void SComposableCameraVariablePicker::OnVariableListSelectionChanged(UComposableCameraVariable* Item,
	ESelectInfo::Type SelectInfo) const
{
	if (SelectInfo != ESelectInfo::Direct)
	{
		OnCameraVariableSelected.ExecuteIfBound(Item);
	}
}

FText SComposableCameraVariablePicker::GetCameraVariableCountText() const
{
	const int32 NumComposableCameraVariables = ComposableCameraVariableItemsSource.Num();

	FText CountText = FText::GetEmpty();
	if (NumComposableCameraVariables == 1)
	{
		CountText = LOCTEXT("ComposableCameraVariableCountTextSingular", "1 item");
	}
	else
	{
		CountText = FText::Format(LOCTEXT("ComposableCameraVariableCountTextPlural", "{0} items"), FText::AsNumber(NumComposableCameraVariables));
	}
	return CountText;
}

#undef LOCTEXT_NAMESPACE