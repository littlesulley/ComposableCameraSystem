// Copyright Sulley. All rights reserved.

#include "AssetTools/AssetDefinition_ComposableCameraTransitionTable.h"
#include "DataAssets/ComposableCameraTransitionTableDataAsset.h"

#define LOCTEXT_NAMESPACE "AssetDefinition_ComposableCameraTransitionTable"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AssetDefinition_ComposableCameraTransitionTable)

FText UAssetDefinition_ComposableCameraTransitionTable::GetAssetDisplayName() const
{
	return LOCTEXT("AssetDisplayName", "Camera Transition Table");
}

FLinearColor UAssetDefinition_ComposableCameraTransitionTable::GetAssetColor() const
{
	// Orange to distinguish from the teal camera type assets.
	return FLinearColor(FColor(200, 120, 40));
}

TSoftClassPtr<UObject> UAssetDefinition_ComposableCameraTransitionTable::GetAssetClass() const
{
	return UComposableCameraTransitionTableDataAsset::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_ComposableCameraTransitionTable::GetAssetCategories() const
{
	static const auto Categories = { FAssetCategoryPath(FText::FromString("Composable Camera System")) };
	return Categories;
}

#undef LOCTEXT_NAMESPACE
