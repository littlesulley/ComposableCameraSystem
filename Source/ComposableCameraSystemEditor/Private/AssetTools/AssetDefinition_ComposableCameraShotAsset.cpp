// Copyright Sulley. All rights reserved.

#include "AssetTools/AssetDefinition_ComposableCameraShotAsset.h"
#include "DataAssets/ComposableCameraShotAsset.h"
#include "Editors/ComposableCameraShotEditor.h"

#define LOCTEXT_NAMESPACE "AssetDefinition_ComposableCameraShotAsset"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AssetDefinition_ComposableCameraShotAsset)

FText UAssetDefinition_ComposableCameraShotAsset::GetAssetDisplayName() const
{
	return LOCTEXT("AssetDisplayName", "Composable Camera Shot");
}

FLinearColor UAssetDefinition_ComposableCameraShotAsset::GetAssetColor() const
{
	// Cool teal-green - distinct from the orange Patch and the warmer teal of
	// the Camera Type asset. Reads as "framing data, not a runnable camera".
	return FLinearColor(FColor(64, 192, 160));
}

TSoftClassPtr<UObject> UAssetDefinition_ComposableCameraShotAsset::GetAssetClass() const
{
	return UComposableCameraShotAsset::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_ComposableCameraShotAsset::GetAssetCategories() const
{
	static const auto Categories = { FAssetCategoryPath(FText::FromString("Composable Camera System")) };
	return Categories;
}

EAssetCommandResult UAssetDefinition_ComposableCameraShotAsset::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	// Route every loaded ShotAsset into the single-instance Shot Editor.
	// HostObject = the asset itself, so the editor's FNotifyHook bridges
	// Shot edits through the asset's Modify() + PostEditChangeProperty - 
	// the asset's own undo stack and dirty flag stay in sync.
	for (UComposableCameraShotAsset* ShotAsset: OpenArgs.LoadObjects<UComposableCameraShotAsset>())
	{
		if (ShotAsset)
		{
			FComposableCameraShotEditor::OpenForShot(&ShotAsset->Shot, ShotAsset);
		}
	}
	return EAssetCommandResult::Handled;
}

#undef LOCTEXT_NAMESPACE
