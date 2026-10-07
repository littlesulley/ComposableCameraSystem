// Copyright 2026 Sulley. All Rights Reserved.

#include "MeshCamera/ComposableCameraMeshLayerPreviewBuild.h"
#include "MeshCamera/ComposableCameraMeshLayerSavedPreview.h"

#include "Async/Async.h"
#include "CoreGlobals.h"
#include "Containers/Queue.h"
#include "ComposableCameraSystemEditorModule.h"
#include "Engine/Engine.h"
#include "Materials/Material.h"
#include "Misc/QueuedThreadPool.h"
#include "MeshCamera/ComposableCameraMeshSurfaceTypes.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace UE::ComposableCamera::MeshEditor
{
	struct FPreviewGeometryBuild::FState
	{
		std::atomic_bool Cancelled{false};
		TQueue<FPreviewGeometryBuildResult, EQueueMode::Spsc> Ready;
		TFuture<FPreviewGeometryBuildResult> Future;
	};

	namespace
	{
		// Main-thread registry keeps cancelled workers alive until their plugin code has returned.
		TArray<TSharedPtr<FPreviewGeometryBuild::FState, ESPMode::ThreadSafe>> PreviewBuilds;
		TUniquePtr<FQueuedThreadPool> PreviewBuildPool;

		struct FResolvedGeometryCache
		{
			FComposableCameraMeshSurfaceRuntimeData Source;
			TArray<FComposableCameraMeshLayerDefinition> Layers;
			TArray<FResolvedSurfaceLayerMesh> Meshes;
			TArray<FVector2D> Centers;
			bool bTiled = false;
			SIZE_T Bytes = 0;
			bool Matches(const FComposableCameraMeshSurfaceRuntimeData& Other,
				TConstArrayView<FComposableCameraMeshLayerDefinition> OtherLayers, bool bOtherTiled) const
			{
				if (bTiled != bOtherTiled || Layers.Num() != OtherLayers.Num() || Source.Vertices != Other.Vertices
					|| Source.Indices != Other.Indices || Source.TriangleLayerIndices != Other.TriangleLayerIndices) { return false; }
				for (int32 Index = 0; Index < Layers.Num(); ++Index)
				{
					if (Layers[Index].LayerId != OtherLayers[Index].LayerId || Layers[Index].bEnabled != OtherLayers[Index].bEnabled
						|| Layers[Index].DebugColor != OtherLayers[Index].DebugColor) { return false; }
				}
				return true;
			}
		};
		// Only the single owned worker accesses these. Pure geometry, no World/Profile
		// lifetime; exact snapshots avoid stale results after undo or same-count edits.
		TArray<FResolvedGeometryCache> ResolvedGeometryCache;
		constexpr SIZE_T GeometryCacheByteLimit = 64u * 1024u * 1024u;
		constexpr int32 GeometryCacheEntryLimit = 4;

		void RememberGeometry(FComposableCameraMeshSurfaceRuntimeData&& Source,
			TArray<FComposableCameraMeshLayerDefinition>&& Layers, const TArray<FResolvedSurfaceLayerMesh>& Meshes, bool bTiled = false)
		{
			SIZE_T Bytes = Source.Vertices.GetAllocatedSize() + Source.Indices.GetAllocatedSize()
				+ Source.TriangleLayerIndices.GetAllocatedSize() + Layers.GetAllocatedSize() + Meshes.GetAllocatedSize()
				+ Meshes.Num() * sizeof(FVector2D);
			for (const auto& Mesh : Meshes) { Bytes += Mesh.LocalVertices.GetAllocatedSize() + Mesh.Indices.GetAllocatedSize(); }
			if (Bytes > GeometryCacheByteLimit) { return; }
			SIZE_T Used = 0; for (const auto& Entry : ResolvedGeometryCache) { Used += Entry.Bytes; }
			while (!ResolvedGeometryCache.IsEmpty() && (ResolvedGeometryCache.Num() >= GeometryCacheEntryLimit
				|| Used + Bytes > GeometryCacheByteLimit))
			{
				Used -= ResolvedGeometryCache[0].Bytes;
				ResolvedGeometryCache.RemoveAt(0);
			}
			auto& Entry = ResolvedGeometryCache.AddDefaulted_GetRef();
			Entry.Source = MoveTemp(Source); Entry.Layers = MoveTemp(Layers); Entry.Meshes = Meshes; Entry.Bytes = Bytes;
			Entry.bTiled = bTiled;
			Entry.Centers.Reserve(Meshes.Num());
			for (const auto& Mesh : Meshes)
			{
				FBox2D Bounds(ForceInit);
				for (const FVector& Vertex : Mesh.LocalVertices) { Bounds += FVector2D(Vertex.X, Vertex.Y); }
				Entry.Centers.Add(Bounds.bIsValid ? Bounds.GetCenter() : FVector2D::ZeroVector);
			}
		}
	}

	void InitializePreviewGeometryBuilds()
	{
		check(IsInGameThread());
		if (PreviewBuildPool) { return; }
		PreviewBuildPool.Reset(FQueuedThreadPool::Allocate());
		// Created during module registration, not Show activation. One low-priority worker
		// avoids saturating the editor's shared pool when many documents/worlds appear.
		if (!PreviewBuildPool->Create(1, 0, TPri_BelowNormal, TEXT("CCSMeshPreview")))
		{
			PreviewBuildPool.Reset();
			UE_LOG(LogComposableCameraSystemEditor, Warning, TEXT("Mesh Layers: could not create background preview worker."));
		}
	}

	FPreviewGeometryBuild::~FPreviewGeometryBuild() { Reset(); }
	FPreviewGeometryBuild::FPreviewGeometryBuild(FPreviewGeometryBuild&& Other) noexcept
		: State(MoveTemp(Other.State)) {}
	FPreviewGeometryBuild& FPreviewGeometryBuild::operator=(FPreviewGeometryBuild&& Other) noexcept
	{
		if (this != &Other) { Reset(); State = MoveTemp(Other.State); }
		return *this;
	}

	void FPreviewGeometryBuild::Begin(const FComposableCameraMeshSurfaceRuntimeData& Data,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers, bool bEditorChunks,
		bool bStreamTiles, const FVector& LocalFocus, const FComposableCameraMeshSurfaceEditorPreview* SavedPreview)
	{
		check(IsInGameThread());
		Reset();
		PumpPreviewGeometryBuilds();
		InitializePreviewGeometryBuilds();
		if (!PreviewBuildPool) { return; }
		// Do not copy the query BVH, Profiles or any other UObject reference.
		FComposableCameraMeshSurfaceRuntimeData Snapshot;
		Snapshot.Vertices = Data.Vertices;
		Snapshot.Indices = Data.Indices;
		Snapshot.TriangleLayerIndices = Data.TriangleLayerIndices;
		TArray<FComposableCameraMeshLayerDefinition> LayerSnapshot;
		LayerSnapshot.SetNum(Layers.Num());
		for (int32 Index = 0; Index < Layers.Num(); ++Index)
		{
			LayerSnapshot[Index].LayerId = Layers[Index].LayerId;
			LayerSnapshot[Index].bEnabled = Layers[Index].bEnabled;
			LayerSnapshot[Index].DebugColor = Layers[Index].DebugColor;
		}
		State = MakeShared<FState, ESPMode::ThreadSafe>();
		PreviewBuilds.Add(State);
		// Capture only the material policy; workers never access the material UObject.
		const bool bAddReverseFaces = bEditorChunks && GEngine && GEngine->GeomMaterial
			&& !GEngine->GeomMaterial->IsTwoSided();
		FComposableCameraMeshSurfaceEditorPreview Saved;
		if (SavedPreview && SavedPreview->Version == 1) { Saved = *SavedPreview; }
		State->Future = AsyncPool(*PreviewBuildPool,
			[Job = State, Snapshot = MoveTemp(Snapshot), Layers = MoveTemp(LayerSnapshot), bEditorChunks, bAddReverseFaces,
				bStreamTiles, Focus = FVector2D(LocalFocus.X, LocalFocus.Y), Saved = MoveTemp(Saved)]() mutable
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(CCS_MeshLayers_BackgroundGeometry);
				FPreviewGeometryBuildResult Result;
				Result.bBuiltOffGameThread = !IsInGameThread();
				if (Job->Cancelled.load(std::memory_order_relaxed)) { return Result; }
				FResolvedSurfaceVisualization SavedCoverage;
				if (LoadEditorPreview(Saved, Layers, SavedCoverage))
				{
					// Save already resolved priority/heights. Publish one ready document, without spatial-tile scheduling.
					FPreviewGeometryBuildResult Batch; Batch.bComplete = false; Batch.bBuiltOffGameThread = Result.bBuiltOffGameThread;
					BuildVisualizationMeshes(SavedCoverage, Layers, Batch.LocalMeshes, &Job->Cancelled);
					for (const auto& Mesh : Batch.LocalMeshes) { Result.ExpectedTriangles += Mesh.Indices.Num() / 3; }
					Batch.ExpectedTriangles = Result.ExpectedTriangles; Batch.bReusedGeometry = Result.bReusedGeometry = true;
					if (bEditorChunks)
					{
						BuildNativePreviewMeshes(Batch.LocalMeshes, Batch.NativeMeshes, PIEPreviewChunkTriangleCount, &Job->Cancelled, bAddReverseFaces);
						Batch.LocalMeshes.Empty();
					}
					else
					{
						int32 Vertices = 0; for (const auto& Mesh : Batch.LocalMeshes) { Vertices += Mesh.LocalVertices.Num(); }
						Batch.ProjectionVertexCache.Reserve(Vertices);
					}
					if (!bStreamTiles) { Batch.bComplete = true; return Batch; }
					if (!Job->Cancelled.load(std::memory_order_relaxed)) { Job->Ready.Enqueue(MoveTemp(Batch)); }
					return Result;
				}
				const int32 Cached = ResolvedGeometryCache.IndexOfByPredicate([&](const auto& Entry) { return Entry.Matches(Snapshot, Layers, bStreamTiles); });
				if (bStreamTiles)
				{
					Result.bReusedGeometry = Cached != INDEX_NONE;
					auto PublishMeshes = [&](TArray<FResolvedSurfaceLayerMesh>&& Meshes)
					{
						for (auto& Mesh : Meshes)
						{
							if (Job->Cancelled.load(std::memory_order_relaxed)) { return; }
							FPreviewGeometryBuildResult Batch;
							Batch.bComplete = false; Batch.bBuiltOffGameThread = Result.bBuiltOffGameThread;
							Batch.bReusedGeometry = Result.bReusedGeometry;
							Batch.ExpectedTriangles = Mesh.Indices.Num() / 3;
							Result.ExpectedTriangles += Batch.ExpectedTriangles;
							if (bEditorChunks)
							{
								BuildNativePreviewMeshes(MakeArrayView(&Mesh, 1), Batch.NativeMeshes,
									PIEPreviewChunkTriangleCount, &Job->Cancelled, bAddReverseFaces);
							}
							else
							{
								Batch.ProjectionVertexCache.Reserve(Mesh.LocalVertices.Num());
								Batch.LocalMeshes.Add(MoveTemp(Mesh));
							}
							if (!Job->Cancelled.load(std::memory_order_relaxed)) { Job->Ready.Enqueue(MoveTemp(Batch)); }
						}
					};
					if (Cached != INDEX_NONE)
					{
						// Reorder cached tiles for this view without copying every vertex
						// before the first batch. Cache ownership stays on this one worker.
						const auto& Entry = ResolvedGeometryCache[Cached];
						TArray<int32> Order;
						for (int32 Index = 0; Index < Entry.Meshes.Num(); ++Index) { Order.Add(Index); }
						Order.Sort([&](int32 A, int32 B)
						{
							const double DA = (Entry.Centers[A] - Focus).SizeSquared(), DB = (Entry.Centers[B] - Focus).SizeSquared();
							return DA != DB ? DA < DB : A < B;
						});
						for (int32 Index : Order)
						{
							if (Job->Cancelled.load(std::memory_order_relaxed)) { return FPreviewGeometryBuildResult(); }
							TArray<FResolvedSurfaceLayerMesh> Batch; Batch.Add(Entry.Meshes[Index]);
							PublishMeshes(MoveTemp(Batch));
						}
						if (Cached + 1 < ResolvedGeometryCache.Num())
						{
							auto Used = MoveTemp(ResolvedGeometryCache[Cached]);
							ResolvedGeometryCache.RemoveAt(Cached); ResolvedGeometryCache.Add(MoveTemp(Used));
						}
					}
					else
					{
						TArray<FResolvedSurfaceLayerMesh> CompleteMeshes;
						SIZE_T RetainedBytes = Snapshot.Vertices.GetAllocatedSize() + Snapshot.Indices.GetAllocatedSize()
							+ Snapshot.TriangleLayerIndices.GetAllocatedSize() + Layers.GetAllocatedSize();
						BuildRuntimeVisualizationTiles(Snapshot, Layers, Focus, [&](FResolvedSurfaceVisualization&& Tile)
						{
							TArray<FResolvedSurfaceLayerMesh> Meshes;
							BuildVisualizationMeshes(Tile, Layers, Meshes, &Job->Cancelled);
							for (const auto& Mesh : Meshes)
							{
								RetainedBytes += sizeof(FResolvedSurfaceLayerMesh) + sizeof(FVector2D)
									+ Mesh.LocalVertices.GetAllocatedSize() + Mesh.Indices.GetAllocatedSize();
							}
							if (RetainedBytes <= GeometryCacheByteLimit) { CompleteMeshes.Append(Meshes); }
							else { CompleteMeshes.Empty(); }
							PublishMeshes(MoveTemp(Meshes));
						}, &Job->Cancelled);
						if (!Job->Cancelled.load(std::memory_order_relaxed) && RetainedBytes <= GeometryCacheByteLimit)
						{
							RememberGeometry(MoveTemp(Snapshot), MoveTemp(Layers), CompleteMeshes, true);
						}
					}
					if (Job->Cancelled.load(std::memory_order_relaxed)) { return FPreviewGeometryBuildResult(); }
					return Result;
				}
				if (Cached != INDEX_NONE)
				{
					Result.LocalMeshes = ResolvedGeometryCache[Cached].Meshes;
					Result.bReusedGeometry = true;
					// Move the recently used entry to the tail, keeping eviction independent
					// of editor/PIE request order and without retaining any UObject.
					if (Cached + 1 < ResolvedGeometryCache.Num())
					{
						auto Entry = MoveTemp(ResolvedGeometryCache[Cached]);
						ResolvedGeometryCache.RemoveAt(Cached); ResolvedGeometryCache.Add(MoveTemp(Entry));
					}
				}
				else
				{
					FResolvedSurfaceVisualization Visualization;
					BuildRuntimeVisualization(Snapshot, Layers, Visualization, &Job->Cancelled);
					BuildVisualizationMeshes(Visualization, Layers, Result.LocalMeshes, &Job->Cancelled);
					if (!Job->Cancelled.load(std::memory_order_relaxed)) { RememberGeometry(MoveTemp(Snapshot), MoveTemp(Layers), Result.LocalMeshes); }
				}
				if (Job->Cancelled.load(std::memory_order_relaxed)) { return FPreviewGeometryBuildResult(); }
				int32 VertexCount = 0;
				for (const auto& Mesh : Result.LocalMeshes)
				{
					Result.ExpectedTriangles += Mesh.Indices.Num() / 3;
					VertexCount += Mesh.LocalVertices.Num();
				}
				if (bEditorChunks)
				{
					BuildNativePreviewMeshes(Result.LocalMeshes, Result.NativeMeshes,
						PIEPreviewChunkTriangleCount, &Job->Cancelled, bAddReverseFaces);
					Result.LocalMeshes.Empty();
				}
				else if (!Job->Cancelled.load(std::memory_order_relaxed))
				{
					// Avoid a large hash allocation when the game thread first consumes the result.
					Result.ProjectionVertexCache.Reserve(VertexCount);
				}
				return Result;
			});
	}

	bool FPreviewGeometryBuild::TakeResult(FPreviewGeometryBuildResult& OutResult)
	{
		check(IsInGameThread());
		if (!State) { return false; }
		if (State->Ready.Dequeue(OutResult)) { return true; }
		if (!State->Future.IsReady()) { return false; }
		// The worker may have queued its final batches between the first dequeue
		// and readiness check. Drain them before exposing the terminal result.
		if (State->Ready.Dequeue(OutResult)) { return true; }
		OutResult = State->Future.Consume();
		State.Reset();
		return true;
	}

	void FPreviewGeometryBuild::Reset()
	{
		if (State) { State->Cancelled.store(true, std::memory_order_relaxed); State.Reset(); }
	}

	void PumpPreviewGeometryBuilds()
	{
		check(IsInGameThread());
		PreviewBuilds.RemoveAllSwap([](const auto& Job)
		{
			if (!Job->Future.IsValid()) { return true; }
			if (!Job->Cancelled.load(std::memory_order_relaxed) || !Job->Future.IsReady()) { return false; }
			// Reset detached the only consumer. Reap abandoned native batches on
			// the owned worker, rather than freeing a large queued document in Tick.
			// Pool joining on shutdown also covers these disposal tasks.
			if (PreviewBuildPool)
			{
				AsyncPool(*PreviewBuildPool, [Discard = Job]() mutable
				{
					FPreviewGeometryBuildResult Batch;
					while (Discard->Ready.Dequeue(Batch)) {}
					Discard.Reset();
				});
			}
			return true;
		});
	}

	void ShutdownPreviewGeometryBuilds()
	{
		check(IsInGameThread());
		for (const auto& Job : PreviewBuilds) { Job->Cancelled.store(true, std::memory_order_relaxed); }
		for (const auto& Job : PreviewBuilds) { if (Job->Future.IsValid()) { Job->Future.Wait(); } }
		// A ready future precedes AsyncPool's callable destruction. Join the owned pool
		// too, so no template/lambda cleanup can execute from the unloaded module.
		if (PreviewBuildPool) { PreviewBuildPool->Destroy(); PreviewBuildPool.Reset(); }
		ResolvedGeometryCache.Empty();
		PreviewBuilds.Empty();
	}
}
