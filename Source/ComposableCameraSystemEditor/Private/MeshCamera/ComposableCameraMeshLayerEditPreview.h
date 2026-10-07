// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/MeshComponent.h"
#include "DynamicMeshBuilder.h"
#include "MeshCamera/ComposableCameraMeshLayerRendering.h"
#include "MeshCamera/ComposableCameraMeshLayerAuthoringIndex.h"
#include "ComposableCameraMeshLayerEditPreview.generated.h"

class ULevel;
class AActor;
namespace UE::ComposableCamera::MeshEditor { struct FEditPreviewGeometry; }

/** Transient authoring fill. Its scene proxy owns reusable GPU buffers, without collision or picking. */
UCLASS(Transient)
class UComposableCameraMeshLayerEditPreviewComponent : public UMeshComponent
{
	GENERATED_BODY()
public:
	UComposableCameraMeshLayerEditPreviewComponent();
	void SetGeometry(TArray<FDynamicMeshVertex>&& InVertices, TArray<uint32>&& InIndices, const FLinearColor& InColor);
	void SetSharedGeometry(TSharedPtr<const UE::ComposableCamera::MeshEditor::FEditPreviewGeometry, ESPMode::ThreadSafe> InGeometry, const FLinearColor& InColor);
	void SetFillColor(const FLinearColor& InColor);
	virtual void SendRenderDynamicData_Concurrent() override;
	TSharedPtr<const UE::ComposableCamera::MeshEditor::FEditPreviewGeometry, ESPMode::ThreadSafe> GetSharedGeometry() const { return Geometry; }
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual int32 GetNumMaterials() const override { return 1; }
	TConstArrayView<FDynamicMeshVertex> GetVertices() const;
	TConstArrayView<uint32> GetIndices() const;
	const FLinearColor& GetFillColor() const { return FillColor; }
	uint64 GetGeometryRevision() const { return GeometryRevision; }
private:
	TSharedPtr<const UE::ComposableCamera::MeshEditor::FEditPreviewGeometry, ESPMode::ThreadSafe> Geometry;
	FLinearColor FillColor = FLinearColor::White;
	uint64 GeometryRevision = 0;
	uint32 RenderVertexCapacity = 0, RenderIndexCapacity = 0;
};

namespace UE::ComposableCamera::MeshEditor
{
	constexpr int32 EditPreviewTileCells = 32;
	/** Exact editable base, immutable after publication. No source/UObject references. */
	struct FEditPreviewCheckpoint
	{
		FGuid Revision;
		FResolvedSurfaceVisualization Visualization;
		FMeshLayerAuthoringIndex Index;
		uint64 AllocatedBytes = 0;
	};
	/** Immutable, UObject-free published buffers. History shares unchanged tiles without triangle copies. */
	struct FEditPreviewGeometry
	{
		TArray<FDynamicMeshVertex> Vertices;
		TArray<uint32> Indices;
		FBoxSphereBounds LocalBounds = FBoxSphereBounds(ForceInit);
	};
	struct FSharedEditPreviewGeometry
	{
		TSharedPtr<const FEditPreviewGeometry, ESPMode::ThreadSafe> Geometry;
		FLinearColor Color = FLinearColor::White;
		bool bUseLayerColor = false;
	};
	struct FEditTileGeometry
	{
		TArray<FDynamicMeshVertex> Vertices;
		TArray<uint32> Indices;
	};
	using FEditTileGeometryByLayer = TMap<int32, FEditTileGeometry>;
	struct FPreparedEditPreviewTile
	{
		FIntPoint Tile = FIntPoint(0, 0);
		FEditTileGeometryByLayer Geometry;
		TMap<int32, FSharedEditPreviewGeometry> SharedGeometry;
	};
	struct FPreparedEditPreview
	{
		double CellSize = 10.0;
		TArray<FPreparedEditPreviewTile> Tiles;
	};
	/** Native only. Preserve the supplied cell order without copying all polygons on the editor thread. */
	FPreparedEditPreview PrepareEditPreview(const FResolvedSurfaceVisualization& Visualization,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers, const std::atomic_bool* Alive = nullptr,
		const FBox2D* DirtyBounds = nullptr, bool bShareBuffers = false);
	/** Assemble exactly these complete regions, including empty removal regions. Duplicate keys are ignored. */
	FPreparedEditPreview PrepareEditPreviewTiles(const FResolvedSurfaceVisualization& Visualization,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers, TConstArrayView<FIntPoint> Tiles,
		const FVector2D& Focus, const std::atomic_bool* Alive = nullptr, bool bShareBuffers = true);
	struct FEditPreviewUpdate;
	struct FQueuedEditPreviewUpdate;
	struct FEditPreviewHistory;
	/** Level-owned components. Ordinary frames only check readiness; updates happen on document mutations. */
	class FMeshLayerEditPreview
	{
	public:
		FMeshLayerEditPreview();
		~FMeshLayerEditPreview();
		bool Update(ULevel& Level, const FTransform& Anchor,
			const FResolvedSurfaceVisualization& Visualization, TConstArrayView<FComposableCameraMeshLayerDefinition> Layers,
			const FBox2D* DirtyBounds = nullptr);
		bool BeginUpdate(ULevel& Level, const FResolvedSurfaceVisualization& Visualization, const FBox2D* DirtyBounds = nullptr);
		/** Capture completed coverage. New snapshots replace waiting updates of the same tile, never source edits. */
		bool QueueUpdate(ULevel& Level, const FResolvedSurfaceVisualization& Visualization, const FBox2D* DirtyBounds = nullptr);
		/** Accept final regions immediately; bComplete closes the stream and queues removal of missing old tiles. */
		bool QueuePreparedUpdate(ULevel& Level, FPreparedEditPreview&& Prepared, bool bComplete = true);
		/** Already-resolved regional buffers, including empty tiles for exact erasure. No second worker/snapshot. */
		bool QueuePreparedRegion(ULevel& Level, FPreparedEditPreview&& Prepared, bool bFull);
		/** Install a complete document in one scene update. Regions are update indices, never loading slices. */
		bool PublishPreparedDocument(ULevel& Level, const FTransform& Anchor,
			TConstArrayView<FComposableCameraMeshLayerDefinition> Layers, FPreparedEditPreview&& Prepared,
			const TSet<FIntPoint>* PreservedRegions = nullptr);
		/** Immediate already-prepared local feedback while a resident document loads. */
		bool PublishPreparedRegions(ULevel& Level, const FTransform& Anchor,
			TConstArrayView<FComposableCameraMeshLayerDefinition> Layers, FPreparedEditPreview&& Prepared);
		void UpdateLayerAppearance(TConstArrayView<FComposableCameraMeshLayerDefinition> Layers);
		/** Dispatch up to four nearest owned tiles together, without publication or any worker wait. */
		void LaunchQueuedTileWork(TConstArrayView<FComposableCameraMeshLayerDefinition> Layers);
		/** Assemble plain snapshots on a worker; finite calls only consume ready results. Zero explicitly flushes. */
		bool AdvanceQueuedUpdates(ULevel& Level, const FTransform& Anchor,
			TConstArrayView<FComposableCameraMeshLayerDefinition> Layers, double TimeBudgetSeconds = 0.001);
		bool HasQueuedUpdates() const;
		bool IsRestoringRevision() const;
		/** Record complete visible revisions only. Saved checkpoint is pinned; native buffers are shared. */
		bool RememberRevision(const FGuid& Revision, const FGuid& SavedRevision,
			TSharedPtr<const FEditPreviewCheckpoint, ESPMode::ThreadSafe> Checkpoint = nullptr);
		TSharedPtr<const FEditPreviewCheckpoint, ESPMode::ThreadSafe> FindCheckpoint(const FGuid& Revision) const;
		/** Queue only changed tiles from a remembered revision, with no coverage/mesh reconstruction. */
		bool QueueRestoreRevision(ULevel& Level, const FGuid& Revision);
		int32 GetQueuedTileCount() const;
		int32 GetWaitingTileCount() const;
		/** Yields between cells; completed tiles replace their old buffer atomically. Keep coverage fixed while pending. */
		bool AdvanceUpdate(ULevel& Level, const FTransform& Anchor,
			const FResolvedSurfaceVisualization& Visualization, TConstArrayView<FComposableCameraMeshLayerDefinition> Layers,
			int32 MaxCells = MAX_int32, double TimeBudgetSeconds = 0.0);
		void CancelUpdate();
		bool IsReadyFor(const ULevel* Level) const;
		void Reset();
		AActor* GetActor() const;
	private:
		bool PublishPreparedTile(ULevel& Level, const FTransform& Anchor,
			TConstArrayView<FComposableCameraMeshLayerDefinition> Layers, FPreparedEditPreviewTile&& Tile);
		TUniquePtr<FEditPreviewUpdate> PendingUpdate;
		TUniquePtr<FQueuedEditPreviewUpdate> QueuedUpdate;
		TArray<TUniquePtr<FEditPreviewHistory>> History;
		FGuid LastRememberedRevision;
		// 32 x 32 coverage cells per spatial tile; Z identifies the Layer row.
		TMap<FIntVector, TWeakObjectPtr<UComposableCameraMeshLayerEditPreviewComponent>> Components;
		TWeakObjectPtr<AActor> Actor;
		TWeakObjectPtr<ULevel> OwnerLevel;
		double CellSize = 0.0;
		int32 ComponentLayerCount = 0;
		bool bInitialized = false;
	};
}
