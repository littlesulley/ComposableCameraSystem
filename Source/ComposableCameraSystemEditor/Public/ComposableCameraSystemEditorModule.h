// Copyright Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetTypeCategories.h"
#include "Modules/ModuleManager.h"

class UComposableCameraTypeAsset;
class UComposableCameraTypeAssetEditor;
class FComposableCameraNodeGraphPinFactory;
class FComposableCameraGraphNodeFactory;

class FComposableCameraSystemEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
    UComposableCameraTypeAssetEditor* CreateComposableCameraTypeAssetEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UComposableCameraTypeAsset* TypeAsset);
private:
    void RegisterDetailsCustomizations();
    void UnregisterDetailsCustomizations();

    /** Registers FComposableCameraNodeGraphPinFactory with FEdGraphUtilities so
     *  exposed input pins on UComposableCameraNodeGraphNode get the greyed-out
     *  SComposableCameraExposedPin widget instead of the default SGraphPin. */
    void RegisterNodeGraphPinFactory();
    void UnregisterNodeGraphPinFactory();

    /** Registers FComposableCameraGraphNodeFactory with FEdGraphUtilities so
     *  UComposableCameraNodeGraphNode instances get the custom
     *  SComposableCameraGraphNode widget that renders debug overlays during PIE. */
    void RegisterGraphNodeFactory();
    void UnregisterGraphNodeFactory();

    /** Kept alive for the lifetime of the module so FEdGraphUtilities' weak
     *  reference stays resolvable. Reset in ShutdownModule after unregister. */
    TSharedPtr<FComposableCameraNodeGraphPinFactory> NodeGraphPinFactory;

    /** Kept alive for the lifetime of the module so the node factory registration
     *  stays valid. Reset in ShutdownModule after unregister. */
    TSharedPtr<FComposableCameraGraphNodeFactory> GraphNodeFactory;
};

DECLARE_LOG_CATEGORY_EXTERN(LogComposableCameraSystemEditor, Log, All);
