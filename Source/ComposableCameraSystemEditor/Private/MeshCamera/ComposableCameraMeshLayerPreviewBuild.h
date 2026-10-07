// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MeshCamera/ComposableCameraMeshLayerPIEPreview.h"
struct FComposableCameraMeshSurfaceEditorPreview;

namespace UE::ComposableCamera::MeshEditor
{
	struct FPreviewGeometryBuildResult
	{
		TArray<FResolvedSurfaceLayerMesh> LocalMeshes;
		TArray<FNativePreviewLayerMesh> NativeMeshes;
		TMap<FVector, FVector> ProjectionVertexCache;
		int32 ExpectedTriangles = 0;
		bool bBuiltOffGameThread = false;
		bool bReusedGeometry = false;
		/** Streaming batches are final geometry for their tiles; a separate terminal result closes the job. */
		bool bComplete = true;
	};

	/** Game-thread handle; worker sees only geometry/Layer metadata snapshots. Never waits on toggle. */
	class FPreviewGeometryBuild
	{
	public:
		FPreviewGeometryBuild() = default;
		~FPreviewGeometryBuild();
		FPreviewGeometryBuild(FPreviewGeometryBuild&& Other) noexcept;
		FPreviewGeometryBuild& operator=(FPreviewGeometryBuild&& Other) noexcept;
		FPreviewGeometryBuild(const FPreviewGeometryBuild&) = delete;
		FPreviewGeometryBuild& operator=(const FPreviewGeometryBuild&) = delete;

		void Begin(const FComposableCameraMeshSurfaceRuntimeData& Data,
			TConstArrayView<FComposableCameraMeshLayerDefinition> Layers, bool bEditorChunks = false,
			bool bStreamTiles = false, const FVector& LocalFocus = FVector::ZeroVector,
			const FComposableCameraMeshSurfaceEditorPreview* SavedPreview = nullptr);
		/** Streaming jobs can return multiple batches while the worker is still running. Never waits. */
		bool TakeResult(FPreviewGeometryBuildResult& OutResult);
		bool IsPending() const { return State.IsValid(); }
		void Reset();

		struct FState;
	private:
		TSharedPtr<FState, ESPMode::ThreadSafe> State;
	};

	/** Reap ready orphan tasks. Shutdown is the only path permitted to wait, before DLL unload. */
	void InitializePreviewGeometryBuilds();
	void PumpPreviewGeometryBuilds();
	void ShutdownPreviewGeometryBuilds();
}
