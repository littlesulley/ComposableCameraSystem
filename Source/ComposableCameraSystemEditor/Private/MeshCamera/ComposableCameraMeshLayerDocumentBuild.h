// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MeshCamera/ComposableCameraMeshLayerAuthoringIndex.h"
#include "MeshCamera/ComposableCameraMeshLayerEditPreview.h"

struct FComposableCameraMeshSurfaceAuthoringData;
struct FComposableCameraMeshSurfaceEditorPreview;

namespace UE::ComposableCamera::MeshEditor
{
	struct FDocumentPreviewResult
	{
		FMeshLayerAuthoringIndex Index;
		FResolvedSurfaceVisualization Visualization;
	};
	struct FDocumentPreviewIndex
	{
		FMeshLayerAuthoringIndex Index;
		double CellSize = 10.0;
	};
	struct FDocumentPreviewWork;
	/** Native document reconstruction. Production Edit uses resident load; explicit legacy callers can stream regions. */
	class FMeshLayerDocumentBuild
	{
	public:
		FMeshLayerDocumentBuild();
		~FMeshLayerDocumentBuild();
		void Start(const FComposableCameraMeshSurfaceAuthoringData& Data,
			TConstArrayView<FComposableCameraMeshLayerDefinition> Layers, const FGuid& Revision,
			FResolvedSurfaceVisualization* RetiredCache = nullptr, const FVector2D& LocalFocus = FVector2D::ZeroVector,
			bool bPublishPreview = true);
		void Cancel();
		/** Resident editing load: early index, then one complete native display; no spatial load stream. */
		void StartResident(const FComposableCameraMeshSurfaceAuthoringData& Data,
			TConstArrayView<FComposableCameraMeshLayerDefinition> Layers, const FGuid& Revision,
			const FComposableCameraMeshSurfaceEditorPreview* Saved = nullptr,
			FResolvedSurfaceVisualization* RetiredCache = nullptr, bool bPublishPreview = true);
		bool IsResident() const;
		FGuid GetRevision() const;
		bool TakeIndex(const FGuid& Revision, FDocumentPreviewIndex& Out);
		/** Only explicit source/save/focus boundaries and automation may wait. */
		void Wait();
		void RetagRevision(const FGuid& Revision);
		/** Load Save's exact polygons directly, without source clipping or spatial-tile scheduling. */
		bool StartSaved(const FComposableCameraMeshSurfaceEditorPreview& Saved,
			const FComposableCameraMeshSurfaceAuthoringData& Data, TConstArrayView<FComposableCameraMeshLayerDefinition> Layers,
			const FGuid& Revision);
		bool HasPending() const { return Work.IsValid(); }
		/** Consume prepared native geometry: one whole document for resident load, regions for legacy Start. Never waits. */
		bool TakeTile(const FGuid& Revision, FPreparedEditPreview& OutTile);
		/** Terminal cache/index after prepared publications are consumed. Explicit wait is for automation. */
		bool Take(const FGuid& Revision, FDocumentPreviewResult& OutResult, bool bWait = false);
	private:
		TUniquePtr<FDocumentPreviewWork> Work;
	};
}
