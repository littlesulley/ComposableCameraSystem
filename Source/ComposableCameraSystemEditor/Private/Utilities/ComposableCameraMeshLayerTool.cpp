// Copyright 2026 Sulley. All Rights Reserved.

#include "Utilities/ComposableCameraMeshLayerTool.h"

#include "ComposableCameraEditorStyle.h"
#include "Components/LineBatchComponent.h"
#include "Containers/Ticker.h"
#include "CoreGlobals.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "EditorModeRegistry.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "MeshCamera/ComposableCameraMeshLayerEdMode.h"
#include "MeshCamera/ComposableCameraMeshLayerPreviewEdMode.h"
#include "MeshCamera/ComposableCameraMeshLayerRendering.h"
#include "MeshCamera/ComposableCameraMeshSurfaceStorageActor.h"
#include "SceneManagement.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "ComposableCameraMeshLayerTool"

namespace
{
	const FName MeshLayerToolMenuOwner(TEXT("ComposableCameraMeshLayerTool"));
	FDelegateHandle MeshLayerToolStartupCallbackHandle;
	FDelegateHandle MeshLayerPrePIEEndedHandle;
	FDelegateHandle MeshLayerPostPIEStartedHandle;
	bool bMeshLayerPreviewRequested = false;
	bool bMeshLayerPIEIsEnding = false;
	FTSTicker::FDelegateHandle MeshLayerPIEPreviewTickerHandle;

	struct FMeshLayerPIEPreviewCache
	{
		FTransform StorageTransform = FTransform::Identity;
		TWeakObjectPtr<UWorld> World;
		uint32 BatchId = ULineBatchComponent::INVALID_ID;
		uint64 LastSeenTick = 0;
	};

	TMap<
		TWeakObjectPtr<AComposableCameraMeshSurfaceStorageActor>,
		FMeshLayerPIEPreviewCache> MeshLayerPIEPreviewCaches;
	uint64 MeshLayerPIEPreviewTickSerial = 0;
	uint32 NextMeshLayerPIEPreviewBatchId = 0xCC510000u;

	uint32 AllocateMeshLayerPIEPreviewBatchId()
	{
		do
		{
			++NextMeshLayerPIEPreviewBatchId;
		}
		while (NextMeshLayerPIEPreviewBatchId == ULineBatchComponent::INVALID_ID);
		return NextMeshLayerPIEPreviewBatchId;
	}

	void ReleasePIEPreviewCache(FMeshLayerPIEPreviewCache& Cache)
	{
		if (Cache.BatchId != ULineBatchComponent::INVALID_ID)
		{
			if (const UWorld* World = Cache.World.Get();
				World && !World->IsBeingCleanedUp())
			{
				if (ULineBatchComponent* LineBatcher = World->GetLineBatcher(
					UWorld::ELineBatcherType::WorldPersistent))
				{
					LineBatcher->ClearBatch(Cache.BatchId);
				}
			}
		}
		Cache.World.Reset();
		Cache.BatchId = ULineBatchComponent::INVALID_ID;
	}

	void ReleaseAllPIEPreviewCaches()
	{
		for (TPair<
			TWeakObjectPtr<AComposableCameraMeshSurfaceStorageActor>,
			FMeshLayerPIEPreviewCache>& Pair : MeshLayerPIEPreviewCaches)
		{
			ReleasePIEPreviewCache(Pair.Value);
		}
		MeshLayerPIEPreviewCaches.Reset();
	}

	void HandlePrePIEEnded(bool /*bIsSimulating*/)
	{
		// PrePIEEnded fires before EndPlayMap starts dismantling PIE worlds.
		// Clear our world-owned persistent batches here and prevent the ticker
		// from repopulating them while UWorld::FinishDestroy releases FScene.
		bMeshLayerPIEIsEnding = true;
		ReleaseAllPIEPreviewCaches();
	}

	void HandlePostPIEStarted(bool /*bIsSimulating*/)
	{
		bMeshLayerPIEIsEnding = false;
	}

	void BuildPIEPreviewCache(
		const AComposableCameraMeshSurfaceStorageActor& StorageActor,
		FMeshLayerPIEPreviewCache& OutCache)
	{
		using namespace UE::ComposableCamera::MeshEditor;
		ReleasePIEPreviewCache(OutCache);

		UWorld* World = StorageActor.GetWorld();
		if (!World || World->IsBeingCleanedUp() || World->GetNetMode() == NM_DedicatedServer)
		{
			return;
		}

		FResolvedSurfaceVisualization Visualization;
		BuildRuntimeVisualization(
			StorageActor.GetRuntimeData(),
			StorageActor.GetLayers(),
			Visualization);

		TArray<FResolvedSurfaceLayerMesh> LocalMeshes;
		BuildVisualizationMeshes(
			Visualization,
			StorageActor.GetLayers(),
			LocalMeshes);

		ULineBatchComponent* LineBatcher = World->GetLineBatcher(
			UWorld::ELineBatcherType::WorldPersistent);
		if (!LineBatcher)
		{
			return;
		}
		OutCache.World = World;
		OutCache.BatchId = AllocateMeshLayerPIEPreviewBatchId();
		OutCache.StorageTransform = StorageActor.GetActorTransform();

		for (FResolvedSurfaceLayerMesh& LocalMesh : LocalMeshes)
		{
			TArray<FVector> WorldVertices;
			WorldVertices.Reserve(LocalMesh.LocalVertices.Num());
			for (const FVector& LocalVertex : LocalMesh.LocalVertices)
			{
				WorldVertices.Add(
					OutCache.StorageTransform.TransformPosition(LocalVertex));
			}
			LineBatcher->DrawMesh(
				WorldVertices,
				LocalMesh.Indices,
				LocalMesh.Color,
				SDPG_World,
				-1.0f,
				OutCache.BatchId);
		}
	}

	bool TickMeshLayerPIEPreview(float /*DeltaTime*/)
	{
#if !UE_BUILD_SHIPPING
		using UE::ComposableCamera::MeshEditor::ShouldDrawPIEPreview;
		if (!bMeshLayerPreviewRequested || bMeshLayerPIEIsEnding || !GEngine)
		{
			return true;
		}
		++MeshLayerPIEPreviewTickSerial;

		for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
		{
			if (!ShouldDrawPIEPreview(
				bMeshLayerPreviewRequested,
				bMeshLayerPIEIsEnding,
				WorldContext.WorldType))
			{
				continue;
			}

			UWorld* World = WorldContext.World();
			if (!World || World->IsBeingCleanedUp()
				|| World->GetNetMode() == NM_DedicatedServer)
			{
				continue;
			}

			for (TActorIterator<AComposableCameraMeshSurfaceStorageActor> Iterator(World);
				Iterator;
				++Iterator)
			{
				AComposableCameraMeshSurfaceStorageActor* StorageActor = *Iterator;
				FMeshLayerPIEPreviewCache& Cache =
					MeshLayerPIEPreviewCaches.FindOrAdd(StorageActor);
				Cache.LastSeenTick = MeshLayerPIEPreviewTickSerial;
				if (Cache.BatchId == ULineBatchComponent::INVALID_ID
					|| Cache.World.Get() != World
					|| !Cache.StorageTransform.Equals(StorageActor->GetActorTransform()))
				{
					BuildPIEPreviewCache(*StorageActor, Cache);
				}
			}
		}

		for (auto CacheIterator = MeshLayerPIEPreviewCaches.CreateIterator();
			CacheIterator;
			++CacheIterator)
		{
			const AComposableCameraMeshSurfaceStorageActor* StorageActor =
				CacheIterator.Key().Get();
			if (!StorageActor
				|| CacheIterator.Value().LastSeenTick != MeshLayerPIEPreviewTickSerial)
			{
				ReleasePIEPreviewCache(CacheIterator.Value());
				CacheIterator.RemoveCurrent();
			}
		}
#endif
		return true;
	}

	void SetMeshLayerPreviewRequested(bool bRequested)
	{
		bMeshLayerPreviewRequested = bRequested;
#if !UE_BUILD_SHIPPING
		if (bRequested && !MeshLayerPIEPreviewTickerHandle.IsValid())
		{
			MeshLayerPIEPreviewTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateStatic(&TickMeshLayerPIEPreview),
				0.0f);
		}
		else if (!bRequested && MeshLayerPIEPreviewTickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(MeshLayerPIEPreviewTickerHandle);
			MeshLayerPIEPreviewTickerHandle.Reset();
		}
#endif
		if (!bRequested)
		{
			ReleaseAllPIEPreviewCaches();
		}
	}

	FSlateIcon GetMeshLayerModeIcon()
	{
		const TSharedRef<FComposableCameraEditorStyle> Style =
			FComposableCameraEditorStyle::Get();
		return FSlateIcon(
			Style->GetStyleSetName(),
			TEXT("MeshCameraLayers.Mode"),
			TEXT("MeshCameraLayers.Mode.Small"));
	}
}

void FComposableCameraMeshLayerTool::Register()
{
	if (!MeshLayerPrePIEEndedHandle.IsValid())
	{
		MeshLayerPrePIEEndedHandle = FEditorDelegates::PrePIEEnded.AddStatic(
			&HandlePrePIEEnded);
	}
	if (!MeshLayerPostPIEStartedHandle.IsValid())
	{
		MeshLayerPostPIEStartedHandle = FEditorDelegates::PostPIEStarted.AddStatic(
			&HandlePostPIEStarted);
	}

	FEditorModeRegistry::Get().RegisterMode<FComposableCameraMeshLayerEdMode>(
		FComposableCameraMeshLayerEdMode::ModeId,
		LOCTEXT("EditModeName", "Mesh Camera Layers"),
		GetMeshLayerModeIcon(),
		true,
		750);
	FEditorModeRegistry::Get().RegisterMode<FComposableCameraMeshLayerPreviewEdMode>(
		FComposableCameraMeshLayerPreviewEdMode::ModeId,
		LOCTEXT("PreviewModeName", "Mesh Camera Layer Preview"),
		GetMeshLayerModeIcon(),
		false,
		751);

	if (UToolMenus::IsToolMenuUIEnabled())
	{
		MeshLayerToolStartupCallbackHandle = UToolMenus::RegisterStartupCallback(
			FSimpleMulticastDelegate::FDelegate::CreateStatic(
				&FComposableCameraMeshLayerTool::RegisterMenus));
	}
}

void FComposableCameraMeshLayerTool::Unregister()
{
	SetMeshLayerPreviewRequested(false);
	if (MeshLayerPrePIEEndedHandle.IsValid())
	{
		FEditorDelegates::PrePIEEnded.Remove(MeshLayerPrePIEEndedHandle);
		MeshLayerPrePIEEndedHandle.Reset();
	}
	if (MeshLayerPostPIEStartedHandle.IsValid())
	{
		FEditorDelegates::PostPIEStarted.Remove(MeshLayerPostPIEStartedHandle);
		MeshLayerPostPIEStartedHandle.Reset();
	}
	if (IsEditModeActive())
	{
		GLevelEditorModeTools().DeactivateMode(FComposableCameraMeshLayerEdMode::ModeId);
	}
	if (IsPreviewModeActive())
	{
		GLevelEditorModeTools().DeactivateMode(FComposableCameraMeshLayerPreviewEdMode::ModeId);
	}

	if (MeshLayerToolStartupCallbackHandle.IsValid())
	{
		UToolMenus::UnRegisterStartupCallback(MeshLayerToolStartupCallbackHandle);
		MeshLayerToolStartupCallbackHandle.Reset();
	}
	if (UToolMenus::IsToolMenuUIEnabled())
	{
		UToolMenus::Get()->UnregisterOwnerByName(MeshLayerToolMenuOwner);
	}

	FEditorModeRegistry::Get().UnregisterMode(FComposableCameraMeshLayerPreviewEdMode::ModeId);
	FEditorModeRegistry::Get().UnregisterMode(FComposableCameraMeshLayerEdMode::ModeId);
}

void FComposableCameraMeshLayerTool::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(MeshLayerToolMenuOwner);
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
	if (!Menu)
	{
		return;
	}

	FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("ComposableCameraSystem"));
	Section.Label = LOCTEXT("Section", "Composable Camera System");
	Section.AddMenuEntry(
		TEXT("ComposableCameraMeshLayerEdit"),
		LOCTEXT("Edit", "Edit Mesh Camera Layers"),
		LOCTEXT("EditTooltip", "Open the Level-local mesh camera layer painting tool."),
		GetMeshLayerModeIcon(),
		FUIAction(
			FExecuteAction::CreateStatic(&FComposableCameraMeshLayerTool::ToggleEditMode),
			FCanExecuteAction(),
			FIsActionChecked::CreateStatic(&FComposableCameraMeshLayerTool::IsEditModeActive)),
		EUserInterfaceActionType::ToggleButton);
	Section.AddMenuEntry(
		TEXT("ComposableCameraMeshLayerPreview"),
		LOCTEXT("Preview", "Show Mesh Camera Layers"),
		LOCTEXT("PreviewTooltip", "Toggle read-only rendering for all loaded mesh camera layers, including PIE."),
		GetMeshLayerModeIcon(),
		FUIAction(
			FExecuteAction::CreateStatic(&FComposableCameraMeshLayerTool::TogglePreviewMode),
			FCanExecuteAction(),
			FIsActionChecked::CreateStatic(&FComposableCameraMeshLayerTool::IsPreviewModeActive)),
		EUserInterfaceActionType::ToggleButton);
}

void FComposableCameraMeshLayerTool::ToggleEditMode()
{
	if (IsEngineExitRequested())
	{
		return;
	}

	if (IsEditModeActive())
	{
		GLevelEditorModeTools().DeactivateMode(FComposableCameraMeshLayerEdMode::ModeId);
		return;
	}

	if (IsPreviewModeActive())
	{
		GLevelEditorModeTools().DeactivateMode(FComposableCameraMeshLayerPreviewEdMode::ModeId);
		SetMeshLayerPreviewRequested(false);
	}
	GLevelEditorModeTools().ActivateMode(FComposableCameraMeshLayerEdMode::ModeId);
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

void FComposableCameraMeshLayerTool::TogglePreviewMode()
{
	if (IsEngineExitRequested())
	{
		return;
	}

	if (IsPreviewModeActive())
	{
		GLevelEditorModeTools().DeactivateMode(FComposableCameraMeshLayerPreviewEdMode::ModeId);
		SetMeshLayerPreviewRequested(false);
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports();
		}
		return;
	}

	if (IsEditModeActive())
	{
		GLevelEditorModeTools().DeactivateMode(FComposableCameraMeshLayerEdMode::ModeId);
	}
	SetMeshLayerPreviewRequested(true);
	GLevelEditorModeTools().ActivateMode(FComposableCameraMeshLayerPreviewEdMode::ModeId);
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

bool FComposableCameraMeshLayerTool::IsEditModeActive()
{
	return !IsEngineExitRequested()
		&& GLevelEditorModeTools().IsModeActive(FComposableCameraMeshLayerEdMode::ModeId);
}

bool FComposableCameraMeshLayerTool::IsPreviewModeActive()
{
	return !IsEngineExitRequested()
		&& (bMeshLayerPreviewRequested
			|| GLevelEditorModeTools().IsModeActive(
				FComposableCameraMeshLayerPreviewEdMode::ModeId));
}

#undef LOCTEXT_NAMESPACE
