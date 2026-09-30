// Copyright 2026 Sulley. All Rights Reserved.

#include "AssetTools/AssetDefinition_ComposableCameraActionTypeAsset.h"
#include "DataAssets/ComposableCameraActionTypeAsset.h"

#define LOCTEXT_NAMESPACE "AssetDefinition_ComposableCameraActionTypeAsset"

FText UAssetDefinition_ComposableCameraActionTypeAsset::GetAssetDisplayName() const
{
	return LOCTEXT("DisplayName", "Composable Camera Action");
}

FLinearColor UAssetDefinition_ComposableCameraActionTypeAsset::GetAssetColor() const
{
	return FLinearColor(FColor(102, 90, 229));
}

TSoftClassPtr<UObject> UAssetDefinition_ComposableCameraActionTypeAsset::GetAssetClass() const
{
	return UComposableCameraActionTypeAsset::StaticClass();
}

TConstArrayView<FAssetCategoryPath>
UAssetDefinition_ComposableCameraActionTypeAsset::GetAssetCategories() const
{
	static const auto Categories = {
		FAssetCategoryPath(FText::FromString("Composable Camera System")) };
	return Categories;
}

#undef LOCTEXT_NAMESPACE
