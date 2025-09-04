// Copyright Sulley. All Rights Reserved.

#include "ComposableCameraSystemEditorModule.h"

#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetTools/ComposableCameraCameraAssetEditor.h"
#include "AssetTools/ComposableCameraVariableCollectionEditor.h"
#include "Customizations/ComposableCameraContextParameterDetailsCustomization.h"
#include "Widgets/ComposableCameraVariablePicker.h"

class UComposableCameraCameraAsset;

#define LOCTEXT_NAMESPACE "FComposableCameraSystemEditorModule"

DEFINE_LOG_CATEGORY(LogComposableCameraSystemEditor);

void FComposableCameraSystemEditorModule::StartupModule()
{
    RegisterDetailsCustomizations();
}

void FComposableCameraSystemEditorModule::ShutdownModule()
{
    UnregisterDetailsCustomizations();
}

UComposableCameraCameraAssetEditor* FComposableCameraSystemEditorModule::
CreateComposableCameraCameraAssetEditor(const EToolkitMode::Type Mode,
    const TSharedPtr<IToolkitHost>& InitToolkitHost, UComposableCameraCameraAsset* CameraAsset)
{
    UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    UComposableCameraCameraAssetEditor* AssetEditor = NewObject<UComposableCameraCameraAssetEditor>(AssetEditorSubsystem, NAME_None, RF_Transient);
    AssetEditor->Initialize(CameraAsset);
    return AssetEditor;
}

UComposableCameraVariableCollectionEditor* FComposableCameraSystemEditorModule::
CreateComposableCameraVariableCollectionEditor(const EToolkitMode::Type Mode,
    const TSharedPtr<IToolkitHost>& InitToolkitHost, UComposableCameraVariableCollection* CameraVariableCollection)
{
    UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    UComposableCameraVariableCollectionEditor* AssetEditor = NewObject<UComposableCameraVariableCollectionEditor>(AssetEditorSubsystem, NAME_None, RF_Transient);
    AssetEditor->Initialize(CameraVariableCollection);
    return AssetEditor;
}

TSharedRef<SWidget> FComposableCameraSystemEditorModule::CreateCameraVariablePicker(
    const FComposableCameraVariablePickerConfig& InPickerConfig)
{
    return SNew(SComposableCameraVariablePicker)
            .ComposableCameraVariablePickerConfig(InPickerConfig);
}

void FComposableCameraSystemEditorModule::RegisterDetailsCustomizations()
{
    FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

    FComposableCameraContextParameterDetailsCustomization::Register(PropertyEditorModule);
}

void FComposableCameraSystemEditorModule::UnregisterDetailsCustomizations()
{
    FPropertyEditorModule* PropertyEditorModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor");

    if (PropertyEditorModule)
    {
        FComposableCameraContextParameterDetailsCustomization::Unregister(*PropertyEditorModule);
    }
}

IMPLEMENT_MODULE(FComposableCameraSystemEditorModule, ComposableCameraSystemEditor)

#undef LOCTEXT_NAMESPACE