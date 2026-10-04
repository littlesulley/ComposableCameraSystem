// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"

class FPrimitiveDrawInterface;
struct FComposableCameraMeshLayerDefinition;
struct FComposableCameraMeshSurfaceRuntimeData;
#if WITH_EDITORONLY_DATA
struct FComposableCameraMeshSurfaceAuthoringData;
#endif

namespace UE::ComposableCamera::MeshEditor
{
	using FSurfacePolygon = TArray<FVector, TInlineAllocator<8>>;

	struct FResolvedSurfacePatch
	{
		FSurfacePolygon LocalVertices;
		FVector LocalNormal = FVector::UpVector;
		int32 LayerIndex = INDEX_NONE;
	};

	struct FResolvedSurfaceCell
	{
		// Center-plane summary for height buckets and sparse-grid index repair.
		// Visible coverage/Layer ownership is stored in Patches, including boundaries.
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
		int32 RasterizedTriangles = 0;
		int64 CellTests = 0;
		bool bFullRebuild = false;
	};

	/** Cached local-space mesh used by PIE's non-shipping LineBatcher path. */
	struct FResolvedSurfaceLayerMesh
	{
		int32 LayerIndex = INDEX_NONE;
		FColor Color = FColor::White;
		TArray<FVector> LocalVertices;
		TArray<int32> Indices;
	};

#if WITH_EDITORONLY_DATA
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
		FVisualizationUpdateStats* OutStats = nullptr);
#endif

	void BuildRuntimeVisualization(
		const FComposableCameraMeshSurfaceRuntimeData& Data,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers,
		FResolvedSurfaceVisualization& OutVisualization);

	void BuildVisualizationMeshes(
		const FResolvedSurfaceVisualization& Visualization,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers,
		TArray<FResolvedSurfaceLayerMesh>& OutMeshes);

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

	/** Small captured-plane fill while exact floor projection completes. */
	void DrawShapePreview(FPrimitiveDrawInterface* PDI, const FTransform& LocalToWorld, const FResolvedSurfaceLayerMesh& Mesh);
}
