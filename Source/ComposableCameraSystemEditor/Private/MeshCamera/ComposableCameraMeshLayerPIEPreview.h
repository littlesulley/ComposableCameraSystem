// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "MeshCamera/ComposableCameraMeshLayerRendering.h"

class AActor;
class AComposableCameraMeshSurfaceStorageActor;
class UWorld;
struct FCollisionQueryParams;
struct FHitResult;

namespace UE::ComposableCamera::MeshEditor
{
	constexpr int32 PIEPreviewChunkTriangleCount = 1024;
	constexpr int32 PIEPreviewFirstChunkTriangleCount = 64;
	constexpr int32 PIEPreviewProjectionQuerySlice = 64;
	constexpr int32 PIEPreviewProjectionQueryLimit = 2048;
	constexpr double PIEPreviewProjectionSeconds = 0.004;
	constexpr int32 PreviewPublicationChunkLimit = 16;
	constexpr double PreviewPublicationSeconds = 0.002;

	/** Time remains the primary limit; the work cap bounds exceptionally cheap loops. */
	struct FPreviewPublicationBudget
	{
		explicit FPreviewPublicationBudget(double Started)
			: Deadline(Started + PreviewPublicationSeconds) {}
		bool CanAdvance(double Now) const { return Remaining > 0 && Now < Deadline; }
		void Consume() { --Remaining; }
	private:
		int32 Remaining = PreviewPublicationChunkLimit;
		double Deadline;
	};

	enum class EPIEPreviewWorkResult : uint8
	{
		Skipped,
		Advanced,
		BudgetExhausted
	};

	/** Resume at the unserved job; RunJob must consume a finite budget and report exhaustion. Allocates nothing. */
	void RunPIEPreviewWorkRoundRobin(int32 NumJobs, int32& NextJobIndex,
		TFunctionRef<EPIEPreviewWorkResult(int32)> RunJob);

	/** Disposable, budgeted floor fitting. Contains no UObject references. */
	class FPIEPreviewSurfaceProjection
	{
	public:
		void Begin(TArray<FResolvedSurfaceLayerMesh>&& InMeshes,
			TMap<FVector, FVector>&& InProjectedVertices = {}, bool bAlreadyPublished = false);
		void Reset();
		bool IsComplete() const { return MeshIndex >= Meshes.Num(); }
		bool Advance(TFunctionRef<FVector(const FVector&)> Project, int32 MaxQueries,
			double TimeBudgetSeconds = 0.0, int32* OutQueries = nullptr);
		TConstArrayView<FResolvedSurfaceLayerMesh> GetMeshes() const { return Meshes; }
		/** Consume completed triangle chunks without waiting for the whole document. */
		TArray<FResolvedSurfaceLayerMesh> TakeReadyMeshes(int32 MaxChunks);
		TArray<FResolvedSurfaceLayerMesh> TakeMeshes();
		bool IsPublicationComplete() const { return ReadyMeshIndex >= Meshes.Num(); }

	private:
		TArray<FResolvedSurfaceLayerMesh> Meshes;
		TMap<FVector, FVector> ProjectedVertices;
		int32 MeshIndex = 0;
		int32 VertexIndex = 0;
		int32 ReadyMeshIndex = 0;
		int32 ReadyIndex = 0;
		bool bPublishedAny = false;
	};

	/** Construction diagnostics; no UObject references or retained hit arrays. */
	struct FPIEPreviewProjectionStats
	{
		int32 Queries = 0;
		int32 Misses = 0;
		int32 NonStaticFloorHits = 0;
		double MinFloorAdjustment = 0.0;
		double MaxFloorAdjustment = 0.0;
	};

	/** Fit only local Z to nearby floor collision, clearing opaque StaticMeshes in the same height band. Excludes Pawns. */
	FVector ProjectPIEPreviewVertex(const UWorld& World, const FTransform& LocalToWorld,
		const FVector& LocalVertex, const FCollisionQueryParams& QueryParams, TArray<FHitResult>& ReusedHits,
		FPIEPreviewProjectionStats* Stats = nullptr);

	struct FPIEPreviewPointSample
	{
		int32 Intersections = 0;
		FVector NearestPosition = FVector::ZeroVector;
	};

	/** Explicit diagnostic only: intersect submitted geometry along world Up near a point. */
	FPIEPreviewPointSample SamplePIEPreviewActor(const AActor& PreviewActor, const FVector& WorldPoint,
		double Distance = 500.0);

	/** PIE Level owns the transient actor/components; callers retain weak references only. */
	AActor* CreatePIEPreviewActor(const AComposableCameraMeshSurfaceStorageActor& Storage,
		TConstArrayView<FResolvedSurfaceLayerMesh> Meshes);

	/** Append persistent construction chunks, creating the Level-owned actor on first use. */
	AActor* AppendPIEPreviewMeshes(const AComposableCameraMeshSurfaceStorageActor& Storage,
		AActor* PreviewActor, TConstArrayView<FResolvedSurfaceLayerMesh> Meshes);

	/** Native topology prepared without a World, component or material. */
	struct FNativePreviewLayerMesh
	{
		int32 LayerIndex = INDEX_NONE;
		FColor Color = FColor::White;
		UE::Geometry::FDynamicMesh3 Mesh;
		int32 RejectedTriangles = 0;
		int32 ExpectedTriangles = 0;
	};

	/** Optional reverse faces use disconnected vertices; the chunk limit includes both faces (minimum two). */
	void BuildNativePreviewMeshes(TConstArrayView<FResolvedSurfaceLayerMesh> Meshes,
		TArray<FNativePreviewLayerMesh>& OutMeshes, int32 MaxTrianglesPerMesh = 0,
		const std::atomic_bool* Cancelled = nullptr, bool bAddReverseFaces = false);

	/** Main-thread publication only. Consumes prepared topology; editor overlays cannot be selected. */
	AActor* AppendNativePreviewMeshes(const AComposableCameraMeshSurfaceStorageActor& Storage,
		AActor* PreviewActor, TArrayView<FNativePreviewLayerMesh> Meshes, bool bEditorPreview = false);

	/** Transform-only update: never rebuilds the mesh or its render buffers. */
	bool UpdatePIEPreviewTransform(AActor& PreviewActor, const FTransform& Transform);

	/** Called before PIE scene teardown, on Show off, and when storage disappears. */
	void DestroyPIEPreviewActor(AActor* PreviewActor);
}
