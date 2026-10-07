// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include <atomic>

class FPrimitiveDrawInterface;
struct FDynamicMeshVertex;
struct FComposableCameraMeshLayerDefinition;
struct FComposableCameraMeshSurfaceRuntimeData;
#if WITH_EDITORONLY_DATA
struct FComposableCameraMeshSurfaceAuthoringData;
#endif

namespace UE::ComposableCamera::MeshEditor
{
	class FMeshLayerAuthoringIndex;
	constexpr double VisualizationSurfaceOffset = 1.5;

	using FSurfacePolygon = TArray<FVector, TInlineAllocator<8>>;

	struct FResolvedSurfacePatch
	{
		FSurfacePolygon LocalVertices;
		FVector LocalNormal = FVector::UpVector;
		int32 LayerIndex = INDEX_NONE;
	};

	struct FResolvedSurfaceCell
	{
		// One XY cell indexes every elevation. Center-plane fields are summaries
		// only; actual overlap heights and visible Layer ownership live in Patches.
		FVector LocalPosition = FVector::ZeroVector;
		FVector LocalNormal = FVector::UpVector;
		int32 LayerIndex = INDEX_NONE;
		TArray<FResolvedSurfacePatch, TInlineAllocator<1>> Patches;
		bool bFullCoverage = false;
	};

	struct FResolvedSurfaceVisualization
	{
		double CellSize = 10.0;
		TArray<FResolvedSurfaceCell> Cells;
		FBox2D LocalBounds = FBox2D(ForceInit);
		TMap<FIntPoint, TArray<int32, TInlineAllocator<2>>> CellsByGrid;

		void Reset()
		{
			CellSize = 10.0;
			Cells.Reset();
			CellsByGrid.Reset();
			LocalBounds = FBox2D(ForceInit);
		}
	};

	struct FVisualizationUpdateStats
	{
		int32 SourceTriangleTests = 0;
		int32 RasterizedTriangles = 0;
		int64 CellTests = 0;
		bool bFullRebuild = false;
	};

	/** Cached local-space mesh used by PIE's editor-only persistent mesh components. */
	struct FResolvedSurfaceLayerMesh
	{
		int32 LayerIndex = INDEX_NONE;
		FColor Color = FColor::White;
		TArray<FVector> LocalVertices;
		TArray<int32> Indices;
	};

#if WITH_EDITORONLY_DATA
	/** Exact existing resolver, yielding between cells/triangles. Source and Layer options stay fixed until completion. */
	class FAuthoringVisualizationUpdate
	{
	public:
		FAuthoringVisualizationUpdate();
		~FAuthoringVisualizationUpdate();
		void Begin(const FComposableCameraMeshSurfaceAuthoringData& Data,
			TConstArrayView<FComposableCameraMeshLayerDefinition> Layers, FResolvedSurfaceVisualization& Visualization,
			const FBox2D* DirtyBounds, const FMeshLayerAuthoringIndex* SourceIndex, int32 AppendFrom = INDEX_NONE,
			const FBox2D* SnapshotBounds = nullptr, double SnapshotCellSize = 0.0);
		bool Advance(const FComposableCameraMeshSurfaceAuthoringData& Data,
			TConstArrayView<FComposableCameraMeshLayerDefinition> Layers, FResolvedSurfaceVisualization& Visualization,
			int32 MaxOperations = MAX_int32, double TimeBudgetSeconds = 0.0);
		const FVisualizationUpdateStats& GetStats() const;
	private:
		struct FState;
		TUniquePtr<FState> State;
	};

	/** Same bounds-derived grid policy for live source and compact worker snapshots. */
	double GetAuthoringVisualizationCellSize(const FBox2D& Bounds);

	void BuildAuthoringVisualization(
		const FComposableCameraMeshSurfaceAuthoringData& Data,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers,
		FResolvedSurfaceVisualization& OutVisualization);

	/** Refresh only grid cells touched by a brush stamp; other surface/Layer winners survive. */
	void UpdateAuthoringVisualization(
		const FComposableCameraMeshSurfaceAuthoringData& Data,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers,
		const FBox2D& DirtyBounds,
		FResolvedSurfaceVisualization& OutVisualization,
		FVisualizationUpdateStats* OutStats = nullptr, const FMeshLayerAuthoringIndex* SourceIndex = nullptr);

	/** Append-only Brush update: resolve new triangles against retained coverage in original source order. */
	void AppendAuthoringVisualization(const FComposableCameraMeshSurfaceAuthoringData& Data,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers, int32 FirstAddedTriangle, const FBox2D& DirtyBounds,
		FResolvedSurfaceVisualization& OutVisualization, FVisualizationUpdateStats* OutStats = nullptr,
		const FMeshLayerAuthoringIndex* SourceIndex = nullptr);
#endif

	void BuildRuntimeVisualization(
		const FComposableCameraMeshSurfaceRuntimeData& Data,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers,
		FResolvedSurfaceVisualization& OutVisualization, const std::atomic_bool* Cancelled = nullptr);

	/** Publish final, disjoint grid tiles before resolving the rest of the document. Same grid/coverage rules as the full build. */
	void BuildRuntimeVisualizationTiles(
		const FComposableCameraMeshSurfaceRuntimeData& Data,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers, const FVector2D& LocalFocus,
		TFunctionRef<void(FResolvedSurfaceVisualization&&)> Publish,
		const std::atomic_bool* Cancelled = nullptr);

	void BuildVisualizationMeshes(
		const FResolvedSurfaceVisualization& Visualization,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers,
		TArray<FResolvedSurfaceLayerMesh>& OutMeshes, const std::atomic_bool* Cancelled = nullptr);

	/** Pure world-routing policy shared by the PIE ticker and automation. */
	bool ShouldDrawPIEPreview(
		bool bPreviewRequested,
		bool bPIEIsEnding,
		EWorldType::Type WorldType);

	void DrawVisualization(
		FPrimitiveDrawInterface* PDI,
		const FTransform& LocalToWorld,
		const FResolvedSurfaceVisualization& Visualization,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers);

	/** Identical fill vertices/tangents/fan indices for both PDI drafts and persistent Edit buffers. */
	void AppendVisualizationPatch(const FResolvedSurfacePatch& Patch,
		TArray<FDynamicMeshVertex>& Vertices, TArray<uint32>& Indices);

	/** Small captured-plane fill while exact floor projection completes. */
	void DrawShapePreview(FPrimitiveDrawInterface* PDI, const FTransform& LocalToWorld, const FResolvedSurfaceLayerMesh& Mesh);
}
