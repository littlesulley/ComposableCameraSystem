// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "IContentBrowserSingleton.h"

class IPropertyHandle;
class UComposableCameraVariable;
class UComposableCameraVariableCollection;

DECLARE_DELEGATE_OneParam(FOnCameraVariableSelected, UComposableCameraVariable*);

/**
 * Configuration structure for a camera variable picker widget.
 * This is a widget that shows the list of camera variables inside a given
 * composable camera variable collection. It can be filtered by variable type.
 */
struct FComposableCameraVariablePickerConfig
{
	/** The initially selected camera variable collection, if any. */
	FAssetData InitialComposableCameraVariableCollectionSelection;

	/** 
	 * The initially selected composable camera variable, if any.
	 * When set, InitialComposableCameraVariableCollectionSelection is ignored, and the outer
	 * collection of the selected variable is used instead.
	 */
	UComposableCameraVariable* InitialComposableCameraVariableSelection = nullptr;

	/** The type of variable to select. */
	UClass* ComposableCameraVariableClass = nullptr;

	/** Asset picker view type for the composable camera variable collection picker. */
	EAssetViewType::Type CameraAssetViewType = EAssetViewType::List;

	/** Asset picker settings name for the composable camera variable collection picker. */
	FString ComposableCameraVariableCollectionSaveSettingsName;

	/** Callback for when a composable camera variable has been selected. */
	FOnCameraVariableSelected OnCameraVariableSelected;
};

/**
 * A picker widget for selecting a composable camera variable.
 */
class COMPOSABLECAMERASYSTEMEDITOR_API SComposableCameraVariablePicker
	: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SComposableCameraVariablePicker)
	{}
		SLATE_ARGUMENT(FComposableCameraVariablePickerConfig, ComposableCameraVariablePickerConfig)	
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	void SetupInitialSelection(UComposableCameraVariable* InInitialComposableCameraVariableSelection);
	TSharedRef<SWidget> BuildVariableCollectionAssetPicker();
	void OnAssetSelected(const FAssetData& AssetData);
	void UpdateVariableListItemsSource(UComposableCameraVariableCollection* InVariableCollection = nullptr);
	TSharedRef<ITableRow> OnVariableListGenerateRow(UComposableCameraVariable* Item, const TSharedRef<STableViewBase>& OwnerTable);
	void OnVariableListSelectionChanged(UComposableCameraVariable* Item, ESelectInfo::Type SelectInfo) const;

	FText GetCameraVariableCountText() const;

private:
	TSharedPtr<SListView<UComposableCameraVariable*>> ComposableCameraVariableListView;
	TArray<UComposableCameraVariable*> ComposableCameraVariableItemsSource;

	UClass* VariableClass = nullptr;

	FGetCurrentSelectionDelegate GetCurrentAssetPickerSelection;

	FOnCameraVariableSelected OnCameraVariableSelected;
};
