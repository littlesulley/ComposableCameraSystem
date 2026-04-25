// Copyright Sulley. All Rights Reserved.

#include "ComposableCameraSystemEditorModule.h"

#include "AssetToolsModule.h"
#include "ComposableCameraEditorStyle.h"
#include "EdGraphUtilities.h"
#include "IAssetTools.h"
#include "ISequencerModule.h"
#include "AssetTools/ComposableCameraTypeAssetEditor.h"
#include "Customizations/ComposableCameraInternalVariableCustomization.h"
#include "Customizations/ComposableCameraNodeGraphNodeDetails.h"
#include "Customizations/ComposableCameraPatchSectionDetails.h"
#include "Customizations/ComposableCameraPatchTypeAssetCustomization.h"
#include "Customizations/ComposableCameraParameterTableRowCustomization.h"
#include "Customizations/ComposableCameraTypeAssetReferenceCustomization.h"
#include "EditorHooks/EditorHooks.h"
#include "Editors/ComposableCameraNodeGraphPinFactory.h"
#include "Editors/ComposableCameraGraphNodeFactory.h"
#include "Sequencer/ComposableCameraLevelSequenceComponentTrackEditor.h"
#include "Sequencer/ComposableCameraPatchTrackEditor.h"

class UComposableCameraTypeAsset;

#define LOCTEXT_NAMESPACE "FComposableCameraSystemEditorModule"

DEFINE_LOG_CATEGORY(LogComposableCameraSystemEditor);

void FComposableCameraSystemEditorModule::StartupModule()
{
#if WITH_EDITOR
    FIsSimulatingInEditor::GetIsSimulatingInEditorDelegate.BindLambda([]() -> bool
    {
        if (GEditor)
        {
            return GEditor->bIsSimulatingInEditor;
        }
        return false;
    });
#endif
    // Initialize the editor style early so ClassIcon / ClassThumbnail brushes
    // are registered before the Content Browser renders any asset tiles.
    FComposableCameraEditorStyle::Get();

    RegisterDetailsCustomizations();
    RegisterNodeGraphPinFactory();
    RegisterGraphNodeFactory();
    RegisterSequencerTrackEditor();
}

void FComposableCameraSystemEditorModule::ShutdownModule()
{
#if WITH_EDITOR
    FIsSimulatingInEditor::GetIsSimulatingInEditorDelegate.Unbind();
#endif
    UnregisterSequencerTrackEditor();
    UnregisterGraphNodeFactory();
    UnregisterNodeGraphPinFactory();
    UnregisterDetailsCustomizations();
}

UComposableCameraTypeAssetEditor* FComposableCameraSystemEditorModule::
CreateComposableCameraTypeAssetEditor(const EToolkitMode::Type Mode,
    const TSharedPtr<IToolkitHost>& InitToolkitHost, UComposableCameraTypeAsset* TypeAsset)
{
    UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    UComposableCameraTypeAssetEditor* AssetEditor = NewObject<UComposableCameraTypeAssetEditor>(AssetEditorSubsystem, NAME_None, RF_Transient);
    AssetEditor->Initialize(TypeAsset);
    return AssetEditor;
}

void FComposableCameraSystemEditorModule::RegisterDetailsCustomizations()
{
    FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

    FComposableCameraInternalVariableCustomization::Register(PropertyEditorModule);
    FComposableCameraParameterTableRowCustomization::Register(PropertyEditorModule);
    FComposableCameraNodeGraphNodeDetails::Register(PropertyEditorModule);
    FComposableCameraTypeAssetReferenceCustomization::Register(PropertyEditorModule);
    FComposableCameraPatchTypeAssetCustomization::Register(PropertyEditorModule);
    FComposableCameraPatchSectionDetails::Register(PropertyEditorModule);
}

void FComposableCameraSystemEditorModule::UnregisterDetailsCustomizations()
{
    FPropertyEditorModule* PropertyEditorModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor");

    if (PropertyEditorModule)
    {
        FComposableCameraInternalVariableCustomization::Unregister(*PropertyEditorModule);
        FComposableCameraParameterTableRowCustomization::Unregister(*PropertyEditorModule);
        FComposableCameraNodeGraphNodeDetails::Unregister(*PropertyEditorModule);
        FComposableCameraTypeAssetReferenceCustomization::Unregister(*PropertyEditorModule);
        FComposableCameraPatchTypeAssetCustomization::Unregister(*PropertyEditorModule);
        FComposableCameraPatchSectionDetails::Unregister(*PropertyEditorModule);
    }
}

void FComposableCameraSystemEditorModule::RegisterNodeGraphPinFactory()
{
    // FEdGraphUtilities holds a TWeakPtr to each registered visual pin
    // factory, so the module must retain a TSharedPtr for the factory to
    // survive past this function. Stored in NodeGraphPinFactory and reset in
    // UnregisterNodeGraphPinFactory.
    NodeGraphPinFactory = MakeShared<FComposableCameraNodeGraphPinFactory>();
    FEdGraphUtilities::RegisterVisualPinFactory(NodeGraphPinFactory);
}

void FComposableCameraSystemEditorModule::UnregisterNodeGraphPinFactory()
{
    if (NodeGraphPinFactory.IsValid())
    {
        FEdGraphUtilities::UnregisterVisualPinFactory(NodeGraphPinFactory);
        NodeGraphPinFactory.Reset();
    }
}

void FComposableCameraSystemEditorModule::RegisterGraphNodeFactory()
{
    GraphNodeFactory = MakeShared<FComposableCameraGraphNodeFactory>();
    FEdGraphUtilities::RegisterVisualNodeFactory(GraphNodeFactory);
}

void FComposableCameraSystemEditorModule::UnregisterGraphNodeFactory()
{
    if (GraphNodeFactory.IsValid())
    {
        FEdGraphUtilities::UnregisterVisualNodeFactory(GraphNodeFactory);
        GraphNodeFactory.Reset();
    }
}

void FComposableCameraSystemEditorModule::RegisterSequencerTrackEditor()
{
    ISequencerModule& SequencerModule = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer");
    LevelSequenceComponentTrackEditorHandle = SequencerModule.RegisterTrackEditor(
        FOnCreateTrackEditor::CreateStatic(
            &FComposableCameraLevelSequenceComponentTrackEditor::CreateTrackEditor));
    PatchTrackEditorHandle = SequencerModule.RegisterTrackEditor(
        FOnCreateTrackEditor::CreateStatic(
            &FComposableCameraPatchTrackEditor::CreateTrackEditor));
}

void FComposableCameraSystemEditorModule::UnregisterSequencerTrackEditor()
{
    ISequencerModule* SequencerModule = FModuleManager::GetModulePtr<ISequencerModule>("Sequencer");
    if (LevelSequenceComponentTrackEditorHandle.IsValid())
    {
        if (SequencerModule)
        {
            SequencerModule->UnRegisterTrackEditor(LevelSequenceComponentTrackEditorHandle);
        }
        LevelSequenceComponentTrackEditorHandle.Reset();
    }
    if (PatchTrackEditorHandle.IsValid())
    {
        if (SequencerModule)
        {
            SequencerModule->UnRegisterTrackEditor(PatchTrackEditorHandle);
        }
        PatchTrackEditorHandle.Reset();
    }
}

IMPLEMENT_MODULE(FComposableCameraSystemEditorModule, ComposableCameraSystemEditor)

#undef LOCTEXT_NAMESPACE