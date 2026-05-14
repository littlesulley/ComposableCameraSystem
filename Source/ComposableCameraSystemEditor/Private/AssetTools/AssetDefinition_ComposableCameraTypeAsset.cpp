// Copyright Sulley. All rights reserved.

#include "AssetTools/AssetDefinition_ComposableCameraTypeAsset.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "ComposableCameraSystemEditorModule.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "AssetDefinition_ComposableCameraTypeAsset"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AssetDefinition_ComposableCameraTypeAsset)

FText UAssetDefinition_ComposableCameraTypeAsset::GetAssetDisplayName() const
{
	return LOCTEXT("AssetDisplayName", "Composable Camera Type");
}

FLinearColor UAssetDefinition_ComposableCameraTypeAsset::GetAssetColor() const
{
	// Distinctive teal to differentiate from the purple camera asset.
	return FLinearColor(FColor(20, 150, 140));
}

TSoftClassPtr<UObject> UAssetDefinition_ComposableCameraTypeAsset::GetAssetClass() const
{
	return UComposableCameraTypeAsset::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_ComposableCameraTypeAsset::GetAssetCategories() const
{
	static const auto Categories = { FAssetCategoryPath(FText::FromString("Composable Camera System")) };
	return Categories;
}

FAssetOpenSupport UAssetDefinition_ComposableCameraTypeAsset::GetAssetOpenSupport(const FAssetOpenSupportArgs& OpenSupportArgs) const
{
	return FAssetOpenSupport(OpenSupportArgs.OpenMethod,
		OpenSupportArgs.OpenMethod == EAssetOpenMethod::Edit,
		EToolkitMode::Standalone);
}

EAssetCommandResult UAssetDefinition_ComposableCameraTypeAsset::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	FComposableCameraSystemEditorModule& EditorModule = FModuleManager::LoadModuleChecked<FComposableCameraSystemEditorModule>("ComposableCameraSystemEditor");
	for (UComposableCameraTypeAsset* TypeAsset: OpenArgs.LoadObjects<UComposableCameraTypeAsset>())
	{
		EditorModule.CreateComposableCameraTypeAssetEditor(OpenArgs.GetToolkitMode(), OpenArgs.ToolkitHost, TypeAsset);
	}

	return EAssetCommandResult::Handled;
}

#undef LOCTEXT_NAMESPACE
