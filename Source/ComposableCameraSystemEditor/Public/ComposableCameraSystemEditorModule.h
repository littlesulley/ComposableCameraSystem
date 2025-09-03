// Copyright Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetTypeCategories.h"
#include "Modules/ModuleManager.h"

class UComposableCameraCameraAsset;
class UComposableCameraCameraAssetEditor;
struct FComposableCameraVariablePickerConfig;

class FComposableCameraSystemEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
    UComposableCameraCameraAssetEditor* CreateComposableCameraCameraAssetEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UComposableCameraCameraAsset* CameraAsset);
    TSharedRef<SWidget> CreateCameraVariablePicker(const FComposableCameraVariablePickerConfig& InPickerConfig);
};

DECLARE_LOG_CATEGORY_EXTERN(LogComposableCameraSystemEditor, Log, All);
