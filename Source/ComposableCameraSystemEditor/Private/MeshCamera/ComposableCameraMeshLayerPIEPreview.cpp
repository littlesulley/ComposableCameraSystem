// Copyright 2026 Sulley. All Rights Reserved.

#include "MeshCamera/ComposableCameraMeshLayerPIEPreview.h"

#include "Components/DynamicMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ComposableCameraSystemEditorModule.h"
#include "CollisionQueryParams.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/Engine.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "HAL/PlatformTime.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MeshCamera/ComposableCameraMeshLayerRendering.h"
#include "MeshCamera/ComposableCameraMeshLayerPreviewViewExtension.h"
#include "MeshCamera/ComposableCameraMeshSurfaceStorageActor.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace UE::ComposableCamera::MeshEditor
{
	void RunPIEPreviewWorkRoundRobin(int32 NumJobs, int32& NextJobIndex,
		TFunctionRef<EPIEPreviewWorkResult(int32)> RunJob)
	{
		if (NumJobs <= 0) { NextJobIndex = 0; return; }
		NextJobIndex = FMath::Max(0, NextJobIndex) % NumJobs;
		int32 SkippedJobs = 0;
		while (SkippedJobs < NumJobs)
		{
			switch (RunJob(NextJobIndex))
			{
			case EPIEPreviewWorkResult::BudgetExhausted: return;
			case EPIEPreviewWorkResult::Skipped: ++SkippedJobs; break;
			case EPIEPreviewWorkResult::Advanced: SkippedJobs = 0; break;
			}
			NextJobIndex = (NextJobIndex + 1) % NumJobs;
		}
	}

	void FPIEPreviewSurfaceProjection::Begin(TArray<FResolvedSurfaceLayerMesh>&& InMeshes,
		TMap<FVector, FVector>&& InProjectedVertices, bool bAlreadyPublished)
	{
		Reset();
		Meshes = MoveTemp(InMeshes);
		ProjectedVertices = MoveTemp(InProjectedVertices);
		bPublishedAny = bAlreadyPublished;
		int32 VertexCount = 0;
		for (const auto& Mesh : Meshes) { VertexCount += Mesh.LocalVertices.Num(); }
		// Live preview supplies capacity prepared on the worker; synchronous fixtures retain this fallback.
		if (ProjectedVertices.GetAllocatedSize() == 0) { ProjectedVertices.Reserve(VertexCount); }
	}

	void FPIEPreviewSurfaceProjection::Reset()
	{
		Meshes.Empty();
		ProjectedVertices.Empty();
		MeshIndex = VertexIndex = 0;
		ReadyMeshIndex = ReadyIndex = 0;
		bPublishedAny = false;
	}

	bool FPIEPreviewSurfaceProjection::Advance(TFunctionRef<FVector(const FVector&)> Project,
		int32 MaxQueries, double TimeBudgetSeconds, int32* OutQueries)
	{
		if (OutQueries) { *OutQueries = 0; }
		if (IsComplete()) { return true; }
		if (MaxQueries <= 0) { return false; }
		const double Started = FPlatformTime::Seconds();
		int32 Queries = 0;
		while (!IsComplete())
		{
			if (TimeBudgetSeconds > 0.0 && FPlatformTime::Seconds() - Started >= TimeBudgetSeconds) { return false; }
			auto& Vertices = Meshes[MeshIndex].LocalVertices;
			if (VertexIndex >= Vertices.Num()) { ++MeshIndex; VertexIndex = 0; continue; }
			const FVector Source = Vertices[VertexIndex];
			if (const FVector* Cached = ProjectedVertices.Find(Source))
			{
				Vertices[VertexIndex] = *Cached;
			}
			else
			{
				if (Queries >= MaxQueries) { return false; }
				FVector Projected = Project(Source);
				++Queries;
				if (OutQueries) { *OutQueries = Queries; }
				if (Projected.ContainsNaN()) { Projected = Source; }
				// Preserve exact shared XY boundaries and Layer color partition.
				Projected.X = Source.X; Projected.Y = Source.Y;
				ProjectedVertices.Add(Source, Projected);
				Vertices[VertexIndex] = Projected;
			}
			++VertexIndex;
		}
		return true;
	}

	TArray<FResolvedSurfaceLayerMesh> FPIEPreviewSurfaceProjection::TakeReadyMeshes(int32 MaxChunks)
	{
		TArray<FResolvedSurfaceLayerMesh> ReadyMeshes;
		while (MaxChunks > 0 && ReadyMeshIndex < Meshes.Num() && ReadyMeshIndex <= MeshIndex)
		{
			const FResolvedSurfaceLayerMesh& Source = Meshes[ReadyMeshIndex];
			const int32 ReadyVertices = ReadyMeshIndex < MeshIndex ? Source.LocalVertices.Num() : VertexIndex;
			const bool bMeshComplete = ReadyVertices >= Source.LocalVertices.Num();
			if (ReadyIndex >= Source.Indices.Num())
			{
				if (!bMeshComplete) { break; }
				++ReadyMeshIndex; ReadyIndex = 0; continue;
			}
			int32 EndIndex = ReadyIndex;
			// A small first chunk gives visible progress without waiting for a large upload batch.
			const int32 MaxIndices = (bPublishedAny ? PIEPreviewChunkTriangleCount : PIEPreviewFirstChunkTriangleCount) * 3;
			while (EndIndex + 2 < Source.Indices.Num() && EndIndex - ReadyIndex < MaxIndices)
			{
				if (Source.Indices[EndIndex] >= ReadyVertices || Source.Indices[EndIndex + 1] >= ReadyVertices
					|| Source.Indices[EndIndex + 2] >= ReadyVertices) { break; }
				EndIndex += 3;
			}
			// Keep construction draw calls bounded. Flush a short tail only when its mesh is fitted.
			if (EndIndex == ReadyIndex || (!bMeshComplete && EndIndex - ReadyIndex < MaxIndices)) { break; }
			FResolvedSurfaceLayerMesh& Chunk = ReadyMeshes.AddDefaulted_GetRef();
			Chunk.LayerIndex = Source.LayerIndex;
			Chunk.Color = Source.Color;
			Chunk.LocalVertices.Reserve(EndIndex - ReadyIndex);
			Chunk.Indices.Reserve(EndIndex - ReadyIndex);
			TMap<int32, int32> Remap;
			Remap.Reserve(EndIndex - ReadyIndex);
			for (int32 Index = ReadyIndex; Index < EndIndex; ++Index)
			{
				const int32 SourceVertex = Source.Indices[Index];
				if (const int32* ExistingVertexIndex = Remap.Find(SourceVertex)) { Chunk.Indices.Add(*ExistingVertexIndex); }
				else
				{
					const int32 NewVertexIndex = Chunk.LocalVertices.Add(Source.LocalVertices[SourceVertex]);
					Remap.Add(SourceVertex, NewVertexIndex);
					Chunk.Indices.Add(NewVertexIndex);
				}
			}
			ReadyIndex = EndIndex;
			bPublishedAny = true;
			--MaxChunks;
		}
		return ReadyMeshes;
	}

	TArray<FResolvedSurfaceLayerMesh> FPIEPreviewSurfaceProjection::TakeMeshes()
	{
		check(IsComplete());
		auto Result = MoveTemp(Meshes);
		Reset();
		return Result;
	}

	FVector ProjectPIEPreviewVertex(const UWorld& World, const FTransform& LocalToWorld,
		const FVector& LocalVertex, const FCollisionQueryParams& QueryParams, TArray<FHitResult>& ReusedHits,
		FPIEPreviewProjectionStats* Stats)
	{
		constexpr double ProjectionDistance = 100.0;
		// The reported flat slab is 7.070 cm above authored Landscape. Keep a bounded
		// clearance above the fixed nearest floor rather than chaining lifts across storeys.
		constexpr double OccludingSurfaceTolerance = 10.0; // World cm.
		const FVector Up = LocalToWorld.TransformVectorNoScale(FVector::UpVector).GetSafeNormal();
		const FVector Floor = LocalToWorld.TransformPosition(LocalVertex - FVector::UpVector * VisualizationSurfaceOffset);
		ReusedHits.Reset();
		World.LineTraceMultiByObjectType(ReusedHits, Floor + Up * ProjectionDistance, Floor - Up * ProjectionDistance,
			FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects), QueryParams);
		if (Stats) { ++Stats->Queries; }
		auto IsRenderedStaticMesh = [](const UPrimitiveComponent* Component)
		{
			const UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Component);
			if (!Mesh || !Mesh->ShouldRender() || !Mesh->IsVisible()
				|| (Mesh->GetOwner() && Mesh->GetOwner()->IsHidden())) { return false; }
			for (int32 Index = 0; Index < Mesh->GetNumMaterials(); ++Index)
			{
				const UMaterialInterface* Material = Mesh->GetMaterial(Index);
				// A missing material uses the engine's opaque default material.
				if (!Material || Material->GetBlendMode() == BLEND_Opaque || Material->GetBlendMode() == BLEND_Masked) { return true; }
			}
			return false;
		};
		auto IsFloorHit = [&](const FHitResult& Hit)
		{
			const UPrimitiveComponent* Component = Hit.GetComponent();
			if (!Component || Hit.bStartPenetrating || FVector::DotProduct(Hit.ImpactNormal, Up) <= UE_DOUBLE_KINDA_SMALL_NUMBER
				|| (Hit.GetActor() && Hit.GetActor()->IsA<APawn>())) { return false; }
			// Visibility controls authoring picks, not whether a rendered floor occludes PIE.
			// Keep non-rendered Ignore/Overlap volumes out of the candidate set.
			return Component->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block || IsRenderedStaticMesh(Component);
		};
		const FHitResult* Closest = nullptr;
		double ClosestDistance = UE_DOUBLE_BIG_NUMBER;
		for (const FHitResult& Hit : ReusedHits)
		{
			if (!IsFloorHit(Hit)) { continue; }
			const double Distance = FMath::Abs(FVector::DotProduct(Hit.ImpactPoint - Floor, Up));
			if (Distance < ClosestDistance) { Closest = &Hit; ClosestDistance = Distance; }
		}
		FVector Result = LocalVertex;
		if (Closest)
		{
			// A visible slab can sit just above the painted Landscape while ignoring Visibility.
			// Clear that slab within one fixed height band, without climbing to another storey.
			const FVector NearestFloor = Closest->ImpactPoint;
			double HighestLift = 0.0;
			for (const FHitResult& Hit : ReusedHits)
			{
				const double Lift = FVector::DotProduct(Hit.ImpactPoint - NearestFloor, Up);
				if (Lift > HighestLift && Lift <= OccludingSurfaceTolerance && IsFloorHit(Hit)
					&& IsRenderedStaticMesh(Hit.GetComponent()))
				{
					Closest = &Hit;
					HighestLift = Lift;
				}
			}
			Result.Z = LocalToWorld.InverseTransformPosition(Closest->ImpactPoint).Z + VisualizationSurfaceOffset;
			if (Stats)
			{
				if (Closest->GetComponent()->GetCollisionObjectType() != ECC_WorldStatic) { ++Stats->NonStaticFloorHits; }
				const double Adjustment = FVector::DotProduct(Closest->ImpactPoint - Floor, Up);
				Stats->MinFloorAdjustment = FMath::Min(Stats->MinFloorAdjustment, Adjustment);
				Stats->MaxFloorAdjustment = FMath::Max(Stats->MaxFloorAdjustment, Adjustment);
			}
		}
		else if (Stats) { ++Stats->Misses; }
		return Result;
	}

	FPIEPreviewPointSample SamplePIEPreviewActor(const AActor& PreviewActor, const FVector& WorldPoint, double Distance)
	{
		FPIEPreviewPointSample Sample;
		Sample.NearestPosition = WorldPoint;
		double ClosestDistance = UE_DOUBLE_BIG_NUMBER;
		TArray<UDynamicMeshComponent*> Components;
		PreviewActor.GetComponents(Components);
		for (const UDynamicMeshComponent* Component : Components)
		{
			const FTransform Transform = Component->GetComponentTransform();
			const FVector Start = Transform.InverseTransformPosition(WorldPoint + FVector::UpVector * Distance);
			const FVector End = Transform.InverseTransformPosition(WorldPoint - FVector::UpVector * Distance);
			Component->ProcessMesh([&](const UE::Geometry::FDynamicMesh3& Mesh)
			{
				for (const int32 TriangleId : Mesh.TriangleIndicesItr())
				{
					FVector A, B, C, Point, Normal;
					Mesh.GetTriVertices(TriangleId, A, B, C);
					if (!FMath::SegmentTriangleIntersection(Start, End, A, B, C, Point, Normal)) { continue; }
					++Sample.Intersections;
					const FVector WorldSurface = Transform.TransformPosition(Point);
					const double SurfaceDistance = FMath::Abs(WorldSurface.Z - WorldPoint.Z);
					if (SurfaceDistance < ClosestDistance)
					{
						ClosestDistance = SurfaceDistance;
						Sample.NearestPosition = WorldSurface;
					}
				}
			});
		}
		return Sample;
	}

	AActor* CreatePIEPreviewActor(const AComposableCameraMeshSurfaceStorageActor& Storage,
		TConstArrayView<FResolvedSurfaceLayerMesh> Meshes)
	{
		return AppendPIEPreviewMeshes(Storage, nullptr, Meshes);
	}

	AActor* AppendPIEPreviewMeshes(const AComposableCameraMeshSurfaceStorageActor& Storage,
		AActor* PreviewActor, TConstArrayView<FResolvedSurfaceLayerMesh> Meshes)
	{
		const UWorld* World = Storage.GetWorld();
		if (!World || World->WorldType != EWorldType::PIE || World->IsBeingCleanedUp()
			|| World->GetNetMode() == NM_DedicatedServer || Meshes.IsEmpty()) { return nullptr; }
		TArray<FNativePreviewLayerMesh> NativeMeshes;
		BuildNativePreviewMeshes(Meshes, NativeMeshes);
		return AppendNativePreviewMeshes(Storage, PreviewActor, NativeMeshes);
	}

	void BuildNativePreviewMeshes(TConstArrayView<FResolvedSurfaceLayerMesh> Meshes,
		TArray<FNativePreviewLayerMesh>& OutMeshes, int32 MaxTrianglesPerMesh, const std::atomic_bool* Cancelled,
		bool bAddReverseFaces)
	{
		OutMeshes.Reset();
		const int32 FaceCopies = bAddReverseFaces ? 2 : 1;
		for (const auto& Source : Meshes)
		{
			const int32 TriangleCount = Source.Indices.Num() / 3;
			const int32 ChunkSize = MaxTrianglesPerMesh > 0
				? FMath::Max(1, MaxTrianglesPerMesh / FaceCopies) : FMath::Max(1, TriangleCount);
			for (int32 FirstTriangle = 0; FirstTriangle < TriangleCount; FirstTriangle += ChunkSize)
			{
				if (Cancelled && Cancelled->load(std::memory_order_relaxed)) { OutMeshes.Reset(); return; }
				auto& Target = OutMeshes.AddDefaulted_GetRef();
				Target.LayerIndex = Source.LayerIndex;
				Target.Color = Source.Color;
				const int32 FrontTriangleCount = FMath::Min(ChunkSize, TriangleCount - FirstTriangle);
				Target.ExpectedTriangles = FrontTriangleCount * FaceCopies;
				TMap<int32, int32> Remap;
				if (MaxTrianglesPerMesh <= 0)
				{
					for (const FVector& Vertex : Source.LocalVertices) { Target.Mesh.AppendVertex(Vertex); }
				}
				else { Remap.Reserve(FrontTriangleCount * 3); }
				for (int32 Triangle = FirstTriangle; Triangle < FirstTriangle + FrontTriangleCount; ++Triangle)
				{
					int32 Vertices[3];
					for (int32 Corner = 0; Corner < 3; ++Corner)
					{
						const int32 SourceIndex = Source.Indices[Triangle * 3 + Corner];
						if (MaxTrianglesPerMesh <= 0) { Vertices[Corner] = SourceIndex; }
						else if (const int32* ExistingIndex = Remap.Find(SourceIndex)) { Vertices[Corner] = *ExistingIndex; }
						else
						{
							const int32 NewIndex = Target.Mesh.AppendVertex(Source.LocalVertices[SourceIndex]);
							Remap.Add(SourceIndex, NewIndex);
							Vertices[Corner] = NewIndex;
						}
					}
					if (Target.Mesh.AppendTriangle(Vertices[0], Vertices[1], Vertices[2]) < 0) { ++Target.RejectedTriangles; }
				}
				if (bAddReverseFaces)
				{
					// PDI disabled backface culling. A persistent component with one-sided
					// GeomMaterial needs both windings, without altering positions or heights.
					const int32 FrontVertexCount = Target.Mesh.VertexCount();
					const int32 FrontTriangleEnd = Target.Mesh.MaxTriangleID();
					for (int32 Vertex = 0; Vertex < FrontVertexCount; ++Vertex)
					{
						Target.Mesh.AppendVertex(Target.Mesh.GetVertex(Vertex));
					}
					Target.RejectedTriangles *= FaceCopies;
					for (int32 Triangle = 0; Triangle < FrontTriangleEnd; ++Triangle)
					{
						if (!Target.Mesh.IsTriangle(Triangle)) { continue; }
						const UE::Geometry::FIndex3i Front = Target.Mesh.GetTriangle(Triangle);
						if (Target.Mesh.AppendTriangle(Front.A + FrontVertexCount,
							Front.C + FrontVertexCount, Front.B + FrontVertexCount) < 0) { ++Target.RejectedTriangles; }
					}
				}
			}
		}
	}

	AActor* AppendNativePreviewMeshes(const AComposableCameraMeshSurfaceStorageActor& Storage,
		AActor* PreviewActor, TArrayView<FNativePreviewLayerMesh> Meshes, bool bEditorPreview)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CCS_MeshLayers_PIEPreviewCreate);
		UWorld* World = Storage.GetWorld();
		const EWorldType::Type RequiredWorld = bEditorPreview ? EWorldType::Editor : EWorldType::PIE;
		if (!World || World->WorldType != RequiredWorld || World->IsBeingCleanedUp()
			|| World->GetNetMode() == NM_DedicatedServer || Meshes.IsEmpty()
			|| !GEngine) return nullptr;

		// Depth-tested and two-sided: characters must occlude floor overlays.
		UMaterial* PreviewMaterial = bEditorPreview ? GEngine->GeomMaterial.Get() : GEngine->DebugMeshMaterial.Get();
		if (!PreviewMaterial) return nullptr;

		AActor* Actor = PreviewActor;
		if (!Actor)
		{
			FActorSpawnParameters Parameters;
			Parameters.OverrideLevel = Storage.GetLevel();
			Parameters.ObjectFlags = RF_Transient;
			if (bEditorPreview) { Parameters.ObjectFlags |= RF_DuplicateTransient; }
			Parameters.bHideFromSceneOutliner = true;
			Parameters.bTemporaryEditorActor = bEditorPreview;
			Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Actor = World->SpawnActor<AActor>(Parameters);
			if (!Actor) return nullptr;
			// Game View also hides editor-only/Hidden In Game actors. Exclude the editor
			// preview from PIE duplication instead of hiding it in the editor's G view.
			Actor->bIsEditorOnlyActor = false;
			Actor->bIgnoreInPIE = bEditorPreview;
			Actor->SetActorHiddenInGame(false);
			Actor->SetActorEnableCollision(false);

			USceneComponent* Root = NewObject<USceneComponent>(Actor, NAME_None, RF_Transient);
			Root->SetMobility(EComponentMobility::Movable);
			Actor->AddInstanceComponent(Root);
			Actor->SetRootComponent(Root);
			Actor->SetActorTransform(Storage.GetActorTransform());
			Root->RegisterComponent();
		}
		else if (!IsValid(Actor) || Actor->IsActorBeingDestroyed() || Actor->GetWorld() != World
			|| Actor->GetLevel() != Storage.GetLevel() || !Actor->GetRootComponent()) { return nullptr; }
		USceneComponent* Root = Actor->GetRootComponent();

		for (FNativePreviewLayerMesh& Source : Meshes)
		{
			if (Source.RejectedTriangles > 0)
			{
				UE_LOG(LogComposableCameraSystemEditor, Warning,
					TEXT("PIE Mesh Layers: %s Layer %d rejected %d of %d preview triangles."),
					*Storage.GetPathName(), Source.LayerIndex, Source.RejectedTriangles, Source.ExpectedTriangles);
			}

			UDynamicMeshComponent* Component = NewObject<UDynamicMeshComponent>(Actor, NAME_None, RF_Transient);
			Actor->AddInstanceComponent(Component);
			Component->SetupAttachment(Root);
			Component->SetMobility(EComponentMobility::Movable);
			Component->PrimaryComponentTick.bCanEverTick = false;
			Component->bSelectable = false;
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Component->SetGenerateOverlapEvents(false);
			Component->SetCanEverAffectNavigation(false);
			Component->SetDeferredCollisionUpdatesEnabled(true, false);
			Component->SetCastShadow(false);
			Component->SetEnableRaytracing(false);
			Component->SetMesh(MoveTemp(Source.Mesh));
			UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(PreviewMaterial, Component);
			Material->SetVectorParameterValue(NAME_Color, FLinearColor(Source.Color));
			Component->SetMaterial(0, Material);
			Component->RegisterComponent();
		}
		TrackPreviewViewActor(*Actor);
		return Actor;
	}

	bool UpdatePIEPreviewTransform(AActor& PreviewActor, const FTransform& Transform)
	{
		if (PreviewActor.GetActorTransform().Equals(Transform)) return false;
		return PreviewActor.SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
	}

	void DestroyPIEPreviewActor(AActor* PreviewActor)
	{
		if (IsValid(PreviewActor))
		{
			UntrackPreviewViewActor(*PreviewActor);
			// World cleanup already owns component unregistration. Never keep the
			// actor alive through a global strong pointer after its Scene is gone.
			if (UWorld* World = PreviewActor->GetWorld(); World && !World->IsBeingCleanedUp())
			{
				PreviewActor->Destroy();
			}
		}
	}
}
