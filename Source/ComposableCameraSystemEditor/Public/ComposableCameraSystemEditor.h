#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class UComposableCameraCameraAsset;
class UComposableCameraCameraAssetEditor;

class FComposableCameraSystemEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
    UComposableCameraCameraAssetEditor* CreateComposableCameraCameraAssetEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UComposableCameraCameraAsset* CameraAsset);
};
