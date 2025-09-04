// Copyright Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetTypeCategories.h"
#include "Modules/ModuleManager.h"

class UComposableCameraVariableCollection;
class UComposableCameraCameraAsset;
class UComposableCameraCameraAssetEditor;
class UComposableCameraVariableCollectionEditor;
struct FComposableCameraVariablePickerConfig;

class FComposableCameraSystemEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
    UComposableCameraCameraAssetEditor* CreateComposableCameraCameraAssetEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UComposableCameraCameraAsset* CameraAsset);
    UComposableCameraVariableCollectionEditor* CreateComposableCameraVariableCollectionEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UComposableCameraVariableCollection* CameraVariableCollection);

    TSharedRef<SWidget> CreateCameraVariablePicker(const FComposableCameraVariablePickerConfig& InPickerConfig);

private:
    void RegisterDetailsCustomizations();
    void UnregisterDetailsCustomizations();
};

DECLARE_LOG_CATEGORY_EXTERN(LogComposableCameraSystemEditor, Log, All);
