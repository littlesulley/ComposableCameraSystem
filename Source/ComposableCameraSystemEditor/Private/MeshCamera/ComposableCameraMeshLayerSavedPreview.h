// Copyright 2026 Sulley. All Rights Reserved.
#pragma once
#include "MeshCamera/ComposableCameraMeshLayerRendering.h"
#include "MeshCamera/ComposableCameraMeshSurfaceTypes.h"

namespace UE::ComposableCamera::MeshEditor
{
	/** Serializes existing exact coverage. No scene queries, clipping or UObject access. */
	FComposableCameraMeshSurfaceEditorPreview SaveEditorPreview(const FResolvedSurfaceVisualization& Visualization,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers);
	/** Allocation-free comparison at Save only; a version number alone cannot validate old coverage. */
	bool MatchesEditorPreview(const FComposableCameraMeshSurfaceEditorPreview& Saved,
		const FResolvedSurfaceVisualization& Visualization, TConstArrayView<FComposableCameraMeshLayerDefinition> Layers);
	bool LoadEditorPreview(const FComposableCameraMeshSurfaceEditorPreview& Saved,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers, FResolvedSurfaceVisualization& Out);
}
