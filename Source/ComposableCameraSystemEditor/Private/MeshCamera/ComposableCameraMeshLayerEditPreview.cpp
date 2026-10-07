// Copyright 2026 Sulley. All Rights Reserved.

#include "MeshCamera/ComposableCameraMeshLayerEditPreview.h"

#include "MeshCamera/ComposableCameraMeshLayerEditWork.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "GameFramework/Actor.h"
#include "LocalVertexFactory.h"
#include "Materials/Material.h"
#include "Materials/MaterialRenderProxy.h"
#include "MeshCamera/ComposableCameraMeshSurfaceTypes.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveUniformShaderParametersBuilder.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "SceneManagement.h"
#include "SceneInterface.h"
#include "StaticMeshResources.h"

namespace
{
	uint32 EditBufferCapacity(uint32 Count) { return FMath::RoundUpToPowerOfTwo(FMath::Max(64u, Count)); }
	/** Same vertices/material color/backface policy as authoring PDI, with engine-owned resource lifetime. */
	class FMeshLayerEditSceneProxy final : public FPrimitiveSceneProxy
	{
	public:
		explicit FMeshLayerEditSceneProxy(UComposableCameraMeshLayerEditPreviewComponent& Component)
			: FPrimitiveSceneProxy(&Component), VertexFactory(GetScene().GetFeatureLevel(), "CCSMeshLayerEdit"),
			MaterialRelevance(Component.GetMaterialRelevance(GetScene().GetFeatureLevel())),
			MaterialProxy(MakeUnique<FColoredMaterialRenderProxy>(Component.GetMaterial(0)->GetRenderProxy(), Component.GetFillColor())),
			InitialGeometry(Component.GetSharedGeometry())
		{
			DrawVertexCount = InitialGeometry->Vertices.Num(); DrawIndexCount = InitialGeometry->Indices.Num();
			GeometryRevision = Component.GetGeometryRevision();
		}
		virtual void CreateRenderThreadResources(FRHICommandListBase& RHICmdList) override
		{
			FPrimitiveSceneProxy::CreateRenderThreadResources(RHICmdList);
			if (!InitialGeometry) { return; }
			TRACE_CPUPROFILER_EVENT_SCOPE(CCS_MeshLayers_EditBufferBuild);
			// Engine calls this before adding the primitive to its render scene. No editor-thread vertex copy.
			TArray<FDynamicMeshVertex> Vertices = InitialGeometry->Vertices;
			const uint32 Capacity = EditBufferCapacity(Vertices.Num()); Vertices.Reserve(Capacity);
			while (static_cast<uint32>(Vertices.Num()) < Capacity) { Vertices.Add(FDynamicMeshVertex(FVector3f::ZeroVector)); }
			IndexBuffer.Indices = InitialGeometry->Indices;
			IndexBuffer.Indices.SetNumZeroed(EditBufferCapacity(IndexBuffer.Indices.Num()));
			VertexBuffers.InitFromDynamicVertex(RHICmdList, &VertexFactory, Vertices);
			IndexBuffer.InitResource(RHICmdList);
			InitialGeometry.Reset();
		}
		virtual ~FMeshLayerEditSceneProxy() override
		{
			VertexFactory.ReleaseResource();
			IndexBuffer.ReleaseResource();
			VertexBuffers.PositionVertexBuffer.ReleaseResource();
			VertexBuffers.StaticMeshVertexBuffer.ReleaseResource();
			VertexBuffers.ColorVertexBuffer.ReleaseResource();
		}
		virtual SIZE_T GetTypeHash() const override
		{
			static size_t TypeIdentity;
			return reinterpret_cast<SIZE_T>(&TypeIdentity);
		}
		virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views,
			const FSceneViewFamily&, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
		{
			// Only small engine-owned draw descriptors are transient. No vertex traversal or buffer upload here.
			if (DrawVertexCount == 0 || DrawIndexCount == 0) { return; }
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
			{
				if (!(VisibilityMap & (1u << ViewIndex))) { continue; }
				FMeshBatch& Mesh = Collector.AllocateMesh();
				Mesh.VertexFactory = &VertexFactory;
				Mesh.MaterialRenderProxy = MaterialProxy.Get();
				Mesh.Type = PT_TriangleList;
				Mesh.DepthPriorityGroup = SDPG_World;
				Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
				Mesh.bDisableBackfaceCulling = true;
				Mesh.CastShadow = false;
				FMeshBatchElement& Element = Mesh.Elements[0];
				Element.IndexBuffer = &IndexBuffer;
				Element.FirstIndex = 0;
				Element.NumPrimitives = DrawIndexCount / 3;
				Element.MinVertexIndex = 0;
				Element.MaxVertexIndex = DrawVertexCount - 1;
				FDynamicPrimitiveUniformBuffer& Uniform = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
				FPrimitiveUniformShaderParametersBuilder Parameters;
				BuildUniformShaderParameters(Parameters);
				Parameters.ReceivesDecals(false);
				Uniform.Set(Collector.GetRHICommandList(), Parameters);
				Element.PrimitiveUniformBufferResource = &Uniform.UniformBuffer;
				Collector.AddMesh(ViewIndex, Mesh);
			}
		}
		virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
		{
			FPrimitiveViewRelevance Result;
			Result.bDrawRelevance = IsShown(View) && !View->bIsSceneCapture && !View->bIsReflectionCapture && !View->bIsPlanarReflection;
			Result.bDynamicRelevance = true;
			Result.bRenderInMainPass = ShouldRenderInMainPass();
			MaterialRelevance.SetPrimitiveViewRelevance(Result);
			return Result;
		}
		// PDI fill had no temporal primitive-occlusion gate. Keep immediate visibility and ordinary depth testing.
		virtual bool CanBeOccluded() const override { return false; }
		virtual uint32 GetMemoryFootprint() const override
		{
			return static_cast<uint32>(sizeof(*this) + GetAllocatedSize() + IndexBuffer.Indices.GetAllocatedSize());
		}
		void SetFillColor(const FLinearColor& Color)
		{
			if (MaterialProxy->Color == Color) { return; }
			const auto* Parent = MaterialProxy->Parent;
			// Replacement destroys the old proxy; its destructor releases the uniform cache.
			// Explicit invalidation here would repeat that work immediately before destruction.
			MaterialProxy = MakeUnique<FColoredMaterialRenderProxy>(Parent, Color);
		}
		void UpdateGeometry(FRHICommandListImmediate& RHICmdList,
			const UE::ComposableCamera::MeshEditor::FEditPreviewGeometry& Geometry, uint64 Revision)
		{
			if (Revision == GeometryRevision) { return; }
			if (!ensure(Geometry.Vertices.Num() <= static_cast<int32>(VertexBuffers.PositionVertexBuffer.GetNumVertices())
				&& Geometry.Indices.Num() <= IndexBuffer.Indices.Num())) { return; }
			TRACE_CPUPROFILER_EVENT_SCOPE(CCS_MeshLayers_EditBufferUpdate);
			for (int32 Index = 0; Index < Geometry.Vertices.Num(); ++Index)
			{
				const auto& Vertex = Geometry.Vertices[Index];
				VertexBuffers.PositionVertexBuffer.VertexPosition(Index) = Vertex.Position;
				VertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(Index, Vertex.TangentX.ToFVector3f(), Vertex.GetTangentY(), Vertex.TangentZ.ToFVector3f());
				VertexBuffers.ColorVertexBuffer.VertexColor(Index) = Vertex.Color;
				VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(Index, 0, Vertex.TextureCoordinate[0]);
			}
			auto Upload = [&](FRHIBuffer* Buffer, const void* Data, uint32 Bytes)
			{
				if (Bytes == 0) { return; }
				void* Destination = RHICmdList.LockBuffer(Buffer, 0, Bytes, RLM_WriteOnly);
				FMemory::Memcpy(Destination, Data, Bytes); RHICmdList.UnlockBuffer(Buffer);
			};
			Upload(VertexBuffers.PositionVertexBuffer.VertexBufferRHI, VertexBuffers.PositionVertexBuffer.GetVertexData(),
				Geometry.Vertices.Num() * VertexBuffers.PositionVertexBuffer.GetStride());
			Upload(VertexBuffers.StaticMeshVertexBuffer.TangentsVertexBuffer.VertexBufferRHI,
				VertexBuffers.StaticMeshVertexBuffer.GetTangentData(),
				VertexBuffers.StaticMeshVertexBuffer.GetTangentSize() / VertexBuffers.PositionVertexBuffer.GetNumVertices() * Geometry.Vertices.Num());
			Upload(VertexBuffers.StaticMeshVertexBuffer.TexCoordVertexBuffer.VertexBufferRHI,
				VertexBuffers.StaticMeshVertexBuffer.GetTexCoordData(),
				VertexBuffers.StaticMeshVertexBuffer.GetTexCoordSize() / VertexBuffers.PositionVertexBuffer.GetNumVertices() * Geometry.Vertices.Num());
			Upload(VertexBuffers.ColorVertexBuffer.VertexBufferRHI, VertexBuffers.ColorVertexBuffer.GetVertexData(),
				Geometry.Vertices.Num() * VertexBuffers.ColorVertexBuffer.GetStride());
			if (!Geometry.Indices.IsEmpty()) { FMemory::Memcpy(IndexBuffer.Indices.GetData(), Geometry.Indices.GetData(), Geometry.Indices.Num() * sizeof(uint32)); }
			Upload(IndexBuffer.IndexBufferRHI, IndexBuffer.Indices.GetData(), Geometry.Indices.Num() * sizeof(uint32));
			DrawVertexCount = Geometry.Vertices.Num(); DrawIndexCount = Geometry.Indices.Num();
			GeometryRevision = Revision;
		}
	private:
		FStaticMeshVertexBuffers VertexBuffers;
		FDynamicMeshIndexBuffer32 IndexBuffer;
		FLocalVertexFactory VertexFactory;
		FMaterialRelevance MaterialRelevance;
		TUniquePtr<FColoredMaterialRenderProxy> MaterialProxy;
		TSharedPtr<const UE::ComposableCamera::MeshEditor::FEditPreviewGeometry, ESPMode::ThreadSafe> InitialGeometry;
		uint64 GeometryRevision = 0;
		uint32 DrawVertexCount = 0, DrawIndexCount = 0;
	};

	using UE::ComposableCamera::MeshEditor::EditPreviewTileCells;
	using UE::ComposableCamera::MeshEditor::FEditTileGeometryByLayer;
	FEditTileGeometryByLayer BuildEditTileSnapshot(TConstArrayView<UE::ComposableCamera::MeshEditor::FResolvedSurfaceCell> Cells,
		TConstArrayView<uint8> EnabledLayers, const std::atomic_bool* Alive = nullptr)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CCS_MeshLayers_EditTileWorker);
		FEditTileGeometryByLayer Geometry;
		for (const auto& Cell : Cells)
		{
			if (Alive && !Alive->load(std::memory_order_relaxed)) { return {}; }
			for (const auto& Patch : Cell.Patches)
			{
				if (Alive && !Alive->load(std::memory_order_relaxed)) { return {}; }
				if (!EnabledLayers.IsValidIndex(Patch.LayerIndex) || !EnabledLayers[Patch.LayerIndex] || Patch.LocalVertices.Num() < 3) { continue; }
				auto& Layer = Geometry.FindOrAdd(Patch.LayerIndex);
				UE::ComposableCamera::MeshEditor::AppendVisualizationPatch(Patch, Layer.Vertices, Layer.Indices);
			}
		}
		return Geometry;
	}
	FIntPoint PreviewTile(const FVector2D& Point, double TileSize)
	{
		return FIntPoint(FMath::FloorToInt(Point.X / TileSize), FMath::FloorToInt(Point.Y / TileSize));
	}
	bool SameEditGeometry(const UE::ComposableCamera::MeshEditor::FEditPreviewGeometry& Existing,
		TConstArrayView<FDynamicMeshVertex> Vertices, TConstArrayView<uint32> Indices)
	{
		if (Existing.Indices.Num() != Indices.Num() || Existing.Vertices.Num() != Vertices.Num()) { return false; }
		if (!Indices.IsEmpty() && FMemory::Memcmp(Existing.Indices.GetData(), Indices.GetData(), Indices.Num() * sizeof(uint32)) != 0) { return false; }
		for (int32 Index = 0; Index < Vertices.Num(); ++Index)
		{
			const auto& A = Existing.Vertices[Index]; const auto& B = Vertices[Index];
			if (A.Position != B.Position || A.TangentX != B.TangentX || A.TangentZ != B.TangentZ || A.Color != B.Color) { return false; }
			for (int32 UV = 0; UV < static_cast<int32>(UE_ARRAY_COUNT(A.TextureCoordinate)); ++UV)
			{ if (A.TextureCoordinate[UV] != B.TextureCoordinate[UV]) { return false; } }
		}
		return true;
	}
}

UComposableCameraMeshLayerEditPreviewComponent::UComposableCameraMeshLayerEditPreviewComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bSelectable = false;
	bReceivesDecals = false;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetCanEverAffectNavigation(false);
	SetCastShadow(false);
	SetVisibleInRayTracing(false);
	SetMobility(EComponentMobility::Movable);
}

void UComposableCameraMeshLayerEditPreviewComponent::SetGeometry(TArray<FDynamicMeshVertex>&& InVertices,
	TArray<uint32>&& InIndices, const FLinearColor& InColor)
{
	// A stamp can affect a tile's bounds without changing this Layer's visible fill.
	// Avoid recreating its proxy/buffers in that case; compare actual render attributes, not padding.
	const bool bSame = Geometry && SameEditGeometry(*Geometry, InVertices, InIndices);
	if (bSame) { SetFillColor(InColor); return; }
	auto Prepared = MakeShared<UE::ComposableCamera::MeshEditor::FEditPreviewGeometry, ESPMode::ThreadSafe>();
	Prepared->Vertices = MoveTemp(InVertices); Prepared->Indices = MoveTemp(InIndices);
	FBox VertexBounds(ForceInit);
	for (const FDynamicMeshVertex& Vertex : Prepared->Vertices) { VertexBounds += FVector(Vertex.Position); }
	Prepared->LocalBounds = VertexBounds.IsValid ? FBoxSphereBounds(VertexBounds) : FBoxSphereBounds(ForceInit);
	SetSharedGeometry(MoveTemp(Prepared), InColor);
}

void UComposableCameraMeshLayerEditPreviewComponent::SetSharedGeometry(
	TSharedPtr<const UE::ComposableCamera::MeshEditor::FEditPreviewGeometry, ESPMode::ThreadSafe> InGeometry, const FLinearColor& InColor)
{
	if (Geometry == InGeometry) { SetFillColor(InColor); return; }
	if (Geometry && InGeometry && Geometry->LocalBounds.Origin == InGeometry->LocalBounds.Origin
		&& Geometry->LocalBounds.BoxExtent == InGeometry->LocalBounds.BoxExtent
		&& Geometry->LocalBounds.SphereRadius == InGeometry->LocalBounds.SphereRadius
		&& SameEditGeometry(*Geometry, InGeometry->Vertices, InGeometry->Indices))
	{
		SetFillColor(InColor);
		UE::ComposableCamera::MeshEditor::AsyncMeshLayerEdit([Retired = MoveTemp(InGeometry)]() mutable { Retired.Reset(); });
		return;
	}
	const bool bReuseBuffers = Geometry && InGeometry && SceneProxy && !IsRenderStateDirty()
		&& static_cast<uint32>(InGeometry->Vertices.Num()) <= RenderVertexCapacity
		&& static_cast<uint32>(InGeometry->Indices.Num()) <= RenderIndexCapacity;
	if (Geometry)
	{
		// Eviction/restoration must not turn an obsolete buffer's final release into a bulk-free hitch.
		UE::ComposableCamera::MeshEditor::AsyncMeshLayerEdit( [Retired = MoveTemp(Geometry)]() mutable { Retired.Reset(); });
	}
	Geometry = MoveTemp(InGeometry);
	FillColor = InColor;
	++GeometryRevision;
	UpdateBounds();
	if (bReuseBuffers) { MarkRenderTransformDirty(); MarkRenderDynamicDataDirty(); }
	else { MarkRenderStateDirty(); }
}

void UComposableCameraMeshLayerEditPreviewComponent::SetFillColor(const FLinearColor& InColor)
{
	if (FillColor == InColor) { return; }
	FillColor = InColor;
	MarkRenderDynamicDataDirty();
}

void UComposableCameraMeshLayerEditPreviewComponent::SendRenderDynamicData_Concurrent()
{
	Super::SendRenderDynamicData_Concurrent();
	if (SceneProxy && !IsRenderStateDirty())
	{
		// This component exclusively creates FMeshLayerEditSceneProxy. Commands follow proxy lifetime ordering.
		auto* Proxy = static_cast<FMeshLayerEditSceneProxy*>(SceneProxy);
		ENQUEUE_RENDER_COMMAND(CCSMeshEditUpdate)([Proxy, Color = FillColor, Data = Geometry, Revision = GeometryRevision](FRHICommandListImmediate& RHICmdList)
		{ Proxy->SetFillColor(Color); if (Data) { Proxy->UpdateGeometry(RHICmdList, *Data, Revision); } });
	}
}

TConstArrayView<FDynamicMeshVertex> UComposableCameraMeshLayerEditPreviewComponent::GetVertices() const
{
	return Geometry ? TConstArrayView<FDynamicMeshVertex>(Geometry->Vertices) : TConstArrayView<FDynamicMeshVertex>();
}
TConstArrayView<uint32> UComposableCameraMeshLayerEditPreviewComponent::GetIndices() const
{
	return Geometry ? TConstArrayView<uint32>(Geometry->Indices) : TConstArrayView<uint32>();
}

FPrimitiveSceneProxy* UComposableCameraMeshLayerEditPreviewComponent::CreateSceneProxy()
{
	RenderVertexCapacity = Geometry ? EditBufferCapacity(Geometry->Vertices.Num()) : 0;
	RenderIndexCapacity = Geometry ? EditBufferCapacity(Geometry->Indices.Num()) : 0;
	return Geometry && !Geometry->Vertices.IsEmpty() && !Geometry->Indices.IsEmpty() && GetMaterial(0) ? new FMeshLayerEditSceneProxy(*this) : nullptr;
}

FBoxSphereBounds UComposableCameraMeshLayerEditPreviewComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	return Geometry ? Geometry->LocalBounds.TransformBy(LocalToWorld) : FBoxSphereBounds(ForceInit);
}

namespace UE::ComposableCamera::MeshEditor
{
	static FPreparedEditPreview PrepareEditPreviewImpl(const FResolvedSurfaceVisualization& Visualization,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers, const std::atomic_bool* Alive, const FBox2D* DirtyBounds,
		bool bShareBuffers, const TConstArrayView<FIntPoint>* ExplicitTiles, const FVector2D* ExplicitFocus)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CCS_MeshLayers_EditDocumentTileWorker);
		FPreparedEditPreview Result; Result.CellSize = Visualization.CellSize;
		if (!FMath::IsFinite(Result.CellSize) || Result.CellSize <= 0.0) { return Result; }
		const bool bRegional = ExplicitTiles || (DirtyBounds && DirtyBounds->bIsValid);
		TMap<FIntPoint, int32> TileIndices;
		const double TileSize = Result.CellSize * EditPreviewTileCells;
		TArray<int32> RegionalCells;
		if (bRegional)
		{
			TArray<FIntPoint> RequestedTiles;
			if (ExplicitTiles) { RequestedTiles.Append(ExplicitTiles->GetData(), ExplicitTiles->Num()); }
			else
			{
				const FIntPoint Min = PreviewTile(DirtyBounds->Min, TileSize), Max = PreviewTile(DirtyBounds->Max, TileSize);
				for (int32 Y = Min.Y; Y <= Max.Y; ++Y) { for (int32 X = Min.X; X <= Max.X; ++X) { RequestedTiles.Add(FIntPoint(X, Y)); } }
			}
			for (const FIntPoint& Tile : RequestedTiles)
			{
				if (TileIndices.Contains(Tile)) { continue; }
				const int32 Added = Result.Tiles.AddDefaulted(); Result.Tiles[Added].Tile = Tile;
				if (Alive && !Alive->load(std::memory_order_relaxed)) { return {}; }
				TileIndices.Add(Tile, Added);
				for (int32 Cell = 0; Cell < EditPreviewTileCells * EditPreviewTileCells; ++Cell)
				{
					const FIntPoint Grid(Tile.X * EditPreviewTileCells + Cell % EditPreviewTileCells, Tile.Y * EditPreviewTileCells + Cell / EditPreviewTileCells);
					if (const auto* Indices = Visualization.CellsByGrid.Find(Grid)) { RegionalCells.Append(Indices->GetData(), Indices->Num()); }
				}
			}
		}
		const int32 CellCount = bRegional ? RegionalCells.Num() : Visualization.Cells.Num();
		for (int32 CellIndex = 0; CellIndex < CellCount; ++CellIndex)
		{
			const auto& Cell = Visualization.Cells[bRegional ? RegionalCells[CellIndex] : CellIndex];
			if (Alive && !Alive->load(std::memory_order_relaxed)) { return {}; }
			const FIntPoint Tile = PreviewTile(FVector2D(Cell.LocalPosition.X, Cell.LocalPosition.Y), TileSize);
			int32* Index = TileIndices.Find(Tile);
			if (!Index) { const int32 Added = Result.Tiles.AddDefaulted(); Result.Tiles[Added].Tile = Tile; Index = &TileIndices.Add(Tile, Added); }
			for (const auto& Patch : Cell.Patches)
			{
				if (Alive && !Alive->load(std::memory_order_relaxed)) { return {}; }
				if (!Layers.IsValidIndex(Patch.LayerIndex) || !Layers[Patch.LayerIndex].bEnabled || Patch.LocalVertices.Num() < 3) { continue; }
				auto& Geometry = Result.Tiles[*Index].Geometry.FindOrAdd(Patch.LayerIndex);
				AppendVisualizationPatch(Patch, Geometry.Vertices, Geometry.Indices);
			}
		}
		const FVector2D Focus = ExplicitFocus ? *ExplicitFocus : DirtyBounds && DirtyBounds->bIsValid ? DirtyBounds->GetCenter()
			: Visualization.LocalBounds.bIsValid ? Visualization.LocalBounds.GetCenter() : FVector2D::ZeroVector;
		Result.Tiles.Sort([&](const FPreparedEditPreviewTile& A, const FPreparedEditPreviewTile& B)
		{
			const double DA = (FVector2D(A.Tile.X + 0.5, A.Tile.Y + 0.5) * TileSize - Focus).SizeSquared();
			const double DB = (FVector2D(B.Tile.X + 0.5, B.Tile.Y + 0.5) * TileSize - Focus).SizeSquared();
			return DA != DB ? DA < DB : (A.Tile.Y != B.Tile.Y ? A.Tile.Y < B.Tile.Y : A.Tile.X < B.Tile.X);
		});
		if (bShareBuffers)
		{
			for (auto& Tile : Result.Tiles) { for (auto& Pair : Tile.Geometry)
			{
				if (Alive && !Alive->load(std::memory_order_relaxed)) { return {}; }
				auto Geometry = MakeShared<FEditPreviewGeometry, ESPMode::ThreadSafe>();
				Geometry->Vertices = MoveTemp(Pair.Value.Vertices); Geometry->Indices = MoveTemp(Pair.Value.Indices);
				FBox Bounds(ForceInit); for (const auto& Vertex : Geometry->Vertices) { Bounds += FVector(Vertex.Position); }
				Geometry->LocalBounds = Bounds.IsValid ? FBoxSphereBounds(Bounds) : FBoxSphereBounds(ForceInit);
				Tile.SharedGeometry.Add(Pair.Key, {MoveTemp(Geometry), FLinearColor::White, true});
			} Tile.Geometry.Reset(); }
		}
		return Result;
	}
	FPreparedEditPreview PrepareEditPreview(const FResolvedSurfaceVisualization& Visualization,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers, const std::atomic_bool* Alive, const FBox2D* DirtyBounds, bool bShareBuffers)
	{
		return PrepareEditPreviewImpl(Visualization, Layers, Alive, DirtyBounds, bShareBuffers, nullptr, nullptr);
	}
	FPreparedEditPreview PrepareEditPreviewTiles(const FResolvedSurfaceVisualization& Visualization,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers, TConstArrayView<FIntPoint> Tiles,
		const FVector2D& Focus, const std::atomic_bool* Alive, bool bShareBuffers)
	{
		return PrepareEditPreviewImpl(Visualization, Layers, Alive, nullptr, bShareBuffers, &Tiles, &Focus);
	}
	struct FEditPreviewUpdate
	{
		TArray<FIntPoint> Tiles;
		TMap<FIntPoint, TArray<int32>> FullCells;
		TMap<int32, FEditTileGeometry> Geometry;
		TMap<int32, FSharedEditPreviewGeometry> SharedGeometry;
		int32 TileIndex = 0, Cell = 0;
		bool bFull = false;
	};
	using FQueuedEditTileResult = FPreparedEditPreviewTile;
	struct FQueuedEditPreviewUpdate
	{
		// One latest waiting snapshot per tile, plus one worker-owned batch of up to four.
		TMap<FIntPoint, TArray<FResolvedSurfaceCell>> Snapshots;
		TArray<FIntPoint> Order;
		TFuture<TArray<FQueuedEditTileResult>> Future;
		TArray<FQueuedEditTileResult> Ready;
		TSet<FIntPoint> PreparedTiles;
		TSet<FIntPoint> SupersededActiveTiles;
		int32 ActiveTiles = 0, ReadyIndex = 0;
		bool bAwaitingDocument = false;
		bool bRestore = false;
		TSharedRef<std::atomic_bool, ESPMode::ThreadSafe> Alive = MakeShared<std::atomic_bool, ESPMode::ThreadSafe>(true);
		~FQueuedEditPreviewUpdate() { Alive->store(false, std::memory_order_relaxed); }
	};
	struct FEditPreviewHistory
	{
		FGuid Revision;
		double CellSize = 10.0;
		TMap<FIntVector, FSharedEditPreviewGeometry> Tiles;
		TSharedPtr<const FEditPreviewCheckpoint, ESPMode::ThreadSafe> Checkpoint;
	};
	FMeshLayerEditPreview::FMeshLayerEditPreview() = default;
	void FMeshLayerEditPreview::CancelUpdate()
	{
		if (QueuedUpdate) { QueuedUpdate->Alive->store(false, std::memory_order_relaxed); }
		if (PendingUpdate || QueuedUpdate)
		{
			// Both states own native arrays only. Undo/Discard should cancel promptly,
			// rather than freeing all unpublished polygons/mesh buffers on the editor thread.
			UE::ComposableCamera::MeshEditor::AsyncMeshLayerEdit(
				[Pending = MoveTemp(PendingUpdate), Queued = MoveTemp(QueuedUpdate)]() mutable { Pending.Reset(); Queued.Reset(); });
		}
	}
	bool FMeshLayerEditPreview::HasQueuedUpdates() const { return !!QueuedUpdate; }
	bool FMeshLayerEditPreview::IsRestoringRevision() const { return QueuedUpdate && QueuedUpdate->bRestore; }
	TSharedPtr<const FEditPreviewCheckpoint, ESPMode::ThreadSafe> FMeshLayerEditPreview::FindCheckpoint(const FGuid& Revision) const
	{
		const auto* Item = History.FindByPredicate([&](const auto& Snapshot) { return Snapshot->Revision == Revision; });
		return Item ? (*Item)->Checkpoint : nullptr;
	}
	bool FMeshLayerEditPreview::RememberRevision(const FGuid& Revision, const FGuid& SavedRevision,
		TSharedPtr<const FEditPreviewCheckpoint, ESPMode::ThreadSafe> Checkpoint)
	{
		if (!Revision.IsValid() || !IsReadyFor(OwnerLevel.Get()) || PendingUpdate || QueuedUpdate) { return false; }
		if (Revision == LastRememberedRevision && (!Checkpoint || FindCheckpoint(Revision))) { return true; }
		TRACE_CPUPROFILER_EVENT_SCOPE(CCS_MeshLayers_EditRememberRevision);
		if (!History.ContainsByPredicate([&](const auto& Item) { return Item->Revision == Revision; }))
		{
			auto Snapshot = MakeUnique<FEditPreviewHistory>(); Snapshot->Revision = Revision; Snapshot->CellSize = CellSize;
			Snapshot->Tiles.Reserve(Components.Num());
			for (const auto& Pair : Components)
			{
				const auto* Component = Pair.Value.Get();
				if (!Component || !Component->GetSharedGeometry()) { return false; }
				Snapshot->Tiles.Add(Pair.Key, {Component->GetSharedGeometry(), Component->GetFillColor()});
			}
			History.Add(MoveTemp(Snapshot));
		}
		if (Checkpoint)
		{
			auto* Item = History.FindByPredicate([&](const auto& Snapshot) { return Snapshot->Revision == Revision; });
			if (!(*Item)->Checkpoint) { (*Item)->Checkpoint = MoveTemp(Checkpoint); }
		}
		LastRememberedRevision = Revision;
		// Bound editor-only history. Count unique retained buffers, excluding current
		// live buffers: unchanged tiles cost references, not repeated vertex arrays.
		auto RetainedBytes = [&]()
		{
			TSet<const FEditPreviewGeometry*> Seen;
			for (const auto& Pair : Components) { if (const auto* Component = Pair.Value.Get()) { Seen.Add(Component->GetSharedGeometry().Get()); } }
			uint64 Bytes = 0;
			TSet<const FEditPreviewCheckpoint*> SeenCheckpoints;
			for (const auto& Item : History)
			{
				if (Item->Checkpoint && Item->Revision != Revision && !SeenCheckpoints.Contains(Item->Checkpoint.Get()))
				{
					SeenCheckpoints.Add(Item->Checkpoint.Get()); Bytes += Item->Checkpoint->AllocatedBytes;
				}
				for (const auto& Pair : Item->Tiles)
				{
					const auto* Geometry = Pair.Value.Geometry.Get();
					if (Geometry && !Seen.Contains(Geometry))
					{
						Seen.Add(Geometry); Bytes += Geometry->Vertices.GetAllocatedSize() + Geometry->Indices.GetAllocatedSize();
					}
				}
			}
			return Bytes;
		};
		while (History.Num() > 32 || RetainedBytes() > 128ull * 1024 * 1024)
		{
			const int32 Oldest = History.IndexOfByPredicate([&](const auto& Item) { return Item->Revision != SavedRevision && Item->Revision != Revision; });
			if (Oldest == INDEX_NONE) { break; } // The saved checkpoint/current revision are never evicted.
			auto Retired = MoveTemp(History[Oldest]); History.RemoveAt(Oldest, 1, EAllowShrinking::No);
			UE::ComposableCamera::MeshEditor::AsyncMeshLayerEdit( [Retired = MoveTemp(Retired)]() mutable { Retired.Reset(); });
		}
		return true;
	}

	bool FMeshLayerEditPreview::QueueRestoreRevision(ULevel& Level, const FGuid& Revision)
	{
		UWorld* World = Level.GetWorld();
		if (OwnerLevel.Get() != &Level || !World || World->WorldType != EWorldType::Editor || World->IsBeingCleanedUp()
			|| (!Components.IsEmpty() && !Actor.IsValid()) || !GEngine || !GEngine->GeomMaterial) { return false; }
		const auto* Saved = History.FindByPredicate([&](const auto& Item) { return Item->Revision == Revision; });
		if (!Saved) { return false; }
		TRACE_CPUPROFILER_EVENT_SCOPE(CCS_MeshLayers_EditRestoreRevision);
		const auto& Snapshot = **Saved;
		CancelUpdate(); QueuedUpdate = MakeUnique<FQueuedEditPreviewUpdate>(); QueuedUpdate->bRestore = true;
		TSet<FIntPoint> Changed;
		for (const auto& Pair : Components)
		{
			const auto* Component = Pair.Value.Get(); const auto* Expected = Snapshot.Tiles.Find(Pair.Key);
			if (!Component || !Expected || Component->GetSharedGeometry() != Expected->Geometry || Component->GetFillColor() != Expected->Color)
			{ Changed.Add(FIntPoint(Pair.Key.X, Pair.Key.Y)); }
		}
		for (const auto& Pair : Snapshot.Tiles)
		{
			const auto* Component = Components.FindRef(Pair.Key).Get();
			if (!Component || Component->GetSharedGeometry() != Pair.Value.Geometry || Component->GetFillColor() != Pair.Value.Color)
			{ Changed.Add(FIntPoint(Pair.Key.X, Pair.Key.Y)); }
		}
		TMap<FIntPoint, int32> ReadyIndices;
		for (const auto& Tile : Changed) { ReadyIndices.Add(Tile, QueuedUpdate->Ready.Add({Tile, {}})); }
		for (const auto& Pair : Snapshot.Tiles)
		{
			if (const auto* Index = ReadyIndices.Find(FIntPoint(Pair.Key.X, Pair.Key.Y)))
			{ QueuedUpdate->Ready[*Index].SharedGeometry.Add(Pair.Key.Z, Pair.Value); }
		}
		CellSize = Snapshot.CellSize;
		return true;
	}
	int32 FMeshLayerEditPreview::GetQueuedTileCount() const
	{
		return QueuedUpdate ? QueuedUpdate->Snapshots.Num() + QueuedUpdate->ActiveTiles + QueuedUpdate->Ready.Num() - QueuedUpdate->ReadyIndex : 0;
	}
	int32 FMeshLayerEditPreview::GetWaitingTileCount() const
	{
		return QueuedUpdate ? QueuedUpdate->Snapshots.Num() : 0;
	}

	FMeshLayerEditPreview::~FMeshLayerEditPreview() { Reset(); }
	AActor* FMeshLayerEditPreview::GetActor() const { return Actor.Get(); }

	bool FMeshLayerEditPreview::IsReadyFor(const ULevel* Level) const
	{
		const AActor* PreviewActor = Actor.Get();
		return bInitialized && Level && OwnerLevel.Get() == Level && Level->GetWorld()
			&& Level->GetWorld()->WorldType == EWorldType::Editor && !Level->GetWorld()->IsBeingCleanedUp()
			&& (Components.IsEmpty() || (PreviewActor && !PreviewActor->IsActorBeingDestroyed() && PreviewActor->GetLevel() == Level));
	}

	void FMeshLayerEditPreview::Reset()
	{
		CancelUpdate();
		if (!History.IsEmpty()) { UE::ComposableCamera::MeshEditor::AsyncMeshLayerEdit( [Retired = MoveTemp(History)]() mutable { Retired.Reset(); }); }
		LastRememberedRevision.Invalidate();
		if (AActor* PreviewActor = Actor.Get())
		{
			if (UWorld* World = PreviewActor->GetWorld(); World && !World->IsBeingCleanedUp()) { PreviewActor->Destroy(); }
		}
		Actor.Reset(); OwnerLevel.Reset(); Components.Empty(); CellSize = 0.0; ComponentLayerCount = 0; bInitialized = false;
	}

	bool FMeshLayerEditPreview::BeginUpdate(ULevel& Level, const FResolvedSurfaceVisualization& Visualization, const FBox2D* DirtyBounds)
	{
		QueuedUpdate.Reset();
		UWorld* World = Level.GetWorld();
		if (!World || World->WorldType != EWorldType::Editor || World->IsBeingCleanedUp()
			|| !GEngine || !GEngine->GeomMaterial || Visualization.CellSize <= 0.0) { Reset(); return false; }
		const bool bReady = IsReadyFor(&Level);
		const bool bFull = !bReady || !DirtyBounds || !DirtyBounds->bIsValid || CellSize != Visualization.CellSize;
		if (OwnerLevel.Get() != &Level || (!bReady && !Components.IsEmpty())) { Reset(); }
		PendingUpdate = MakeUnique<FEditPreviewUpdate>();
		PendingUpdate->bFull = bFull;
		TSet<FIntPoint> Tiles;
		const double TileSize = Visualization.CellSize * EditPreviewTileCells;
		if (bFull)
		{
			for (int32 Index = 0; Index < Visualization.Cells.Num(); ++Index)
			{
				const auto& Cell = Visualization.Cells[Index];
				const FIntPoint Tile = PreviewTile(FVector2D(Cell.LocalPosition.X, Cell.LocalPosition.Y), TileSize);
				Tiles.Add(Tile); PendingUpdate->FullCells.FindOrAdd(Tile).Add(Index);
			}
			for (const auto& Pair : Components) { Tiles.Add(FIntPoint(Pair.Key.X, Pair.Key.Y)); }
		}
		else
		{
			const FIntPoint Min = PreviewTile(DirtyBounds->Min, TileSize), Max = PreviewTile(DirtyBounds->Max, TileSize);
			for (int32 Y = Min.Y; Y <= Max.Y; ++Y) { for (int32 X = Min.X; X <= Max.X; ++X) { Tiles.Add(FIntPoint(X, Y)); } }
		}
		PendingUpdate->Tiles = Tiles.Array();
		OwnerLevel = &Level; CellSize = Visualization.CellSize;
		return true;
	}

	bool FMeshLayerEditPreview::QueueUpdate(ULevel& Level, const FResolvedSurfaceVisualization& Visualization, const FBox2D* DirtyBounds)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CCS_MeshLayers_EditTileSnapshot);
		UWorld* World = Level.GetWorld();
		if (!World || World->WorldType != EWorldType::Editor || World->IsBeingCleanedUp()
			|| !GEngine || !GEngine->GeomMaterial || Visualization.CellSize <= 0.0) { Reset(); return false; }
		const bool bReady = IsReadyFor(&Level);
		const bool bFull = OwnerLevel.Get() != &Level || !DirtyBounds || !DirtyBounds->bIsValid
			|| CellSize != Visualization.CellSize || (!bReady && !QueuedUpdate);
		if (OwnerLevel.Get() != &Level || (!bReady && !QueuedUpdate && !Components.IsEmpty())) { Reset(); }
		// A full/regridded result supersedes every older snapshot, including an active
		// future. Its worker owns plain copies and has no publication callback to cancel.
		if (bFull || !QueuedUpdate) { QueuedUpdate = MakeUnique<FQueuedEditPreviewUpdate>(); }
		PendingUpdate.Reset();
		auto& Work = *QueuedUpdate;
		const double TileSize = Visualization.CellSize * EditPreviewTileCells;
		auto PrepareTile = [&](const FIntPoint& Tile) -> TArray<FResolvedSurfaceCell>&
		{
			if (Work.bRestore)
			{
				// This stroke replaces the restored version of its tile. Keep every
				// untouched restoration tile, including removals and other Layer rows.
				for (int32 Index = Work.Ready.Num() - 1; Index >= Work.ReadyIndex; --Index)
				{
					if (Work.Ready[Index].Tile == Tile) { Work.Ready.RemoveAt(Index, 1, EAllowShrinking::No); }
				}
			}
			if (!Work.Snapshots.Contains(Tile)) { Work.Order.Add(Tile); }
			auto& Cells = Work.Snapshots.FindOrAdd(Tile); Cells.Reset(); return Cells;
		};
		if (bFull)
		{
			for (const auto& Pair : Components) { PrepareTile(FIntPoint(Pair.Key.X, Pair.Key.Y)); }
			// Same per-tile order as the original full update. Copy actual resolved
			// polygons, including every Layer/elevation; never infer fill from bounds.
			for (const auto& Cell : Visualization.Cells)
			{
				const FIntPoint Tile = PreviewTile(FVector2D(Cell.LocalPosition.X, Cell.LocalPosition.Y), TileSize);
				if (!Work.Snapshots.Contains(Tile)) { PrepareTile(Tile); }
				Work.Snapshots.FindChecked(Tile).Add(Cell);
			}
		}
		else
		{
			const FIntPoint Min = PreviewTile(DirtyBounds->Min, TileSize), Max = PreviewTile(DirtyBounds->Max, TileSize);
			for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
			{
				for (int32 X = Min.X; X <= Max.X; ++X)
				{
					auto& Cells = PrepareTile(FIntPoint(X, Y));
					// Retain unchanged neighbors so each whole tile remains correct when
					// published. Coverage may mutate immediately after this owned snapshot.
					for (int32 Cell = 0; Cell < EditPreviewTileCells * EditPreviewTileCells; ++Cell)
					{
						const FIntPoint Grid(X * EditPreviewTileCells + Cell % EditPreviewTileCells,
							Y * EditPreviewTileCells + Cell / EditPreviewTileCells);
						if (const auto* Indices = Visualization.CellsByGrid.Find(Grid))
						{
							for (int32 Index : *Indices) { Cells.Add(Visualization.Cells[Index]); }
						}
					}
				}
			}
		}
		const FVector2D Focus = DirtyBounds && DirtyBounds->bIsValid ? DirtyBounds->GetCenter()
			: Visualization.LocalBounds.bIsValid ? Visualization.LocalBounds.GetCenter() : FVector2D::ZeroVector;
		Work.Order.Sort([&](const FIntPoint& A, const FIntPoint& B)
		{
			const double DA = (FVector2D(A.X + 0.5, A.Y + 0.5) * TileSize - Focus).SizeSquared();
			const double DB = (FVector2D(B.X + 0.5, B.Y + 0.5) * TileSize - Focus).SizeSquared();
			return DA != DB ? DA < DB : (A.Y != B.Y ? A.Y < B.Y : A.X < B.X);
		});
		OwnerLevel = &Level; CellSize = Visualization.CellSize;
		return true;
	}

	bool FMeshLayerEditPreview::QueuePreparedUpdate(ULevel& Level, FPreparedEditPreview&& Prepared, bool bComplete)
	{
		UWorld* World = Level.GetWorld();
		if (!World || World->WorldType != EWorldType::Editor || World->IsBeingCleanedUp()
			|| !GEngine || !GEngine->GeomMaterial || Prepared.CellSize <= 0.0) { Reset(); return false; }
		if (OwnerLevel.Get() != &Level || (!Actor.IsValid() && !Components.IsEmpty())) { Reset(); }
		const bool bContinue = QueuedUpdate && QueuedUpdate->bAwaitingDocument && CellSize == Prepared.CellSize;
		if (!bContinue) { CancelUpdate(); QueuedUpdate = MakeUnique<FQueuedEditPreviewUpdate>(); }
		auto& Work = *QueuedUpdate; Work.bAwaitingDocument = !bComplete;
		for (auto& Tile : Prepared.Tiles)
		{
			Work.PreparedTiles.Add(Tile.Tile);
			// A tiny first region and its complete tile can arrive in the same
			// frame. Retain only the latest unpublished geometry for that tile.
			FQueuedEditTileResult* Waiting = nullptr;
			for (int32 Index = Work.ReadyIndex; Index < Work.Ready.Num(); ++Index)
			{
				if (Work.Ready[Index].Tile == Tile.Tile) { Waiting = &Work.Ready[Index]; break; }
			}
			if (Waiting) { *Waiting = MoveTemp(Tile); }
			else { Work.Ready.Add(MoveTemp(Tile)); }
		}
		if (bComplete)
		{
			for (const auto& Pair : Components)
			{
				const FIntPoint Tile(Pair.Key.X, Pair.Key.Y);
				if (!Work.PreparedTiles.Contains(Tile)) { Work.PreparedTiles.Add(Tile); Work.Ready.Add({Tile, {}}); }
			}
		}
		OwnerLevel = &Level; CellSize = Prepared.CellSize;
		return true;
	}

	bool FMeshLayerEditPreview::QueuePreparedRegion(ULevel& Level, FPreparedEditPreview&& Prepared, bool bFull)
	{
		if (bFull || OwnerLevel.Get() != &Level || CellSize != Prepared.CellSize)
		{ return QueuePreparedUpdate(Level, MoveTemp(Prepared)); }
		UWorld* World = Level.GetWorld();
		if (!World || World->WorldType != EWorldType::Editor || World->IsBeingCleanedUp()) { return false; }
		if (!QueuedUpdate) { QueuedUpdate = MakeUnique<FQueuedEditPreviewUpdate>(); }
		auto& Work = *QueuedUpdate;
		for (auto& Tile : Prepared.Tiles)
		{
			if (Work.Future.IsValid()) { Work.SupersededActiveTiles.Add(Tile.Tile); }
			Work.Snapshots.Remove(Tile.Tile); Work.Order.Remove(Tile.Tile);
			FQueuedEditTileResult* Waiting = nullptr;
			for (int32 Index = Work.ReadyIndex; Index < Work.Ready.Num(); ++Index)
			{ if (Work.Ready[Index].Tile == Tile.Tile) { Waiting = &Work.Ready[Index]; break; } }
			if (Waiting) { *Waiting = MoveTemp(Tile); } else { Work.Ready.Add(MoveTemp(Tile)); }
		}
		return true;
	}

	bool FMeshLayerEditPreview::PublishPreparedDocument(ULevel& Level, const FTransform& Anchor,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers, FPreparedEditPreview&& Prepared,
		const TSet<FIntPoint>* PreservedRegions)
	{
		UWorld* World = Level.GetWorld();
		if (!World || World->WorldType != EWorldType::Editor || World->IsBeingCleanedUp()
			|| !GEngine || !GEngine->GeomMaterial || !FMath::IsFinite(Prepared.CellSize) || Prepared.CellSize <= 0.0) { return false; }
		if (OwnerLevel.Get() != &Level || (CellSize != 0.0 && CellSize != Prepared.CellSize)) { Reset(); }
		CancelUpdate(); OwnerLevel = &Level; CellSize = Prepared.CellSize;
		TSet<FIntPoint> Regions;
		for (const auto& Tile : Prepared.Tiles) { Regions.Add(Tile.Tile); }
		for (auto It = Components.CreateIterator(); It; ++It)
		{
			const FIntPoint Region(It.Key().X, It.Key().Y);
			if (!Regions.Contains(Region) && (!PreservedRegions || !PreservedRegions->Contains(Region)))
			{ if (auto* Component = It.Value().Get()) { Component->DestroyComponent(); } It.RemoveCurrent(); }
		}
		// One editor/scene update: no ready cursor, per-frame tile budget, or partially visible load.
		// Components only install shared native geometry; vertex traversal/resource initialization runs off-thread.
		for (auto& Tile : Prepared.Tiles)
		{
			if (PreservedRegions && PreservedRegions->Contains(Tile.Tile)) { continue; }
			if (!PublishPreparedTile(Level, Anchor, Layers, MoveTemp(Tile))) { return false; }
		}
		// Skipped stale regions can still own large native buffers. Dispose of them off the input thread.
		AsyncMeshLayerEdit([Retired = MoveTemp(Prepared)]() mutable { Retired.Tiles.Reset(); });
		bInitialized = true; return true;
	}

	bool FMeshLayerEditPreview::PublishPreparedRegions(ULevel& Level, const FTransform& Anchor,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers, FPreparedEditPreview&& Prepared)
	{
		UWorld* World = Level.GetWorld();
		if (!World || World->WorldType != EWorldType::Editor || World->IsBeingCleanedUp()
			|| !GEngine || !GEngine->GeomMaterial || !FMath::IsFinite(Prepared.CellSize) || Prepared.CellSize <= 0.0) { return false; }
		if (OwnerLevel.Get() != &Level || (CellSize != 0.0 && CellSize != Prepared.CellSize)) { Reset(); }
		OwnerLevel = &Level; CellSize = Prepared.CellSize;
		for (auto& Tile : Prepared.Tiles) { if (!PublishPreparedTile(Level, Anchor, Layers, MoveTemp(Tile))) { return false; } }
		return true;
	}

	bool FMeshLayerEditPreview::PublishPreparedTile(ULevel& Level, const FTransform& Anchor,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers, FPreparedEditPreviewTile&& Tile)
	{
		PendingUpdate = MakeUnique<FEditPreviewUpdate>();
		PendingUpdate->Tiles.Add(Tile.Tile); PendingUpdate->bFull = true;
		PendingUpdate->Geometry = MoveTemp(Tile.Geometry); PendingUpdate->SharedGeometry = MoveTemp(Tile.SharedGeometry);
		FResolvedSurfaceVisualization Publication; Publication.CellSize = CellSize;
		AdvanceUpdate(Level, Anchor, Publication, Layers);
		return OwnerLevel.Get() == &Level;
	}

	void FMeshLayerEditPreview::UpdateLayerAppearance(TConstArrayView<FComposableCameraMeshLayerDefinition> Layers)
	{
		for (const auto& Pair : Components)
		{
			if (auto* Component = Pair.Value.Get(); Component && Layers.IsValidIndex(Pair.Key.Z))
			{
				FLinearColor Color = Layers[Pair.Key.Z].DebugColor; Color.A = FMath::Clamp(Color.A, 0.12f, 0.5f);
				Component->SetFillColor(Color);
			}
		}
	}

	void FMeshLayerEditPreview::LaunchQueuedTileWork(TConstArrayView<FComposableCameraMeshLayerDefinition> Layers)
	{
		if (!QueuedUpdate || QueuedUpdate->Future.IsValid()
			|| (!QueuedUpdate->bRestore && QueuedUpdate->ReadyIndex < QueuedUpdate->Ready.Num()) || QueuedUpdate->Order.IsEmpty()) { return; }
		auto& Work = *QueuedUpdate;
		struct FTileSnapshot { FIntPoint Tile; TArray<FResolvedSurfaceCell> Cells; };
		TArray<FTileSnapshot> Batch;
		Work.ActiveTiles = FMath::Min(4, Work.Order.Num()); Batch.Reserve(Work.ActiveTiles);
		for (int32 Index = 0; Index < Work.ActiveTiles; ++Index)
		{
			const FIntPoint Tile = Work.Order[Index];
			Batch.Add({Tile, MoveTemp(Work.Snapshots.FindChecked(Tile))}); Work.Snapshots.Remove(Tile);
		}
		Work.Order.RemoveAt(0, Work.ActiveTiles, EAllowShrinking::No);
		TArray<uint8> Enabled; Enabled.Reserve(Layers.Num());
		for (const auto& Layer : Layers) { Enabled.Add(Layer.bEnabled ? 1 : 0); }
		Work.Future = UE::ComposableCamera::MeshEditor::AsyncMeshLayerEdit( [Batch = MoveTemp(Batch), Enabled = MoveTemp(Enabled), Alive = Work.Alive]()
		{
			TArray<FQueuedEditTileResult> Result; Result.Reserve(Batch.Num());
			for (const auto& Snapshot : Batch)
			{
				if (!Alive->load(std::memory_order_relaxed)) { break; }
				Result.Add({Snapshot.Tile, BuildEditTileSnapshot(Snapshot.Cells, Enabled, &Alive.Get())});
			}
			return Result;
		});
	}

	bool FMeshLayerEditPreview::AdvanceQueuedUpdates(ULevel& Level, const FTransform& Anchor,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers, double TimeBudgetSeconds)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CCS_MeshLayers_EditTilePublication);
		if (!QueuedUpdate) { return true; }
		UWorld* World = Level.GetWorld();
		if (!World || World->WorldType != EWorldType::Editor || World->IsBeingCleanedUp() || OwnerLevel.Get() != &Level)
		{
			Reset(); return true;
		}
		const double Started = FPlatformTime::Seconds();
		while (QueuedUpdate)
		{
			if (TimeBudgetSeconds > 0.0 && FPlatformTime::Seconds() - Started >= TimeBudgetSeconds) { return false; }
			auto& Restore = *QueuedUpdate;
			if (Restore.bRestore && Restore.Future.IsValid() && Restore.Future.IsReady())
			{
				// Newly brushed tiles take priority over the remaining historical
				// restoration; a large restore cannot postpone the latest stroke.
				auto Completed = Restore.Future.Consume(); Restore.Future = TFuture<TArray<FQueuedEditTileResult>>(); Restore.ActiveTiles = 0;
				for (auto& Tile : Completed)
				{ if (!Restore.SupersededActiveTiles.Contains(Tile.Tile)) { Restore.Ready.Insert(MoveTemp(Tile), Restore.ReadyIndex); } }
				Restore.SupersededActiveTiles.Reset();
			}
			auto& Work = *QueuedUpdate;
			if (Work.ReadyIndex >= Work.Ready.Num())
			{
				Work.Ready.Reset(); Work.ReadyIndex = 0;
				if (Work.Future.IsValid())
				{
					if (TimeBudgetSeconds > 0.0 && !Work.Future.IsReady()) { return false; }
					auto Completed = Work.Future.Consume(); Work.Future = TFuture<TArray<FQueuedEditTileResult>>(); Work.ActiveTiles = 0;
					for (auto& Tile : Completed) { if (!Work.SupersededActiveTiles.Contains(Tile.Tile)) { Work.Ready.Add(MoveTemp(Tile)); } }
					Work.SupersededActiveTiles.Reset();
				}
				else
				{
					if (Work.Order.IsEmpty())
					{
						if (Work.bAwaitingDocument) { return false; } // More final regions can arrive in later Ticks.
						QueuedUpdate.Reset(); bInitialized = true; return true;
					}
					if (TimeBudgetSeconds > 0.0)
					{
						LaunchQueuedTileWork(Layers);
						return false; // Never wait in an ordinary editor frame.
					}
					const FIntPoint Tile = Work.Order[0]; Work.Order.RemoveAt(0, 1, EAllowShrinking::No);
					auto Cells = MoveTemp(Work.Snapshots.FindChecked(Tile)); Work.Snapshots.Remove(Tile);
					TArray<uint8> Enabled; Enabled.Reserve(Layers.Num());
					for (const auto& Layer : Layers) { Enabled.Add(Layer.bEnabled ? 1 : 0); }
					Work.Ready.Add({Tile, BuildEditTileSnapshot(Cells, Enabled)});
				}
				if (Work.Ready.IsEmpty()) { continue; }
			}
			// Reuse the unchanged tile publisher: no cell walk remains on this path,
			// only engine-owned components/buffers. Do not retain any reference into
			// Work across publication, because a failed Actor spawn can Reset it.
			PendingUpdate = MakeUnique<FEditPreviewUpdate>();
			auto& Result = Work.Ready[Work.ReadyIndex++];
			PendingUpdate->Tiles.Add(Result.Tile); PendingUpdate->bFull = true;
			PendingUpdate->Geometry = MoveTemp(Result.Geometry);
			PendingUpdate->SharedGeometry = MoveTemp(Result.SharedGeometry);
			FResolvedSurfaceVisualization Publication; Publication.CellSize = CellSize;
			AdvanceUpdate(Level, Anchor, Publication, Layers);
		}
		return true;
	}

	bool FMeshLayerEditPreview::AdvanceUpdate(ULevel& Level, const FTransform& Anchor,
		const FResolvedSurfaceVisualization& Visualization, TConstArrayView<FComposableCameraMeshLayerDefinition> Layers,
		int32 MaxCells, double TimeBudgetSeconds)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CCS_MeshLayers_EditPreviewUpdate);
		if (!PendingUpdate) { return true; }
		UWorld* World = Level.GetWorld();
		if (!World || World->IsBeingCleanedUp() || OwnerLevel.Get() != &Level || CellSize != Visualization.CellSize)
		{
			Reset(); return true;
		}
		auto& Work = *PendingUpdate;
		const double Started = FPlatformTime::Seconds();
		int32 Cells = 0;
		while (Work.TileIndex < Work.Tiles.Num())
		{
			if (Cells >= MaxCells || (TimeBudgetSeconds > 0.0 && FPlatformTime::Seconds() - Started >= TimeBudgetSeconds)) { return false; }
			const FIntPoint Tile = Work.Tiles[Work.TileIndex];
			const auto* FullIndices = Work.FullCells.Find(Tile);
			const int32 CellCount = Work.bFull ? (FullIndices ? FullIndices->Num() : 0) : EditPreviewTileCells * EditPreviewTileCells;
			if (Work.Cell < CellCount)
			{
				const FIntPoint Grid(Tile.X * EditPreviewTileCells + Work.Cell % EditPreviewTileCells,
					Tile.Y * EditPreviewTileCells + Work.Cell / EditPreviewTileCells);
				TArray<int32, TInlineAllocator<2>> Single;
				if (Work.bFull) { Single.Add((*FullIndices)[Work.Cell]); }
				const auto* Indices = Work.bFull ? &Single : Visualization.CellsByGrid.Find(Grid);
				++Work.Cell; ++Cells;
				if (Indices)
				{
					for (int32 Index : *Indices)
					{
						for (const auto& Patch : Visualization.Cells[Index].Patches)
						{
							if (!Layers.IsValidIndex(Patch.LayerIndex) || !Layers[Patch.LayerIndex].bEnabled || Patch.LocalVertices.Num() < 3) { continue; }
							auto& Geometry = Work.Geometry.FindOrAdd(Patch.LayerIndex);
							AppendVisualizationPatch(Patch, Geometry.Vertices, Geometry.Indices);
						}
					}
				}
				continue;
			}
			// Do not scan every document component for every region: that made a whole load O(regions squared).
			ComponentLayerCount = FMath::Max(ComponentLayerCount, Layers.Num());
			for (int32 Layer = 0; Layer < ComponentLayerCount; ++Layer)
			{
				if (!Work.Geometry.Contains(Layer) && !Work.SharedGeometry.Contains(Layer))
				{
					const FIntVector Key(Tile.X, Tile.Y, Layer);
					if (auto* Component = Components.FindRef(Key).Get()) { Component->DestroyComponent(); }
					Components.Remove(Key);
				}
			}
			if ((!Work.Geometry.IsEmpty() || !Work.SharedGeometry.IsEmpty()) && !Actor.IsValid())
			{
				FActorSpawnParameters Parameters;
				Parameters.OverrideLevel = &Level;
				Parameters.ObjectFlags = RF_Transient | RF_DuplicateTransient;
				Parameters.bHideFromSceneOutliner = true; Parameters.bTemporaryEditorActor = true;
				Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				AActor* PreviewActor = World->SpawnActor<AActor>(Parameters);
				if (!PreviewActor) { Reset(); return true; }
				Actor = PreviewActor;
				PreviewActor->bIsEditorOnlyActor = false; PreviewActor->bIgnoreInPIE = true;
				PreviewActor->SetActorHiddenInGame(false); PreviewActor->SetActorEnableCollision(false);
				USceneComponent* Root = NewObject<USceneComponent>(PreviewActor, NAME_None, RF_Transient);
				Root->SetMobility(EComponentMobility::Movable);
				PreviewActor->AddInstanceComponent(Root); PreviewActor->SetRootComponent(Root);
				PreviewActor->SetActorTransform(Anchor); Root->RegisterComponent();
			}
			AActor* PreviewActor = Actor.Get();
			if (PreviewActor && !PreviewActor->GetActorTransform().Equals(Anchor)) { PreviewActor->SetActorTransform(Anchor); }
			auto FindComponent = [&](int32 Layer)
			{
				const FIntVector Key(Tile.X, Tile.Y, Layer);
				auto* Component = Components.FindRef(Key).Get();
				if (!Component)
				{
					Component = NewObject<UComposableCameraMeshLayerEditPreviewComponent>(PreviewActor, NAME_None, RF_Transient);
					PreviewActor->AddInstanceComponent(Component); Component->SetupAttachment(PreviewActor->GetRootComponent());
					Component->SetMaterial(0, GEngine->GeomMaterial.Get()); Component->TranslucencySortPriority = Layer;
					Components.Add(Key, Component);
				}
				return Component;
			};
			for (auto& Pair : Work.Geometry)
			{
				auto* Component = FindComponent(Pair.Key); const bool bNew = !Component->IsRegistered();
				FLinearColor Color = Layers[Pair.Key].DebugColor; Color.A = FMath::Clamp(Color.A, 0.12f, 0.5f);
				Component->SetGeometry(MoveTemp(Pair.Value.Vertices), MoveTemp(Pair.Value.Indices), Color);
				if (bNew) { Component->RegisterComponent(); }
			}
			for (const auto& Pair : Work.SharedGeometry)
			{
				auto* Component = FindComponent(Pair.Key); const bool bNew = !Component->IsRegistered();
				FLinearColor Color = Pair.Value.bUseLayerColor ? Layers[Pair.Key].DebugColor : Pair.Value.Color;
				if (Pair.Value.bUseLayerColor) { Color.A = FMath::Clamp(Color.A, 0.12f, 0.5f); }
				Component->SetSharedGeometry(Pair.Value.Geometry, Color);
				if (bNew) { Component->RegisterComponent(); }
			}
			++Work.TileIndex; Work.Cell = 0; Work.Geometry.Reset(); Work.SharedGeometry.Reset();
		}
		bInitialized = true; PendingUpdate.Reset();
		return true;
	}

	bool FMeshLayerEditPreview::Update(ULevel& Level, const FTransform& Anchor,
		const FResolvedSurfaceVisualization& Visualization, TConstArrayView<FComposableCameraMeshLayerDefinition> Layers,
		const FBox2D* DirtyBounds)
	{
		if (!BeginUpdate(Level, Visualization, DirtyBounds)) { return false; }
		AdvanceUpdate(Level, Anchor, Visualization, Layers);
		return IsReadyFor(&Level);
	}

}
