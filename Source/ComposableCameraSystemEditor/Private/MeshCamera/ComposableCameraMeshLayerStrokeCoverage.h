// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MeshCamera/ComposableCameraMeshLayerRendering.h"

namespace UE::ComposableCamera::MeshEditor
{
	struct FStrokeCoverageWork;
	struct FStrokeCoverageInput;
	struct FEditPreviewCheckpoint;
	struct FPreparedEditPreview;

	/** Derived coverage only. Authoritative source stays on the editor thread. */
	class FMeshLayerStrokeCoverage
	{
	public:
		FMeshLayerStrokeCoverage();
		~FMeshLayerStrokeCoverage();
		void Cancel();
		void EnablePreparedPreview() { bPreparePreview = true; }
		/** Temporary feedback on the opening document's fixed grid; never trigger a whole-source snapshot. */
		void EnableRegionOnlyPreview() { bPreparePreview = bRegionOnly = true; }
		bool TakePrepared(FPreparedEditPreview& Out, bool& bOutFull);
		/** Retain an exact immutable restored base; its first mutable copy happens on the worker. */
		void UseCheckpoint(TSharedPtr<const FEditPreviewCheckpoint, ESPMode::ThreadSafe> Checkpoint);
		bool HasPending() const { return PendingIndex < Pending.Num() || Work.IsValid(); }
		void Queue(const FComposableCameraMeshSurfaceAuthoringData& Data,
			TConstArrayView<FComposableCameraMeshLayerDefinition> Layers, const FMeshLayerAuthoringIndex& SourceIndex,
			const FBox2D& DirtyBounds, int32 FirstAddedTriangle, double CurrentCellSize, bool bCacheInvalid);
		/** Returns one complete cache. Finite interaction never waits; explicit boundaries may flush. */
		bool Advance(FResolvedSurfaceVisualization& Visualization, FBox2D& OutDirtyBounds, bool& bOutFull,
			bool bFlush = false, bool bAllowStart = true);
		int32 GetLastSnapshotTriangleCount() const { return LastSnapshotTriangles; }
		/** Completed batch work counters; independent of wall-clock timing. */
		int32 GetLastCoverageUpdateCount() const { return LastCoverageUpdates; }
		int32 GetLastPreparedTileCount() const { return LastPreparedTiles; }
	private:
		TUniquePtr<FStrokeCoverageWork> Work;
		TArray<TUniquePtr<FStrokeCoverageInput>> Pending;
		int32 PendingIndex = 0;
		TSharedPtr<const FEditPreviewCheckpoint, ESPMode::ThreadSafe> BaseCheckpoint;
		int32 LastSnapshotTriangles = 0;
		int32 LastCoverageUpdates = 0, LastPreparedTiles = 0;
		double PlannedCellSize = 10.0;
		bool bInitialized = false, bBoundsValid = false;
		bool bPreparePreview = false;
		bool bRegionOnly = false;
	};
}
