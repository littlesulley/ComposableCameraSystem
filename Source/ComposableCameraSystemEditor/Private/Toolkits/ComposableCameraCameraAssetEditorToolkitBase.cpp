// Copyright Sulley. All rights reserved.


#include "Toolkits/ComposableCameraCameraAssetEditorToolkitBase.h"

FComposableCameraCameraAssetEditorToolkitBase::FComposableCameraCameraAssetEditorToolkitBase(FName InLayoutName)
{
}

FComposableCameraCameraAssetEditorToolkitBase::~FComposableCameraCameraAssetEditorToolkitBase()
{
}

void FComposableCameraCameraAssetEditorToolkitBase::RegisterTabSpawners(TSharedRef<FTabManager> InTabManager,
	TSharedPtr<FWorkspaceItem> InAssetEditorTabsCategory)
{
}

void FComposableCameraCameraAssetEditorToolkitBase::UnregisterTabSpawners(TSharedRef<FTabManager> InTabManager)
{
}

void FComposableCameraCameraAssetEditorToolkitBase::CreateWidgets()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsViewArgs.bHideSelectionTip = true;
	DetailsViewArgs.NotifyHook = this;
	DetailsView = PropertyModule.CreateDetailView(DetailsViewArgs);
}

void FComposableCameraCameraAssetEditorToolkitBase::BuildToolbarMenu(UToolMenu* ToolbarMenu)
{
}

void FComposableCameraCameraAssetEditorToolkitBase::BindCommands(TSharedRef<FUICommandList> CommandList)
{
}

void FComposableCameraCameraAssetEditorToolkitBase::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(CameraAsset);
}

FString FComposableCameraCameraAssetEditorToolkitBase::GetReferencerName() const
{
	return TEXT("FComposableCameraCameraAssetEditorToolkitBase");
}

void FComposableCameraCameraAssetEditorToolkitBase::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent,
	FProperty* PropertyThatChanged)
{
	FNotifyHook::NotifyPostChange(PropertyChangedEvent, PropertyThatChanged);
}
