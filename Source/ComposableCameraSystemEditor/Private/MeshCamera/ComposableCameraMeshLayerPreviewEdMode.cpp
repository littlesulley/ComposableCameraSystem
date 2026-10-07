// Copyright 2026 Sulley. All Rights Reserved.

#include "MeshCamera/ComposableCameraMeshLayerPreviewEdMode.h"

#include "EngineUtils.h"
#include "CoreGlobals.h"
#include "Editor.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "LevelEditorViewport.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "MeshCamera/ComposableCameraMeshLayerEdMode.h"
#include "MeshCamera/ComposableCameraMeshLayerRendering.h"
#include "MeshCamera/ComposableCameraMeshSurfaceStorageActor.h"

const FEditorModeID FComposableCameraMeshLayerPreviewEdMode::ModeId =
	TEXT("EM_ComposableCameraMeshLayerPreview");

void FComposableCameraMeshLayerPreviewEdMode::Enter()
{
	FEdMode::Enter();
	ResetPreviews();
}

void FComposableCameraMeshLayerPreviewEdMode::Exit()
{
	ResetPreviews();
	FEdMode::Exit();
}

bool FComposableCameraMeshLayerPreviewEdMode::IsCompatibleWith(FEditorModeID OtherModeID) const
{
	return OtherModeID != FComposableCameraMeshLayerEdMode::ModeId;
}

void FComposableCameraMeshLayerPreviewEdMode::ResetPreviews()
{
	for (auto& Pair : Previews)
	{
		Pair.Value.Build.Reset();
		UE::ComposableCamera::MeshEditor::DestroyPIEPreviewActor(Pair.Value.PreviewActor.Get());
	}
	Previews.Empty();
	WorkOrder.Empty();
	NextPublicationJob = 0;
	LastTickFrame = MAX_uint64;
	PendingRedrawFrames = 0;
}

void FComposableCameraMeshLayerPreviewEdMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	FEdMode::Tick(ViewportClient, DeltaTime);
	AdvancePreview();
}

void FComposableCameraMeshLayerPreviewEdMode::AdvancePreview()
{
	if (LastTickFrame == GFrameCounter) { return; }
	LastTickFrame = GFrameCounter;
	TRACE_CPUPROFILER_EVENT_SCOPE(CCS_MeshLayers_EditorPreviewTick);
	using namespace UE::ComposableCamera::MeshEditor;
	PumpPreviewGeometryBuilds();
	UWorld* World = GetWorld();
	if (!World || World->WorldType != EWorldType::Editor || World->IsBeingCleanedUp())
	{
		ResetPreviews();
		return;
	}
	if (GEditor && GEditor->PlayWorld)
	{
		// Give the owned worker to the active PIE worlds. Otherwise a cold,
		// unfinished editor document can queue every PIE request behind it.
		// Keep already displayed editor components; restart interrupted documents
		// on return, without allowing abandoned batches to reach their caches.
		for (auto& Pair : Previews)
		{
			if (Pair.Value.Build.IsPending())
			{
				Pair.Value.Build.Reset(); Pair.Value.ReadyMeshes.Empty(); Pair.Value.NextMesh = 0;
				Pair.Value.bRestartAfterPIE = true;
			}
		}
		return;
	}

	for (TActorIterator<AComposableCameraMeshSurfaceStorageActor> Iterator(World); Iterator; ++Iterator)
	{
		AComposableCameraMeshSurfaceStorageActor* StorageActor = *Iterator;
		const TWeakObjectPtr<AComposableCameraMeshSurfaceStorageActor> StorageKey(StorageActor);
		FPreviewCache* Cache = Previews.Find(StorageKey);
		if (Cache && Cache->PreviewActor.IsStale())
		{
			Previews.Remove(StorageKey);
			Cache = nullptr;
		}
		if (!Cache || Cache->bRestartAfterPIE || Cache->DataRevision != StorageActor->GetEditorDataRevision())
		{
			if (!Cache) { Cache = &Previews.Add(StorageKey); }
			else
			{
				Cache->Build.Reset(); Cache->ReadyMeshes.Empty(); Cache->NextMesh = 0;
				DestroyPIEPreviewActor(Cache->PreviewActor.Get()); Cache->PreviewActor.Reset();
				Cache->bRestartAfterPIE = false;
			}
			const FLevelEditorViewportClient* View = GCurrentLevelEditingViewportClient
				? GCurrentLevelEditingViewportClient : GLastKeyLevelEditingViewportClient;
			const FVector Focus = View && View->GetWorld() == World
				? StorageActor->GetActorTransform().InverseTransformPosition(View->GetViewLocation()) : FVector::ZeroVector;
			Cache->DataRevision = StorageActor->GetEditorDataRevision();
			Cache->Build.Begin(StorageActor->GetRuntimeData(), StorageActor->GetLayers(), true, true, Focus, &StorageActor->GetEditorPreview());
		}
		Cache->LastSeenFrame = LastTickFrame;
		FPreviewGeometryBuildResult Result;
		for (int32 Batch = 0; Batch < PreviewPublicationChunkLimit
			&& Cache->ReadyMeshes.Num() - Cache->NextMesh < PreviewPublicationChunkLimit
			&& Cache->Build.TakeResult(Result); ++Batch)
		{
			// Never replace unsubmitted geometry when a later spatial tile arrives.
			Cache->ReadyMeshes.Append(MoveTemp(Result.NativeMeshes));
		}
		if (AActor* PreviewActor = Cache->PreviewActor.Get())
		{
			UpdatePIEPreviewTransform(*PreviewActor, StorageActor->GetActorTransform());
		}
	}
	for (auto It = Previews.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || It.Value().LastSeenFrame != LastTickFrame)
		{
			It.Value().Build.Reset();
			DestroyPIEPreviewActor(It.Value().PreviewActor.Get());
			It.RemoveCurrent();
		}
	}
	// Shared across viewports. Capacity and geometry remain unchanged on stable frames.
	WorkOrder.Reset();
	if (WorkOrder.Max() < Previews.Num()) { WorkOrder.Reserve(Previews.Num()); }
	for (const auto& Pair : Previews) { WorkOrder.Add(Pair.Key); }
	bool bPublishedGeometry = false;
	FPreviewPublicationBudget Budget(FPlatformTime::Seconds());
	RunPIEPreviewWorkRoundRobin(WorkOrder.Num(), NextPublicationJob, [&](int32 JobIndex)
	{
		if (!Budget.CanAdvance(FPlatformTime::Seconds()))
		{
			return EPIEPreviewWorkResult::BudgetExhausted;
		}
		const auto Key = WorkOrder[JobIndex];
		FPreviewCache* Cache = Previews.Find(Key);
		AComposableCameraMeshSurfaceStorageActor* Storage = Key.Get();
		if (!Cache || !Storage || Cache->NextMesh >= Cache->ReadyMeshes.Num()) { return EPIEPreviewWorkResult::Skipped; }
		AActor* Preview = AppendNativePreviewMeshes(*Storage, Cache->PreviewActor.Get(),
			MakeArrayView(Cache->ReadyMeshes.GetData() + Cache->NextMesh, 1), true);
		Budget.Consume();
		if (Preview)
		{
			bPublishedGeometry = true;
			Cache->PreviewActor = Preview;
			++Cache->NextMesh;
			if (Cache->NextMesh == Cache->ReadyMeshes.Num()) { Cache->ReadyMeshes.Empty(); Cache->NextMesh = 0; }
		}
		return EPIEPreviewWorkResult::Advanced;
	});
	// Registration/end-of-frame render updates can land after the current editor
	// draw. Request subsequent static-view draws too; stable completed caches stop.
	if (bPublishedGeometry) { PendingRedrawFrames = 2; }
	if (PendingRedrawFrames > 0 && GEditor)
	{
		GEditor->RedrawLevelEditingViewports(false);
		if (!bPublishedGeometry) { --PendingRedrawFrames; }
	}
}
