// Copyright Sulley. All rights reserved.

#include "AssetTools/AssetDefinition_ComposableCameraTransition.h"
#include "DataAssets/ComposableCameraTransitionDataAsset.h"

#define LOCTEXT_NAMESPACE "AssetDefinition_ComposableCameraTransition"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AssetDefinition_ComposableCameraTransition)

FText UAssetDefinition_ComposableCameraTransition::GetAssetDisplayName() const
{
	return LOCTEXT("AssetDisplayName", "Camera Transition");
}

FLinearColor UAssetDefinition_ComposableCameraTransition::GetAssetColor() const
{
	// Warm orange, slightly different shade from the transition table.
	return FLinearColor(FColor(220, 140, 50));
}

TSoftClassPtr<UObject> UAssetDefinition_ComposableCameraTransition::GetAssetClass() const
{
	return UComposableCameraTransitionDataAsset::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_ComposableCameraTransition::GetAssetCategories() const
{
	static const auto Categories = { FAssetCategoryPath(FText::FromString("Composable Camera System")) };
	return Categories;
}

#undef LOCTEXT_NAMESPACE
