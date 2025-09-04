// Copyright Sulley. All rights reserved.

#include "AssetTools/AssetDefinition_ComposableCameraVariableCollection.h"

#define LOCTEXT_NAMESPACE "AssetDefinition_ComposableCameraVariableCollection"

#include "ComposableCameraSystemEditorModule.h"
#include "Variables/ComposableCameraVariableCollection.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AssetDefinition_ComposableCameraVariableCollection)

TConstArrayView<FAssetCategoryPath> UAssetDefinition_ComposableCameraVariableCollection::StaticMenuCategories()
{
	static const auto Categories = { FAssetCategoryPath(FText::FromString("Composable Camera System")) };
	return Categories;
}

FText UAssetDefinition_ComposableCameraVariableCollection::GetAssetDisplayName() const
{
	return LOCTEXT("AssetDisplayName", "Composable Camera Variable Collection");
}

FLinearColor UAssetDefinition_ComposableCameraVariableCollection::GetAssetColor() const
{
	return FLinearColor(FColor(100, 10, 120));
}

TSoftClassPtr<UObject> UAssetDefinition_ComposableCameraVariableCollection::GetAssetClass() const
{
	return UComposableCameraVariableCollection::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_ComposableCameraVariableCollection::GetAssetCategories() const
{
	return StaticMenuCategories();
}

FAssetOpenSupport UAssetDefinition_ComposableCameraVariableCollection::GetAssetOpenSupport(
	const FAssetOpenSupportArgs& OpenSupportArgs) const
{
	return FAssetOpenSupport(
		OpenSupportArgs.OpenMethod,
		OpenSupportArgs.OpenMethod == EAssetOpenMethod::Edit,
		EToolkitMode::Standalone
	);
}

EAssetCommandResult UAssetDefinition_ComposableCameraVariableCollection::OpenAssets(
	const FAssetOpenArgs& OpenArgs) const
{
	FComposableCameraSystemEditorModule& EditorModule = FModuleManager::LoadModuleChecked<FComposableCameraSystemEditorModule>("ComposableCameraSystemEditor");
	for (UComposableCameraVariableCollection* CameraVariableCollection : OpenArgs.LoadObjects<UComposableCameraVariableCollection>())
	{
		EditorModule.CreateComposableCameraVariableCollectionEditor(OpenArgs.GetToolkitMode(), OpenArgs.ToolkitHost, CameraVariableCollection);
	}

	return EAssetCommandResult::Handled;
}

#undef LOCTEXT_NAMESPACE