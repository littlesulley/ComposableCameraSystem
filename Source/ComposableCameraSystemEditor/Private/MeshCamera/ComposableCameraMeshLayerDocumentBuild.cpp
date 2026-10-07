// Copyright 2026 Sulley. All Rights Reserved.

#include "MeshCamera/ComposableCameraMeshLayerDocumentBuild.h"
#include "MeshCamera/ComposableCameraMeshLayerSavedPreview.h"

#include "MeshCamera/ComposableCameraMeshLayerEditWork.h"
#include "Containers/Queue.h"
#include "MeshCamera/ComposableCameraMeshSurfaceTypes.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace UE::ComposableCamera::MeshEditor
{
	struct FDocumentPreviewState
	{
		TQueue<FPreparedEditPreview, EQueueMode::Mpsc> Ready;
		TQueue<FDocumentPreviewIndex, EQueueMode::Spsc> IndexReady;
		std::atomic_bool Alive { true }, Cancelled { false };
		void Cancel() { Alive.store(false, std::memory_order_relaxed); Cancelled.store(true, std::memory_order_relaxed); }
	};
	struct FDocumentPreviewWork
	{
		TFuture<FDocumentPreviewResult> Future;
		FGuid Revision;
		bool bResident = false;
		TSharedRef<FDocumentPreviewState, ESPMode::ThreadSafe> State = MakeShared<FDocumentPreviewState, ESPMode::ThreadSafe>();
		~FDocumentPreviewWork() { State->Cancel(); }
	};
	FMeshLayerDocumentBuild::FMeshLayerDocumentBuild() = default;
	FMeshLayerDocumentBuild::~FMeshLayerDocumentBuild() { Cancel(); }
	bool FMeshLayerDocumentBuild::IsResident() const { return Work && Work->bResident; }
	FGuid FMeshLayerDocumentBuild::GetRevision() const { return Work ? Work->Revision : FGuid(); }
	void FMeshLayerDocumentBuild::Wait() { if (Work) { Work->Future.Wait(); } }
	bool FMeshLayerDocumentBuild::TakeIndex(const FGuid& Revision, FDocumentPreviewIndex& Out)
	{ return Work && Work->Revision == Revision && Work->State->IndexReady.Dequeue(Out); }
	void FMeshLayerDocumentBuild::RetagRevision(const FGuid& Revision) { if (Work) { Work->Revision = Revision; } }
	void FMeshLayerDocumentBuild::Cancel()
	{
		if (!Work) { return; }
		Work->State->Cancel();
		// A completed, unconsumed result can own hundreds of thousands of polygons.
		// Retire it off-thread too; cancellation must not become a bulk-free stall.
		UE::ComposableCamera::MeshEditor::AsyncMeshLayerEdit( [Retired = MoveTemp(Work)]() mutable { Retired.Reset(); });
	}

	void FMeshLayerDocumentBuild::StartResident(const FComposableCameraMeshSurfaceAuthoringData& Data,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers, const FGuid& Revision,
		const FComposableCameraMeshSurfaceEditorPreview* Saved, FResolvedSurfaceVisualization* RetiredCache, bool bPublishPreview)
	{
		Cancel(); Work = MakeUnique<FDocumentPreviewWork>(); Work->Revision = Revision; Work->bResident = true;
		FComposableCameraMeshSurfaceAuthoringData Source;
		Source.Vertices = Data.Vertices; Source.Indices = Data.Indices; Source.TriangleLayerIds = Data.TriangleLayerIds;
		TArray<FComposableCameraMeshLayerDefinition> Options; Options.SetNum(Layers.Num());
		for (int32 Index = 0; Index < Layers.Num(); ++Index)
		{ Options[Index].LayerId = Layers[Index].LayerId; Options[Index].bEnabled = Layers[Index].bEnabled; }
		FComposableCameraMeshSurfaceEditorPreview Cache; if (Saved && Saved->Version == 1) { Cache = *Saved; }
		FResolvedSurfaceVisualization Retired; if (RetiredCache) { Retired = MoveTemp(*RetiredCache); }
		Work->Future = AsyncMeshLayerEdit([Source = MoveTemp(Source), Options = MoveTemp(Options), Saved = MoveTemp(Cache),
			Retired = MoveTemp(Retired), Job = Work->State, bPublishPreview]() mutable
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(CCS_MeshLayers_ResidentDocumentWorker);
			FDocumentPreviewResult Result;
			if (!Job->Alive.load(std::memory_order_relaxed)) { return Result; }
			Result.Index.Build(Source);
			const bool bLoaded = LoadEditorPreview(Saved, Options, Result.Visualization);
			const double CellSize = bLoaded ? Result.Visualization.CellSize : GetAuthoringVisualizationCellSize(Result.Index.GetProjectedBounds(Options));
			// Release the native broad phase before expensive legacy coverage; Brush never rebuilds it on the editor thread.
			if (!Job->Alive.load(std::memory_order_relaxed)) { return Result; }
			Job->IndexReady.Enqueue({Result.Index, CellSize});
			if (!bLoaded)
			{
				FAuthoringVisualizationUpdate Update; Update.Begin(Source, Options, Result.Visualization, nullptr, &Result.Index);
				while (Job->Alive.load(std::memory_order_relaxed) && !Update.Advance(Source, Options, Result.Visualization, 256)) {}
			}
			if (!Job->Alive.load(std::memory_order_relaxed)) { return Result; }
			if (bPublishPreview)
			{
				auto Prepared = PrepareEditPreview(Result.Visualization, Options, &Job->Alive, nullptr, true);
				if (Job->Alive.load(std::memory_order_relaxed)) { Job->Ready.Enqueue(MoveTemp(Prepared)); }
			}
			Retired.Reset(); return Result;
		});
	}

	void FMeshLayerDocumentBuild::Start(const FComposableCameraMeshSurfaceAuthoringData& Data,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers, const FGuid& Revision,
		FResolvedSurfaceVisualization* RetiredCache, const FVector2D& LocalFocus, bool bPublishPreview)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CCS_MeshLayers_EditDocumentSnapshot);
		Cancel();
		// Keep original indices/vertex counts for the returned broad phase. Shapes,
		// masks, surface ownership and Profile references are not used by this cache.
		FComposableCameraMeshSurfaceAuthoringData Source;
		Source.Vertices = Data.Vertices; Source.Indices = Data.Indices; Source.TriangleLayerIds = Data.TriangleLayerIds;
		TArray<FComposableCameraMeshLayerDefinition> Options; Options.SetNum(Layers.Num());
		for (int32 Index = 0; Index < Layers.Num(); ++Index)
		{
			Options[Index].LayerId = Layers[Index].LayerId; Options[Index].bEnabled = Layers[Index].bEnabled;
		}
		Work = MakeUnique<FDocumentPreviewWork>(); Work->Revision = Revision;
		FResolvedSurfaceVisualization Retired;
		if (RetiredCache) { Retired = MoveTemp(*RetiredCache); }
		Work->Future = UE::ComposableCamera::MeshEditor::AsyncMeshLayerEdit(
			[Source = MoveTemp(Source), Options = MoveTemp(Options), Retired = MoveTemp(Retired), Job = Work->State, LocalFocus, bPublishPreview]() mutable
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(CCS_MeshLayers_EditDocumentWorker);
			FDocumentPreviewResult Result;
			if (!Job->Alive.load(std::memory_order_relaxed)) { return Result; }
			FComposableCameraMeshSurfaceRuntimeData Triangles;
			Triangles.Vertices = MoveTemp(Source.Vertices); Triangles.Indices = MoveTemp(Source.Indices);
			TMap<FGuid, int32> LayerIndices;
			for (int32 Index = 0; Index < Options.Num(); ++Index) { LayerIndices.Add(Options[Index].LayerId, Index); }
			Triangles.TriangleLayerIndices.Reserve(Source.TriangleLayerIds.Num());
			for (const FGuid& Id : Source.TriangleLayerIds)
			{
				if (!Job->Alive.load(std::memory_order_relaxed)) { return Result; }
				const int32* Layer = LayerIndices.Find(Id); Triangles.TriangleLayerIndices.Add(Layer ? *Layer : INDEX_NONE);
			}
			FResolvedSurfaceVisualization CurrentTile;
			FIntPoint CurrentKey(0, 0); bool bHasTile = false;
			BuildRuntimeVisualizationTiles(Triangles, Options, LocalFocus, [&](FResolvedSurfaceVisualization&& Batch)
			{
				if (!Job->Alive.load(std::memory_order_relaxed) || Batch.Cells.IsEmpty()) { return; }
				if (bPublishPreview)
				{
					const double TileSize = Batch.CellSize * 32.0;
					const FIntPoint Key(FMath::FloorToInt(Batch.Cells[0].LocalPosition.X / TileSize), FMath::FloorToInt(Batch.Cells[0].LocalPosition.Y / TileSize));
					if (!bHasTile || Key != CurrentKey) { CurrentTile.Reset(); CurrentKey = Key; bHasTile = true; }
					CurrentTile.CellSize = Batch.CellSize; CurrentTile.LocalBounds = Batch.LocalBounds;
					// Combine a tiny first region and its disjoint remainder before replacing their common GPU tile.
					CurrentTile.Cells.Append(Batch.Cells);
					auto Prepared = PrepareEditPreview(CurrentTile, Options, &Job->Alive);
					if (!Job->Alive.load(std::memory_order_relaxed)) { return; }
					Job->Ready.Enqueue(MoveTemp(Prepared));
				}
				const int32 Base = Result.Visualization.Cells.Num();
				Result.Visualization.CellSize = Batch.CellSize; Result.Visualization.LocalBounds = Batch.LocalBounds;
				Result.Visualization.Cells.Append(MoveTemp(Batch.Cells));
				for (const auto& Pair : Batch.CellsByGrid)
				{
					auto& Indices = Result.Visualization.CellsByGrid.FindOrAdd(Pair.Key);
					for (int32 Index : Pair.Value) { Indices.Add(Base + Index); }
				}
			}, &Job->Cancelled);
			if (!Job->Alive.load(std::memory_order_relaxed)) { return Result; }
			// The authoring broad phase and retiring the old cache do not contribute
			// to first visible fill. Finish them after final regions can be published.
			Source.Vertices = MoveTemp(Triangles.Vertices); Source.Indices = MoveTemp(Triangles.Indices);
			Result.Index.Build(Source);
			Result.Visualization.LocalBounds = Result.Index.GetProjectedBounds(Options);
			Result.Visualization.CellSize = GetAuthoringVisualizationCellSize(Result.Visualization.LocalBounds);
			Retired.Reset();
			return Result;
		});
	}

	bool FMeshLayerDocumentBuild::StartSaved(const FComposableCameraMeshSurfaceEditorPreview& Saved,
		const FComposableCameraMeshSurfaceAuthoringData& Data, TConstArrayView<FComposableCameraMeshLayerDefinition> Layers,
		const FGuid& Revision)
	{
		if (Saved.Version != 1) { return false; }
		Cancel(); Work = MakeUnique<FDocumentPreviewWork>(); Work->Revision = Revision;
		FComposableCameraMeshSurfaceAuthoringData Source;
		Source.Vertices = Data.Vertices; Source.Indices = Data.Indices; Source.TriangleLayerIds = Data.TriangleLayerIds;
		TArray<FComposableCameraMeshLayerDefinition> Options; Options.SetNum(Layers.Num());
		for (int32 Index = 0; Index < Layers.Num(); ++Index)
		{ Options[Index].LayerId = Layers[Index].LayerId; Options[Index].bEnabled = Layers[Index].bEnabled; }
		Work->Future = UE::ComposableCamera::MeshEditor::AsyncMeshLayerEdit(
			[Saved = Saved, Source = MoveTemp(Source), Options = MoveTemp(Options), Job = Work->State]() mutable
		{
			FDocumentPreviewResult Result;
			if (!Job->Alive.load(std::memory_order_relaxed)) { return Result; }
			if (!LoadEditorPreview(Saved, Options, Result.Visualization))
			{ BuildAuthoringVisualization(Source, Options, Result.Visualization); }
			if (!Job->Alive.load(std::memory_order_relaxed)) { return Result; }
			auto Prepared = PrepareEditPreview(Result.Visualization, Options, &Job->Alive);
			if (Job->Alive.load(std::memory_order_relaxed)) { Job->Ready.Enqueue(MoveTemp(Prepared)); }
			Result.Index.Build(Source); return Result;
		});
		return true;
	}

	bool FMeshLayerDocumentBuild::TakeTile(const FGuid& Revision, FPreparedEditPreview& OutTile)
	{
		if (!Work) { return false; }
		if (Work->Revision != Revision) { Cancel(); return false; }
		return Work->State->Ready.Dequeue(OutTile);
	}

	bool FMeshLayerDocumentBuild::Take(const FGuid& Revision, FDocumentPreviewResult& OutResult, bool bWait)
	{
		if (!Work) { return false; }
		if (Work->Revision != Revision) { Cancel(); return false; }
		if (!bWait && !Work->Future.IsReady()) { return false; }
		if (bWait) { Work->Future.Wait(); }
		// Readiness follows all producer enqueues. Do not expose completion while
		// a final region is still waiting to reach the component publication queue.
		if (!Work->State->Ready.IsEmpty()) { return false; }
		OutResult = Work->Future.Consume(); Work.Reset();
		return true;
	}
}
