// Copyright Sulley. All rights reserved.

#include "AssetTools/AssetDefinition_ComposableCameraPatchTypeAsset.h"
#include "DataAssets/ComposableCameraPatchTypeAsset.h"
#include "ComposableCameraSystemEditorModule.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "AssetDefinition_ComposableCameraPatchTypeAsset"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AssetDefinition_ComposableCameraPatchTypeAsset)

FText UAssetDefinition_ComposableCameraPatchTypeAsset::GetAssetDisplayName() const
{
	return LOCTEXT("AssetDisplayName", "Composable Camera Patch");
}

FLinearColor UAssetDefinition_ComposableCameraPatchTypeAsset::GetAssetColor() const
{
	// Warm orange - distinct from the teal Camera Type asset and the purple
	// Modifier asset. Reads as "temporary, additive overlay" rather than
	// "primary camera definition".
	return FLinearColor(FColor(224, 128, 32));
}

TSoftClassPtr<UObject> UAssetDefinition_ComposableCameraPatchTypeAsset::GetAssetClass() const
{
	return UComposableCameraPatchTypeAsset::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_ComposableCameraPatchTypeAsset::GetAssetCategories() const
{
	static const auto Categories = { FAssetCategoryPath(FText::FromString("Composable Camera System")) };
	return Categories;
}

FAssetOpenSupport UAssetDefinition_ComposableCameraPatchTypeAsset::GetAssetOpenSupport(const FAssetOpenSupportArgs& OpenSupportArgs) const
{
	return FAssetOpenSupport(OpenSupportArgs.OpenMethod,
		OpenSupportArgs.OpenMethod == EAssetOpenMethod::Edit,
		EToolkitMode::Standalone);
}

EAssetCommandResult UAssetDefinition_ComposableCameraPatchTypeAsset::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	// Patch asset reuses the existing TypeAsset editor unchanged - it IS-A
	// TypeAsset and its graph schema / pin system / parameter system are
	// inherited verbatim. Pass each loaded Patch through the same module entry
	// point used by UAssetDefinition_ComposableCameraTypeAsset.
	FComposableCameraSystemEditorModule& EditorModule = FModuleManager::LoadModuleChecked<FComposableCameraSystemEditorModule>("ComposableCameraSystemEditor");
	for (UComposableCameraPatchTypeAsset* PatchAsset: OpenArgs.LoadObjects<UComposableCameraPatchTypeAsset>())
	{
		EditorModule.CreateComposableCameraTypeAssetEditor(OpenArgs.GetToolkitMode(), OpenArgs.ToolkitHost, PatchAsset);
	}

	return EAssetCommandResult::Handled;
}

#undef LOCTEXT_NAMESPACE
