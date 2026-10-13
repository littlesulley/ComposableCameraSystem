// Copyright 2026 Sulley. All Rights Reserved.
#include "AssetTools/AssetDefinition_ComposableCameraShotTemplateLibrary.h"
#include "DataAssets/ComposableCameraShotTemplateLibrary.h"

FText UAssetDefinition_ComposableCameraShotTemplateLibrary::GetAssetDisplayName() const
{
	return NSLOCTEXT("ComposableCameraShotTemplateLibrary", "AssetName", "Composable Camera Shot Template Library");
}
FLinearColor UAssetDefinition_ComposableCameraShotTemplateLibrary::GetAssetColor() const
{
	return FLinearColor(FColor(64, 192, 160));
}
TSoftClassPtr<UObject> UAssetDefinition_ComposableCameraShotTemplateLibrary::GetAssetClass() const
{
	return UComposableCameraShotTemplateLibrary::StaticClass();
}
TConstArrayView<FAssetCategoryPath> UAssetDefinition_ComposableCameraShotTemplateLibrary::GetAssetCategories() const
{
	static const auto Categories = { FAssetCategoryPath(FText::FromString(TEXT("Composable Camera System"))) };
	return Categories;
}
