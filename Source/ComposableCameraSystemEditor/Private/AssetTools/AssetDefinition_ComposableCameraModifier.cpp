// Copyright 2026 Sulley. All Rights Reserved.

#include "AssetTools/AssetDefinition_ComposableCameraModifier.h"
#include "DataAssets/ComposableCameraModifierDataAsset.h"

#define LOCTEXT_NAMESPACE "AssetDefinition_ComposableCameraModifier"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AssetDefinition_ComposableCameraModifier)

FText UAssetDefinition_ComposableCameraModifier::GetAssetDisplayName() const
{
	return LOCTEXT("AssetDisplayName", "Camera Node Modifier");
}

FLinearColor UAssetDefinition_ComposableCameraModifier::GetAssetColor() const
{
	// Purple to distinguish from teal (type asset) and orange (transition table).
	return FLinearColor(FColor(160, 90, 200));
}

TSoftClassPtr<UObject> UAssetDefinition_ComposableCameraModifier::GetAssetClass() const
{
	return UComposableCameraNodeModifierDataAsset::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_ComposableCameraModifier::GetAssetCategories() const
{
	static const auto Categories = { FAssetCategoryPath(FText::FromString("Composable Camera System")) };
	return Categories;
}

#undef LOCTEXT_NAMESPACE
