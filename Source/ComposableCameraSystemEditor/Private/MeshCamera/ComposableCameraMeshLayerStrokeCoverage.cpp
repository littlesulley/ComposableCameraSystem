// Copyright 2026 Sulley. All Rights Reserved.

#include "MeshCamera/ComposableCameraMeshLayerStrokeCoverage.h"

#include "MeshCamera/ComposableCameraMeshLayerEditWork.h"
#include "MeshCamera/ComposableCameraMeshLayerAuthoringIndex.h"
#include "MeshCamera/ComposableCameraMeshLayerEditPreview.h"
#include "MeshCamera/ComposableCameraMeshSurfaceTypes.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Containers/Queue.h"
#include <atomic>

namespace UE::ComposableCamera::MeshEditor
{
	struct FStrokeCoverageInput
	{
		FComposableCameraMeshSurfaceAuthoringData Source;
		TArray<FComposableCameraMeshLayerDefinition> Layers;
		FBox2D DirtyBounds = FBox2D(ForceInit), ProjectedBounds = FBox2D(ForceInit);
		double CellSize = 10.0;
		bool bFull = false, bAppend = false;
	};
	struct FStrokePreviewPublication
	{
		FPreparedEditPreview Prepared;
		bool bFull = false;
	};
	using FStrokePreviewQueue = TQueue<FStrokePreviewPublication, EQueueMode::Spsc>;
	namespace
	{
		struct FStrokeCoverageResult
		{
			FResolvedSurfaceVisualization Cache;
			FBox2D DirtyBounds = FBox2D(ForceInit);
			bool bFull = false;
			int32 CoverageUpdates = 0, PreparedTiles = 0;
		};
		FStrokeCoverageResult ResolveStrokeCoverage(FResolvedSurfaceVisualization Cache,
			TArray<TUniquePtr<FStrokeCoverageInput>> Inputs, const std::atomic_bool* Alive = nullptr,
			FStrokePreviewQueue* Publications = nullptr)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(CCS_MeshLayers_StrokeCoverageWorker);
			FStrokeCoverageResult Result; FAuthoringVisualizationUpdate Update;
			TArray<TArray<FIntPoint>> InputTiles;
			TMap<FIntPoint, int32> LastTileUpdate;
			const bool bPlannedFull = Inputs.ContainsByPredicate([](const auto& Input)
			{ return Input->bFull || !Input->ProjectedBounds.bIsValid || !Input->DirtyBounds.bIsValid; });
			if (Publications && !bPlannedFull)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(CCS_MeshLayers_StrokePreviewPlan);
				InputTiles.SetNum(Inputs.Num());
				for (int32 InputIndex = 0; InputIndex < Inputs.Num(); ++InputIndex)
				{
					if (Alive && !Alive->load(std::memory_order_relaxed)) { return Result; }
					const auto& Input = *Inputs[InputIndex]; const double TileSize = Input.CellSize * EditPreviewTileCells;
					const FIntPoint Min(FMath::FloorToInt(Input.DirtyBounds.Min.X / TileSize), FMath::FloorToInt(Input.DirtyBounds.Min.Y / TileSize));
					const FIntPoint Max(FMath::FloorToInt(Input.DirtyBounds.Max.X / TileSize), FMath::FloorToInt(Input.DirtyBounds.Max.Y / TileSize));
					for (int32 Y = Min.Y; Y <= Max.Y; ++Y) { for (int32 X = Min.X; X <= Max.X; ++X)
					{
						if (Alive && !Alive->load(std::memory_order_relaxed)) { return Result; }
						const FIntPoint Tile(X, Y); InputTiles[InputIndex].Add(Tile); LastTileUpdate.Add(Tile, InputIndex);
					} }
				}
			}
			for (int32 InputIndex = 0; InputIndex < Inputs.Num(); ++InputIndex)
			{
				if (Alive && !Alive->load(std::memory_order_relaxed)) { break; }
				const auto& Input = Inputs[InputIndex];
				Update.Begin(Input->Source, Input->Layers, Cache, Input->bFull ? nullptr : &Input->DirtyBounds,
					nullptr, Input->bAppend ? 0 : INDEX_NONE, &Input->ProjectedBounds, Input->CellSize);
				bool bDone = false;
				while (!bDone && (!Alive || Alive->load(std::memory_order_relaxed)))
				{
					bDone = Update.Advance(Input->Source, Input->Layers, Cache, 256);
				}
				if (Input->DirtyBounds.bIsValid) { Result.DirtyBounds += Input->DirtyBounds; }
				Result.bFull |= Input->bFull || !Input->ProjectedBounds.bIsValid || Update.GetStats().bFullRebuild;
				++Result.CoverageUpdates;
				if (Publications && (!Alive || Alive->load(std::memory_order_relaxed)))
				{
					FPreparedEditPreview Prepared;
					if (Result.bFull)
					{
						if (InputIndex + 1 != Inputs.Num()) { continue; }
						Prepared = PrepareEditPreview(Cache, Input->Layers, Alive, nullptr, true);
					}
					else
					{
						TArray<FIntPoint> FinalTiles;
						for (const FIntPoint& Tile : InputTiles[InputIndex])
						{ if (LastTileUpdate.FindChecked(Tile) == InputIndex) { FinalTiles.Add(Tile); } }
						if (FinalTiles.IsEmpty()) { continue; }
						// Keep every exact coverage operation, but assemble a region only after
						// its final mutation in this owned batch. Distant regions publish independently.
						Prepared = PrepareEditPreviewTiles(Cache, Input->Layers, FinalTiles, Input->DirtyBounds.GetCenter(), Alive);
					}
					Result.PreparedTiles += Prepared.Tiles.Num();
					if (!Alive || Alive->load(std::memory_order_relaxed)) { Publications->Enqueue({MoveTemp(Prepared), Result.bFull}); }
				}
			}
			Result.Cache = MoveTemp(Cache); return Result;
		}
	}

	struct FStrokeCoverageWork
	{
		TFuture<FStrokeCoverageResult> Future;
		TSharedRef<std::atomic_bool, ESPMode::ThreadSafe> Alive = MakeShared<std::atomic_bool, ESPMode::ThreadSafe>(true);
		TSharedRef<FStrokePreviewQueue, ESPMode::ThreadSafe> Ready = MakeShared<FStrokePreviewQueue, ESPMode::ThreadSafe>();
		~FStrokeCoverageWork() { Alive->store(false, std::memory_order_relaxed); }
	};
	bool FMeshLayerStrokeCoverage::TakePrepared(FPreparedEditPreview& Out, bool& bOutFull)
	{
		FStrokePreviewPublication Publication;
		if (!Work || !Work->Ready->Dequeue(Publication)) { return false; }
		Out = MoveTemp(Publication.Prepared); bOutFull = Publication.bFull; return true;
	}

	FMeshLayerStrokeCoverage::FMeshLayerStrokeCoverage() = default;
	FMeshLayerStrokeCoverage::~FMeshLayerStrokeCoverage() { Cancel(); }
	void FMeshLayerStrokeCoverage::Cancel()
	{
		if (Work) { Work->Alive->store(false, std::memory_order_relaxed); }
		if (Work || !Pending.IsEmpty() || BaseCheckpoint)
		{
			UE::ComposableCamera::MeshEditor::AsyncMeshLayerEdit(
				[RetiredWork = MoveTemp(Work), RetiredInputs = MoveTemp(Pending), RetiredBase = MoveTemp(BaseCheckpoint)]() mutable
				{ RetiredWork.Reset(); RetiredInputs.Reset(); RetiredBase.Reset(); });
		}
		LastSnapshotTriangles = LastCoverageUpdates = LastPreparedTiles = 0; bInitialized = bBoundsValid = false;
		PendingIndex = 0;
	}
	void FMeshLayerStrokeCoverage::UseCheckpoint(TSharedPtr<const FEditPreviewCheckpoint, ESPMode::ThreadSafe> Checkpoint)
	{
		if (BaseCheckpoint == Checkpoint) { return; } // A no-op stroke need not detach its base.
		check(!HasPending());
		if (BaseCheckpoint)
		{
			UE::ComposableCamera::MeshEditor::AsyncMeshLayerEdit( [Retired = MoveTemp(BaseCheckpoint)]() mutable { Retired.Reset(); });
		}
		BaseCheckpoint = MoveTemp(Checkpoint);
		bInitialized = bBoundsValid = false;
	}

	void FMeshLayerStrokeCoverage::Queue(const FComposableCameraMeshSurfaceAuthoringData& Data,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers, const FMeshLayerAuthoringIndex& SourceIndex,
		const FBox2D& DirtyBounds, int32 FirstAddedTriangle, double CurrentCellSize, bool bCacheInvalid)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CCS_MeshLayers_StrokeCoverageSnapshot);
		check(SourceIndex.IsCurrent(Data));
		auto Input = MakeUnique<FStrokeCoverageInput>();
		Input->DirtyBounds = DirtyBounds; Input->ProjectedBounds = SourceIndex.GetProjectedBounds(Layers);
		Input->bFull = !DirtyBounds.bIsValid || (!bInitialized ? bCacheInvalid : !bBoundsValid);
		if (bRegionOnly) { Input->bFull = false; }
		if (!bInitialized) { PlannedCellSize = CurrentCellSize; }
		const double Desired = GetAuthoringVisualizationCellSize(Input->ProjectedBounds);
		// Apply the original grid policy at each source mutation, including temporary
		// growth or an empty document followed by a new stamp before publication.
		if (!bRegionOnly && (Input->bFull || Desired > PlannedCellSize * 1.25))
		{
			PlannedCellSize = Desired; Input->bFull = true;
		}
		bInitialized = true; bBoundsValid = Input->ProjectedBounds.bIsValid;
		Input->CellSize = PlannedCellSize; Input->bAppend = FirstAddedTriangle != INDEX_NONE && !Input->bFull;
		// No Layer assets, World/Settings/index or mutable source references enter a worker.
		Input->Layers.SetNum(Layers.Num());
		for (int32 Index = 0; Index < Layers.Num(); ++Index)
		{
			Input->Layers[Index].LayerId = Layers[Index].LayerId; Input->Layers[Index].bEnabled = Layers[Index].bEnabled;
		}
		TArray<int32> Candidates;
		FBox2D SnapshotRegion(ForceInit);
		TMap<FGuid, bool> EnabledLayers;
		if (!Input->bFull && !Input->bAppend)
		{
			const FBox2D WholeCells(
				FVector2D(FMath::FloorToDouble(DirtyBounds.Min.X / PlannedCellSize), FMath::FloorToDouble(DirtyBounds.Min.Y / PlannedCellSize)) * PlannedCellSize,
				FVector2D(FMath::FloorToDouble(DirtyBounds.Max.X / PlannedCellSize) + 1, FMath::FloorToDouble(DirtyBounds.Max.Y / PlannedCellSize) + 1) * PlannedCellSize);
			SourceIndex.FindVisualizationCandidates(WholeCells, Layers, Candidates);
			SnapshotRegion = WholeCells;
			for (const auto& Layer : Layers) { EnabledLayers.Add(Layer.LayerId, Layer.bEnabled); }
			// Filter before reserving snapshot storage, retaining ascending source order.
			Candidates.RemoveAll([&](int32 Triangle)
			{
				const int32 Offset = Triangle * 3;
				if (!Data.Vertices.IsValidIndex(Data.Indices[Offset]) || !Data.Vertices.IsValidIndex(Data.Indices[Offset + 1])
					|| !Data.Vertices.IsValidIndex(Data.Indices[Offset + 2])) { return true; }
				const bool* Enabled = EnabledLayers.Find(Data.TriangleLayerIds[Triangle]);
				if (!Enabled || !*Enabled) { return true; }
				const FVector3f A = Data.Vertices[Data.Indices[Offset]], B = Data.Vertices[Data.Indices[Offset + 1]], C = Data.Vertices[Data.Indices[Offset + 2]];
				// Leaves are 128-triangle blocks. One local neighbor does not make every
				// other triangle in its leaf relevant to the complete dirty cells.
				return FMath::Min3(A.X, B.X, C.X) > SnapshotRegion.Max.X || FMath::Max3(A.X, B.X, C.X) < SnapshotRegion.Min.X
					|| FMath::Min3(A.Y, B.Y, C.Y) > SnapshotRegion.Max.Y || FMath::Max3(A.Y, B.Y, C.Y) < SnapshotRegion.Min.Y;
			});
		}
		const int32 First = Input->bAppend ? FirstAddedTriangle : 0;
		const int32 Count = !Input->bFull && !Input->bAppend ? Candidates.Num() : Data.TriangleLayerIds.Num() - First;
		Input->Source.Vertices.Reserve(Count * 3); Input->Source.Indices.Reserve(Count * 3); Input->Source.TriangleLayerIds.Reserve(Count);
		for (int32 Cursor = 0; Cursor < Count; ++Cursor)
		{
			const int32 Triangle = !Input->bFull && !Input->bAppend ? Candidates[Cursor] : First + Cursor;
			const int32 Offset = Triangle * 3;
			if (!Data.Vertices.IsValidIndex(Data.Indices[Offset]) || !Data.Vertices.IsValidIndex(Data.Indices[Offset + 1])
				|| !Data.Vertices.IsValidIndex(Data.Indices[Offset + 2])) { continue; }
			const int32 Base = Input->Source.Vertices.Num();
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				Input->Source.Vertices.Add(Data.Vertices[Data.Indices[Offset + Corner]]); Input->Source.Indices.Add(Base + Corner);
			}
			Input->Source.TriangleLayerIds.Add(Data.TriangleLayerIds[Triangle]);
		}
		// Only adjacent append-only coverage operations can merge without changing
		// same-Layer height winners. Mixed Brush/Erase keeps its exact operation order.
		if (Input->bFull) { Pending.Reset(); PendingIndex = 0; }
		if (Input->bAppend && PendingIndex < Pending.Num() && Pending.Last()->bAppend && Pending.Last()->CellSize == Input->CellSize
			&& (!bPreparePreview || Pending.Last()->Source.TriangleLayerIds.Num() + Input->Source.TriangleLayerIds.Num() <= 4096))
		{
			auto& Previous = *Pending.Last(); const int32 Base = Previous.Source.Vertices.Num();
			Previous.Source.Vertices.Append(Input->Source.Vertices);
			for (int32 Index : Input->Source.Indices) { Previous.Source.Indices.Add(Base + Index); }
			Previous.Source.TriangleLayerIds.Append(Input->Source.TriangleLayerIds);
			Previous.DirtyBounds += Input->DirtyBounds; Previous.ProjectedBounds = Input->ProjectedBounds;
		}
		else { Pending.Add(MoveTemp(Input)); }
	}

	bool FMeshLayerStrokeCoverage::Advance(FResolvedSurfaceVisualization& Visualization,
		FBox2D& OutDirtyBounds, bool& bOutFull, bool bFlush, bool bAllowStart)
	{
		if (Work)
		{
			if (!bFlush && !Work->Future.IsReady()) { return false; }
			if (bFlush) { Work->Future.Wait(); }
			// A ready future follows all enqueues. Let the mode publish them before retiring their owner.
			if (bPreparePreview && !Work->Ready->IsEmpty()) { return false; }
			auto Result = Work->Future.Consume(); Work.Reset();
			LastCoverageUpdates = Result.CoverageUpdates; LastPreparedTiles = Result.PreparedTiles;
			Visualization = MoveTemp(Result.Cache); OutDirtyBounds = Result.DirtyBounds; bOutFull = Result.bFull;
			return true;
		}
		if (PendingIndex >= Pending.Num() || !bAllowStart) { return false; }
		LastSnapshotTriangles = LastCoverageUpdates = LastPreparedTiles = 0;
		// Coalesce display within a bounded owned batch, so a long held-stroke backlog
		// still publishes progress. Source inputs are never dropped or recomputed from a newer source.
		const int32 Count = bPreparePreview ? FMath::Min(8, Pending.Num() - PendingIndex) : Pending.Num() - PendingIndex;
		TArray<TUniquePtr<FStrokeCoverageInput>> Inputs; Inputs.Reserve(Count);
		for (int32 Cursor = 0; Cursor < Count; ++Cursor)
		{
			auto Input = MoveTemp(Pending[PendingIndex++]); LastSnapshotTriangles += Input->Source.TriangleLayerIds.Num(); Inputs.Add(MoveTemp(Input));
		}
		if (PendingIndex == Pending.Num()) { Pending.Reset(); PendingIndex = 0; }
		if (bFlush && !bPreparePreview)
		{
			if (BaseCheckpoint) { Visualization = BaseCheckpoint->Visualization; BaseCheckpoint.Reset(); }
			auto Result = ResolveStrokeCoverage(MoveTemp(Visualization), MoveTemp(Inputs));
			LastCoverageUpdates = Result.CoverageUpdates; LastPreparedTiles = Result.PreparedTiles;
			Visualization = MoveTemp(Result.Cache); OutDirtyBounds = Result.DirtyBounds; bOutFull = Result.bFull;
			return true;
		}
		Work = MakeUnique<FStrokeCoverageWork>();
		Work->Future = UE::ComposableCamera::MeshEditor::AsyncMeshLayerEdit(
			[Cache = MoveTemp(Visualization), Base = MoveTemp(BaseCheckpoint), Inputs = MoveTemp(Inputs), Alive = Work->Alive,
				Ready = Work->Ready, bPrepare = bPreparePreview]() mutable
		{
			// Copy plain resolved polygons once, never reclip the historical document.
			// Later stamps continue from the returned mutable cache as before.
			if (Base && !Inputs[0]->bFull && Alive->load(std::memory_order_relaxed))
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(CCS_MeshLayers_EditCheckpointCopy);
				Cache = Base->Visualization;
			}
			return ResolveStrokeCoverage(MoveTemp(Cache), MoveTemp(Inputs), &Alive.Get(), bPrepare ? &Ready.Get() : nullptr);
		});
		return false;
	}
}
