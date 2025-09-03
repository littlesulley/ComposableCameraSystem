// Copyright Sulley. All rights reserved.

#include "Widgets/ComposableCameraVariablePicker.h"

#define LOCTEXT_NAMESPACE "SComposableCameraVariablePicker"

void SComposableCameraVariablePicker::Construct(const FArguments& InArgs)
{
	
}

void SComposableCameraVariablePicker::SetupInitialSelection(
	UComposableCameraVariable* InInitialComposableCameraVariableSelection)
{
}

TSharedRef<SWidget> SComposableCameraVariablePicker::BuildVariableCollectionAssetPicker()
{
}

void SComposableCameraVariablePicker::OnAssetSelected(const FAssetData& AssetData)
{
	UpdateVariableListItemsSource();
}

void SComposableCameraVariablePicker::UpdateVariableListItemsSource(
	UComposableCameraVariableCollection* InVariableCollection)
{
}

TSharedRef<ITableRow> SComposableCameraVariablePicker::OnVariableListGenerateRow(UComposableCameraVariable* Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
}

void SComposableCameraVariablePicker::OnVariableListSelectionChanged(UComposableCameraVariable* Item,
	ESelectInfo::Type SelectInfo) const
{
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
