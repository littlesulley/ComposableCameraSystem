// Copyright Sulley. All rights reserved.

#include "AssetTools/ComposableCameraCameraAssetEditorToolkit.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ComposableCameraCameraAssetEditorToolkit)

#define LOCTEXT_NAMESPACE "ComposableCameraCameraAssetEditorToolkit"

FComposableCameraCameraAssetEditorToolkit::FComposableCameraCameraAssetEditorToolkit(UAssetEditor* InAssetEditor)
	: FBaseAssetToolkit(InAssetEditor)
{

}

FComposableCameraCameraAssetEditorToolkit::~FComposableCameraCameraAssetEditorToolkit()
{
}

void FComposableCameraCameraAssetEditorToolkit::SetCameraAsset(UComposableCameraCameraAsset* InCameraAsset)
{
	CameraAsset = InCameraAsset;
}

void FComposableCameraCameraAssetEditorToolkit::RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
}

void FComposableCameraCameraAssetEditorToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
}

void FComposableCameraCameraAssetEditorToolkit::CreateWidgets()
{
}

void FComposableCameraCameraAssetEditorToolkit::RegisterToolbar()
{
}

void FComposableCameraCameraAssetEditorToolkit::InitToolMenuContext(FToolMenuContext& MenuContext)
{
}

void FComposableCameraCameraAssetEditorToolkit::PostInitAssetEditor()
{
}

void FComposableCameraCameraAssetEditorToolkit::PostRegenerateMenusAndToolbars()
{
}

FText FComposableCameraCameraAssetEditorToolkit::GetBaseToolkitName() const
{
	return FText();
}

FName FComposableCameraCameraAssetEditorToolkit::GetToolkitFName() const
{
	return FName();
}

FString FComposableCameraCameraAssetEditorToolkit::GetWorldCentricTabPrefix() const
{
	return FString();
}

FLinearColor FComposableCameraCameraAssetEditorToolkit::GetWorldCentricTabColorScale() const
{
	return FLinearColor();
}

#undef LOCTEXT_NAMESPACE