// Copyright Sulley. All rights reserved.

#include "Toolkits/ComposableCameraCameraAssetEditorToolkit.h"
#include "Toolkits/ComposableCameraCameraAssetEditorToolkitBase.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ComposableCameraCameraAssetEditorToolkit)

#define LOCTEXT_NAMESPACE "ComposableCameraCameraAssetEditorToolkit"

FComposableCameraCameraAssetEditorToolkit::FComposableCameraCameraAssetEditorToolkit(UAssetEditor* InAssetEditor)
	: FBaseAssetToolkit(InAssetEditor)
{
	Impl = MakeShared<FComposableCameraCameraAssetEditorToolkitBase>(TEXT("ComposableCameraCameraAssetEditorLayout"));
	
}

FComposableCameraCameraAssetEditorToolkit::~FComposableCameraCameraAssetEditorToolkit()
{
}

void FComposableCameraCameraAssetEditorToolkit::SetCameraAsset(UComposableCameraCameraAsset* InCameraAsset)
{
	Impl->SetCameraAsset(InCameraAsset);
}

void FComposableCameraCameraAssetEditorToolkit::RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
}

void FComposableCameraCameraAssetEditorToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
}

void FComposableCameraCameraAssetEditorToolkit::CreateWidgets()
{
	// Create widgets
	Impl->CreateWidgets();
	DetailsView = Impl->GetDetailsView();
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
	return LOCTEXT("AppLabel", "Composable Camera Camera Asset");
}

FName FComposableCameraCameraAssetEditorToolkit::GetToolkitFName() const
{
	static FName ToolkitName("Composable Camera Camera Asset Editor");
	return ToolkitName;
}

FString FComposableCameraCameraAssetEditorToolkit::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "Composable Camera Camera Asset ").ToString();
}

FLinearColor FComposableCameraCameraAssetEditorToolkit::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.7f, 0.0f, 0.0f, 0.5f);
}

#undef LOCTEXT_NAMESPACE