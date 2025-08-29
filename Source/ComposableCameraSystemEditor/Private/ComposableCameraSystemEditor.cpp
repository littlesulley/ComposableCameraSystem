#include "ComposableCameraSystemEditor.h"
#include "AssetTools/ComposableCameraCameraAssetEditor.h"

#define LOCTEXT_NAMESPACE "FComposableCameraSystemEditorModule"

void FComposableCameraSystemEditorModule::StartupModule()
{
    
}

void FComposableCameraSystemEditorModule::ShutdownModule()
{
    
}

UComposableCameraCameraAssetEditor* FComposableCameraSystemEditorModule::CreateComposableCameraCameraAssetEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UComposableCameraCameraAsset* CameraAsset)
{
    UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    UComposableCameraCameraAssetEditor* AssetEditor = NewObject<UComposableCameraCameraAssetEditor>(AssetEditorSubsystem, NAME_None, RF_Transient);
    AssetEditor->Initialize(CameraAsset);
    return AssetEditor;
}

#undef LOCTEXT_NAMESPACE
    
IMPLEMENT_MODULE(FComposableCameraSystemEditorModule, ComposableCameraSystemEditor)