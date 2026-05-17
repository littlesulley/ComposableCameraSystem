// Copyright 2026 Sulley. All Rights Reserved.

#include "ComposableCameraSystemEditorModule.h"

#include "AssetToolsModule.h"
#include "Editor.h"
#include "GameFramework/Actor.h"
#include "ISequencer.h"
#include "MovieSceneSequencePlayer.h"
#include "MovieSceneSpawnableAnnotation.h"
#include "MovieSceneSpawnRegister.h"
#include "UnrealClient.h"
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
#include "Customizations/ComposableCameraShotAnchorIndexCustomization.h"
#include "Customizations/ComposableCameraShotPlacementCustomization.h"
#include "Customizations/ComposableCameraShotSectionDetails.h"
#include "Customizations/ComposableCameraTargetInfoCustomization.h"
#include "Customizations/ComposableCameraTypeAssetReferenceCustomization.h"
#include "EditorHooks/EditorHooks.h"
#include "Editors/ComposableCameraNodeGraphPinFactory.h"
#include "Editors/ComposableCameraGraphNodeFactory.h"
#include "Editors/ComposableCameraShotEditor.h"
#include "Sequencer/ComposableCameraLevelSequenceComponentTrackEditor.h"
#include "Sequencer/ComposableCameraPatchTrackEditor.h"
#include "Sequencer/ComposableCameraShotTrackEditor.h"
#include "Utilities/ComposableCameraLevelSequenceSpawnTrackTool.h"
#include "Utilities/ComposableCameraViewportTransformClipboard.h"

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

 // Editor active-viewport size resolver - lets `TryGetEffectiveViewportSize`
 // (runtime module) return the actual editor-scrub viewport dimensions
 // instead of falling back to 1920x1080. Without this, the Composition
 // Solver runs with a wrong aspect during editor scrub of LS Spawnables,
 // and anchor screen positions drift between Shot Editor preview and
 // LS playback. Pulls from the active level viewport (perspective
 // viewport when scrubbing through Sequencer in editor).
 FGetActiveEditorViewport::GetSizeDelegate.BindLambda([](FIntPoint& OutSize) -> bool
 {
 if (!GEditor)
 {
 return false;
 }
 FViewport* Viewport = GEditor->GetActiveViewport();
 if (!Viewport)
 {
 return false;
 }
 const FIntPoint Size = Viewport->GetSizeXY();
 if (Size.X <= 0 || Size.Y <= 0)
 {
 return false;
 }
 OutSize = Size;
 return true;
 });

 FGetEditorSequencerPlaybackDeltaTime::GetDeltaTimeDelegate.BindLambda(
 [this](const AActor* SpawnedActor, float WorldDeltaTime, float& OutDeltaTime) -> bool
 {
 if (!SpawnedActor)
 {
 return false;
 }

 const TOptional<FMovieSceneSpawnableAnnotation> Annotation =
 FMovieSceneSpawnableAnnotation::Find(const_cast<AActor*>(SpawnedActor));
 if (!Annotation.IsSet())
 {
 return false;
 }

 for (int32 i = ActiveSequencers.Num() - 1; i >= 0; --i)
 {
 TSharedPtr<ISequencer> Live = ActiveSequencers[i].Pin();
 if (!Live.IsValid())
 {
 ActiveSequencers.RemoveAtSwap(i, EAllowShrinking::No);
 continue;
 }

 UObject* BoundSpawnedObject = Live->GetSpawnRegister()
 .FindSpawnedObject(Annotation->ObjectBindingID, Annotation->SequenceID)
 .Get();
 if (BoundSpawnedObject != SpawnedActor)
 {
 continue;
 }

 OutDeltaTime = Live->GetPlaybackStatus() == EMovieScenePlayerStatus::Playing
 ? WorldDeltaTime * FMath::Abs(Live->GetPlaybackSpeed())
 : 0.f;
 return true;
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
 FComposableCameraLevelSequenceSpawnTrackTool::Register();
 FComposableCameraViewportTransformClipboard::Register();

 // Shot Editor (Phase D.1+) - registers the nomad tab spawner with
 // FGlobalTabmanager + binds the runtime-side FOpenShotEditor delegate
 // so node CallInEditor buttons route into OpenForShot.
 FComposableCameraShotEditor::RegisterTabSpawner();

 // Cache every Sequencer that opens so the Shot Editor's preview can
 // resolve per-section TargetActorOverrides (FMovieSceneObjectBindingID)
 // through the live Sequencer instance for the section's sequence.
 {
 ISequencerModule& SequencerModule = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer");
 SequencerCreatedHandle = SequencerModule.RegisterOnSequencerCreated(FOnSequencerCreated::FDelegate::CreateRaw(this, &FComposableCameraSystemEditorModule::OnSequencerCreated));
 }
}

void FComposableCameraSystemEditorModule::ShutdownModule()
{
#if WITH_EDITOR
 FIsSimulatingInEditor::GetIsSimulatingInEditorDelegate.Unbind();
 FGetActiveEditorViewport::GetSizeDelegate.Unbind();
 FGetEditorSequencerPlaybackDeltaTime::GetDeltaTimeDelegate.Unbind();
#endif
 if (SequencerCreatedHandle.IsValid())
 {
 if (ISequencerModule* SequencerModule = FModuleManager::GetModulePtr<ISequencerModule>("Sequencer"))
 {
 SequencerModule->UnregisterOnSequencerCreated(SequencerCreatedHandle);
 }
 SequencerCreatedHandle.Reset();
 }
 ActiveSequencers.Reset();

 FComposableCameraShotEditor::UnregisterTabSpawner();
 FComposableCameraViewportTransformClipboard::Unregister();
 FComposableCameraLevelSequenceSpawnTrackTool::Unregister();
 UnregisterSequencerTrackEditor();
 UnregisterGraphNodeFactory();
 UnregisterNodeGraphPinFactory();
 UnregisterDetailsCustomizations();
}

void FComposableCameraSystemEditorModule::OnSequencerCreated(TSharedRef<ISequencer> InSequencer)
{
 ActiveSequencers.Add(InSequencer);
 UE_LOG(LogComposableCameraSystemEditor, Log,
 TEXT("Cached new Sequencer (focused sequence: %s). Total tracked: %d."),
 InSequencer->GetFocusedMovieSceneSequence()
 ? *InSequencer->GetFocusedMovieSceneSequence()->GetName()
 : TEXT("<none>"),
 ActiveSequencers.Num());
}

TSharedPtr<ISequencer> FComposableCameraSystemEditorModule::FindOpenSequencerForSequence(UMovieSceneSequence* Sequence) const
{
 if (!Sequence)
 {
 return nullptr;
 }

 // Walk the cache and prune dead refs in the same pass - Sequencer windows
 // close without notifying us, leaving stale weak ptrs.
 TSharedPtr<ISequencer> Hit;
 int32 LiveCount = 0;
 for (int32 i = ActiveSequencers.Num() - 1; i >= 0; --i)
 {
 TSharedPtr<ISequencer> Live = ActiveSequencers[i].Pin();
 if (!Live.IsValid())
 {
 ActiveSequencers.RemoveAtSwap(i, EAllowShrinking::No);
 continue;
 }
 ++LiveCount;
 if (!Hit.IsValid())
 {
 // Prefer the focused sequence - handles sub-sequence sub-sections
 // correctly. Falls back to root sequence equality otherwise.
 if (Live->GetFocusedMovieSceneSequence() == Sequence
 || Live->GetRootMovieSceneSequence() == Sequence)
 {
 Hit = Live;
 }
 }
 }

 UE_LOG(LogComposableCameraSystemEditor, Verbose,
 TEXT("FindOpenSequencerForSequence('%s'): %d live Sequencers tracked -> %s."),
 *Sequence->GetName(), LiveCount,
 Hit.IsValid() ? TEXT("HIT") : TEXT("MISS (no Sequencer is focused on this sequence)"));

 return Hit;
}

TArray<TSharedPtr<ISequencer>> FComposableCameraSystemEditorModule::GetLiveSequencers() const
{
 TArray<TSharedPtr<ISequencer>> LiveSequencers;
 for (int32 i = ActiveSequencers.Num() - 1; i >= 0; --i)
 {
 TSharedPtr<ISequencer> Live = ActiveSequencers[i].Pin();
 if (!Live.IsValid())
 {
 ActiveSequencers.RemoveAtSwap(i, EAllowShrinking::No);
 continue;
 }

 LiveSequencers.Add(Live);
 }

 return LiveSequencers;
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
 FComposableCameraShotSectionDetails::Register(PropertyEditorModule);
 FComposableCameraTargetInfoCustomization::Register(PropertyEditorModule);
 FShotPlacementCustomization::Register(PropertyEditorModule);
 FShotAnchorIndexCustomization::Register(PropertyEditorModule);
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
 FComposableCameraShotSectionDetails::Unregister(*PropertyEditorModule);
 FComposableCameraTargetInfoCustomization::Unregister(*PropertyEditorModule);
 FShotPlacementCustomization::Unregister(*PropertyEditorModule);
 FShotAnchorIndexCustomization::Unregister(*PropertyEditorModule);
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
 LevelSequenceComponentTrackEditorHandle = SequencerModule.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(
 &FComposableCameraLevelSequenceComponentTrackEditor::CreateTrackEditor));
 PatchTrackEditorHandle = SequencerModule.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(
 &FComposableCameraPatchTrackEditor::CreateTrackEditor));
 ShotTrackEditorHandle = SequencerModule.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(
 &FComposableCameraShotTrackEditor::CreateTrackEditor));
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
 if (ShotTrackEditorHandle.IsValid())
 {
 if (SequencerModule)
 {
 SequencerModule->UnRegisterTrackEditor(ShotTrackEditorHandle);
 }
 ShotTrackEditorHandle.Reset();
 }
}

IMPLEMENT_MODULE(FComposableCameraSystemEditorModule, ComposableCameraSystemEditor)

#undef LOCTEXT_NAMESPACE
