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
	struct FResolvedSurfaceCell
	{
		FVector LocalPosition = FVector::ZeroVector;
		FVector LocalNormal = FVector::UpVector;
		int32 LayerIndex = INDEX_NONE;
	};

	struct FResolvedSurfaceVisualization
	{
		double CellSize = 10.0;
		TArray<FResolvedSurfaceCell> Cells;
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
}
