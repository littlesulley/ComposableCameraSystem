// Copyright Sulley. All Rights Reserved.

#include "ComposableCameraSystemEditorModule.h"

#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetTools/ComposableCameraCameraAssetEditor.h"
#include "Widgets/ComposableCameraVariablePicker.h"

class UComposableCameraCameraAsset;

#define LOCTEXT_NAMESPACE "FComposableCameraSystemEditorModule"

DEFINE_LOG_CATEGORY(LogComposableCameraSystemEditor);

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

TSharedRef<SWidget> FComposableCameraSystemEditorModule::CreateCameraVariablePicker(
    const FComposableCameraVariablePickerConfig& InPickerConfig)
{
    return SNew(SComposableCameraVariablePicker)
            .ComposableCameraVariablePickerConfig(InPickerConfig);
}

IMPLEMENT_MODULE(FComposableCameraSystemEditorModule, ComposableCameraSystemEditor)

#undef LOCTEXT_NAMESPACE