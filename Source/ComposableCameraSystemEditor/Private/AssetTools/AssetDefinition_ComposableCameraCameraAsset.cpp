// Copyright Sulley. All rights reserved.

#include "AssetTools/AssetDefinition_ComposableCameraCameraAsset.h"
#include "Core/ComposableCameraCameraAsset.h"
#include "ComposableCameraSystemEditorModule.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "AssetDefinition_ComposableCameraCameraAsset"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AssetDefinition_ComposableCameraCameraAsset)

FText UAssetDefinition_ComposableCameraCameraAsset::GetAssetDisplayName() const
{
	return LOCTEXT("AssetDisplayName", "Composable Camera Asset");
}

FLinearColor UAssetDefinition_ComposableCameraCameraAsset::GetAssetColor() const
{
	return FLinearColor(FColor(100, 10, 120));
}

TSoftClassPtr<UObject> UAssetDefinition_ComposableCameraCameraAsset::GetAssetClass() const
{
	return UComposableCameraCameraAsset::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_ComposableCameraCameraAsset::GetAssetCategories() const
{
	// You don't need to register category manually by aseet actions.
	static const auto Categories = { FAssetCategoryPath(FText::FromString("Composable Camera System")) };
	return Categories;
}

FAssetOpenSupport UAssetDefinition_ComposableCameraCameraAsset::GetAssetOpenSupport(const FAssetOpenSupportArgs& OpenSupportArgs) const
{
	return FAssetOpenSupport(
		OpenSupportArgs.OpenMethod,
		OpenSupportArgs.OpenMethod == EAssetOpenMethod::Edit,
		EToolkitMode::Standalone
	);
}

EAssetCommandResult UAssetDefinition_ComposableCameraCameraAsset::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	FComposableCameraSystemEditorModule& EditorModule = FModuleManager::LoadModuleChecked<FComposableCameraSystemEditorModule>("ComposableCameraSystemEditor");
	for (UComposableCameraCameraAsset* CameraAsset : OpenArgs.LoadObjects<UComposableCameraCameraAsset>())
	{
		EditorModule.CreateComposableCameraCameraAssetEditor(OpenArgs.GetToolkitMode(), OpenArgs.ToolkitHost, CameraAsset);
	}

	return EAssetCommandResult::Handled;
}

#undef LOCTEXT_NAMESPACE