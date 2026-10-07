// Copyright 2026 Sulley. All Rights Reserved.

#include "Utilities/ComposableCameraMeshLayerTool.h"
#include "MeshCamera/ComposableCameraMeshLayerEditWork.h"

#include "Utilities/ComposableCameraEditorToolsMenu.h"

#include "ComposableCameraEditorStyle.h"
#include "ComposableCameraSystemEditorModule.h"
#include "CollisionQueryParams.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "CoreGlobals.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "EditorModeRegistry.h"
#include "Engine/Engine.h"
#include "Engine/HitResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "MeshCamera/ComposableCameraMeshLayerEdMode.h"
#include "MeshCamera/ComposableCameraMeshLayerPreviewEdMode.h"
#include "MeshCamera/ComposableCameraMeshLayerPIEPreview.h"
#include "MeshCamera/ComposableCameraMeshLayerPreviewBuild.h"
#include "MeshCamera/ComposableCameraMeshLayerPreviewViewExtension.h"
#include "MeshCamera/ComposableCameraMeshLayerRendering.h"
#include "MeshCamera/ComposableCameraMeshSurfaceStorageActor.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "RenderingThread.h"
#include "SceneManagement.h"
#include "ToolMenus.h"
#include "UDynamicMesh.h"

#define LOCTEXT_NAMESPACE "ComposableCameraMeshLayerTool"

namespace
{
	const FName MeshLayerToolMenuOwner(TEXT("ComposableCameraMeshLayerTool"));
	FDelegateHandle MeshLayerToolStartupCallbackHandle;
	FDelegateHandle MeshLayerPrePIEEndedHandle;
	FDelegateHandle MeshLayerPostPIEStartedHandle;
	bool bMeshLayerPreviewRequested = false;
	bool bMeshLayerEditModeEntered = false;
	bool bMeshLayerPIEIsEnding = false;
	FTSTicker::FDelegateHandle MeshLayerPIEPreviewTickerHandle;

	void UpdateRequestedEditorPreviewMode()
	{
		if (IsEngineExitRequested() || !GEditor || !bMeshLayerPreviewRequested || bMeshLayerEditModeEntered) { return; }
		FEditorModeTools& Modes = GLevelEditorModeTools();
		UWorld* World = Modes.GetWorld();
		if (!World || World->WorldType != EWorldType::Editor || World->IsBeingCleanedUp()
			|| Modes.IsModeActive(FComposableCameraMeshLayerEdMode::ModeId)
			|| Modes.IsModeActive(FComposableCameraMeshLayerPreviewEdMode::ModeId)) { return; }
		// DeactivateMode removes the active flag before Exit runs. The Enter/Exit
		// flag keeps restoration outside deferred Edit cleanup and its save prompt.
		Modes.ActivateMode(FComposableCameraMeshLayerPreviewEdMode::ModeId);
		GEditor->RedrawLevelEditingViewports();
	}

	struct FMeshLayerPIEPreviewCache
	{
		FTransform StorageTransform = FTransform::Identity;
		FTransform ProjectionTransform = FTransform::Identity;
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<AActor> PreviewActor;
		UE::ComposableCamera::MeshEditor::FPIEPreviewSurfaceProjection Projection;
		UE::ComposableCamera::MeshEditor::FPreviewGeometryBuild GeometryBuild;
		FCollisionQueryParams ProjectionQueryParams;
		TArray<FHitResult> ProjectionHits;
		UE::ComposableCamera::MeshEditor::FPIEPreviewProjectionStats ProjectionStats;
		int32 ExpectedTriangles = 0;
		double GeometryStarted = 0.0;
		double GeometryWaitSeconds = 0.0;
		double FirstGeometrySeconds = -1.0;
		bool bGeometryReused = false;
		bool bProjectionPending = false;
		bool bPublicationPending = false;
		bool bInitialized = false;
		uint64 LastSeenTick = 0;
		uint64 DataRevision = 0;
	};

	TMap<
		TWeakObjectPtr<AComposableCameraMeshSurfaceStorageActor>,
		FMeshLayerPIEPreviewCache> MeshLayerPIEPreviewCaches;
	uint64 MeshLayerPIEPreviewTickSerial = 0;
	TArray<TWeakObjectPtr<AComposableCameraMeshSurfaceStorageActor>> MeshLayerPIEPreviewWorkOrder;
	int32 NextMeshLayerPIEProjectionJob = 0;
	int32 NextMeshLayerPIEPublicationJob = 0;

	void DumpPIEPreviewAtPawn(const AComposableCameraMeshSurfaceStorageActor& Storage, const FMeshLayerPIEPreviewCache& Cache)
	{
		using namespace UE::ComposableCamera::MeshEditor;
		UWorld* World = Cache.World.Get();
		const APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
		if (!Pawn)
		{
			UE_LOG(LogComposableCameraSystemEditor, Log, TEXT("PIE Mesh Layers: %s PointSample unavailable: no player Pawn."),
				*Storage.GetPathName());
			return;
		}
		const FVector Foot = Pawn->GetActorLocation() - FVector::UpVector * Pawn->GetSimpleCollisionHalfHeight();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CCSMeshLayerPIEPointDiagnostic), true, Pawn);
		TArray<FHitResult> Hits;
		FPIEPreviewProjectionStats PointStats;
		const FVector Fitted = ProjectPIEPreviewVertex(*World, FTransform::Identity,
			Foot + FVector::UpVector * VisualizationSurfaceOffset, Params, Hits, &PointStats);
		UE_LOG(LogComposableCameraSystemEditor, Log,
			TEXT("PIE Mesh Layers: %s PointSample Pawn=%s Foot=%s FittedZ=%.3f FloorMiss=%d ComplexHits=%d"),
			*Storage.GetPathName(), *Pawn->GetPathName(), *Foot.ToString(), Fitted.Z, PointStats.Misses, Hits.Num());
		for (int32 Index = 0; Index < FMath::Min(Hits.Num(), 12); ++Index)
		{
			const FHitResult& Hit = Hits[Index];
			const UPrimitiveComponent* Component = Hit.GetComponent();
			if (!Component) { continue; }
			UE_LOG(LogComposableCameraSystemEditor, Log,
				TEXT("PIE Mesh Layers: PointHit[%d] Actor=%s Component=%s Z=%.3f NormalZ=%.3f Penetrating=%d ObjectType=%d Collision=%d Visibility=%d Scale=%s"),
				Index, *GetPathNameSafe(Hit.GetActor()), *Component->GetPathName(), Hit.ImpactPoint.Z, Hit.ImpactNormal.Z,
				Hit.bStartPenetrating != 0, static_cast<int32>(Component->GetCollisionObjectType()),
				static_cast<int32>(Component->GetCollisionEnabled()), static_cast<int32>(Component->GetCollisionResponseToChannel(ECC_Visibility)),
				*Component->GetComponentScale().ToString());
			if (const UStaticMeshComponent* StaticMesh = Cast<UStaticMeshComponent>(Component))
			{
				const UMaterialInterface* Material = StaticMesh->GetMaterial(0);
				UE_LOG(LogComposableCameraSystemEditor, Log, TEXT("PIE Mesh Layers: PointStaticMesh=%s Material0=%s Blend0=%d Rendered=%d"),
					*GetPathNameSafe(StaticMesh->GetStaticMesh().Get()), *GetPathNameSafe(Material),
					Material ? static_cast<int32>(Material->GetBlendMode()) : -1, StaticMesh->ShouldRender());
			}
		}
		// Bounds candidates reveal child meshes that collision queries never hit.
		// A candidate is not evidence that its rendered triangles cover the point.
		int32 CandidateCount = 0;
		TArray<UStaticMeshComponent*> StaticMeshes;
		for (TActorIterator<AActor> It(World); It && CandidateCount < 12; ++It)
		{
			if (It->IsA<APawn>()) { continue; }
			StaticMeshes.Reset();
			It->GetComponents(StaticMeshes);
			for (const UStaticMeshComponent* Component : StaticMeshes)
			{
				const FBox Bounds = Component->Bounds.GetBox().ExpandBy(2.0);
				if (!Bounds.IsInsideXY(Foot) || Bounds.Max.Z < Foot.Z - 100.0 || Bounds.Min.Z > Foot.Z + 100.0) { continue; }
				UE_LOG(LogComposableCameraSystemEditor, Log,
					TEXT("PIE Mesh Layers: PointCandidate[%d] Actor=%s Component=%s Collision=%d ObjectType=%d Visibility=%d BoundsMin=%s BoundsMax=%s"),
					CandidateCount, *It->GetPathName(), *Component->GetPathName(), static_cast<int32>(Component->GetCollisionEnabled()),
					static_cast<int32>(Component->GetCollisionObjectType()), static_cast<int32>(Component->GetCollisionResponseToChannel(ECC_Visibility)),
					*Bounds.Min.ToString(), *Bounds.Max.ToString());
				UE_LOG(LogComposableCameraSystemEditor, Log, TEXT("PIE Mesh Layers: PointCandidateMesh=%s Material0=%s Scale=%s"),
					*GetPathNameSafe(Component->GetStaticMesh().Get()), *GetPathNameSafe(Component->GetMaterial(0)),
					*Component->GetComponentScale().ToString());
				if (++CandidateCount >= 12) { break; }
			}
		}
		Params.bTraceComplex = false;
		Hits.Reset();
		FPIEPreviewProjectionStats SimpleStats;
		const FVector SimpleFitted = ProjectPIEPreviewVertex(*World, FTransform::Identity,
			Foot + FVector::UpVector * VisualizationSurfaceOffset, Params, Hits, &SimpleStats);
		UE_LOG(LogComposableCameraSystemEditor, Log,
			TEXT("PIE Mesh Layers: PointSimple FittedZ=%.3f FloorMiss=%d Hits=%d"), SimpleFitted.Z, SimpleStats.Misses, Hits.Num());
		FComposableCameraMeshLayerQueryResults Layers;
		Storage.QueryLayers(Foot + FVector::UpVector * 100.0, 600.0, 1.0, Layers);
		for (const auto& Layer : Layers)
		{
			UE_LOG(LogComposableCameraSystemEditor, Log, TEXT("PIE Mesh Layers: PointLayer Order=%d Name=%s NativeZ=%.3f"),
				Layer.LayerOrder, *Layer.LayerName.ToString(), Layer.SurfacePosition.Z);
		}
		FPIEPreviewPointSample Preview;
		Preview.NearestPosition = Foot;
		if (const AActor* Actor = Cache.PreviewActor.Get()) { Preview = SamplePIEPreviewActor(*Actor, Foot); }
		const bool bDepthComparable = Preview.Intersections > 0 && PointStats.Misses == 0;
		UE_LOG(LogComposableCameraSystemEditor, Log,
			TEXT("PIE Mesh Layers: PointCoverage NativeLayers=%d SubmittedHits=%d PreviewZ=%.3f DepthComparable=%d PreviewMinusComplexFloorCm=%.3f Pending=%d"),
			Layers.Num(), Preview.Intersections, Preview.NearestPosition.Z,
			bDepthComparable, bDepthComparable ? Preview.NearestPosition.Z - (Fitted.Z - VisualizationSurfaceOffset) : 0.0,
			Cache.GeometryBuild.IsPending() || Cache.bProjectionPending || Cache.bPublicationPending);
	}

	void DumpPIEPreviewCaches()
	{
		UE_LOG(LogComposableCameraSystemEditor, Log, TEXT("PIE Mesh Layers: Show=%d Ending=%d Documents=%d"),
			bMeshLayerPreviewRequested, bMeshLayerPIEIsEnding, MeshLayerPIEPreviewCaches.Num());
		for (const auto& Pair : MeshLayerPIEPreviewCaches)
		{
			const auto& Cache = Pair.Value;
			int32 SubmittedTriangles = 0;
			TArray<UDynamicMeshComponent*> Components;
			if (const AActor* Preview = Cache.PreviewActor.Get()) { Preview->GetComponents(Components); }
			for (UDynamicMeshComponent* Component : Components)
			{
				Component->GetDynamicMesh()->ProcessMesh([&SubmittedTriangles](const auto& Mesh)
				{
					SubmittedTriangles += Mesh.TriangleCount();
				});
			}
			const auto& Stats = Cache.ProjectionStats;
			UE_LOG(LogComposableCameraSystemEditor, Log,
				TEXT("PIE Mesh Layers: %s Pending=%d GeometryPending=%d ReusedGeometry=%d FirstGeometrySec=%.3f GeometryWaitSec=%.3f Components=%d Triangles=%d/%d Queries=%d Misses=%d NonStatic=%d FloorDeltaCm=[%.3f,%.3f]"),
				*GetPathNameSafe(Pair.Key.Get()), Cache.GeometryBuild.IsPending() || Cache.bProjectionPending || Cache.bPublicationPending,
				Cache.GeometryBuild.IsPending(), Cache.bGeometryReused, Cache.FirstGeometrySeconds,
				Cache.GeometryBuild.IsPending() ? FPlatformTime::Seconds() - Cache.GeometryStarted : Cache.GeometryWaitSeconds, Components.Num(),
				SubmittedTriangles, Cache.ExpectedTriangles, Stats.Queries, Stats.Misses, Stats.NonStaticFloorHits,
				Stats.MinFloorAdjustment, Stats.MaxFloorAdjustment);
			if (const AComposableCameraMeshSurfaceStorageActor* Storage = Pair.Key.Get()) { DumpPIEPreviewAtPawn(*Storage, Cache); }
		}
	}

	FAutoConsoleCommand MeshLayerPIEPreviewDumpCommand(
		TEXT("CCS.Editor.MeshLayers.DumpPIEPreview"),
		TEXT("Log PIE Mesh Layer submission/fitting statistics and source, submitted mesh and floor heights beneath the player Pawn."),
		FConsoleCommandDelegate::CreateStatic(&DumpPIEPreviewCaches));

	void ReleasePIEPreviewCache(FMeshLayerPIEPreviewCache& Cache)
	{
		UE::ComposableCamera::MeshEditor::DestroyPIEPreviewActor(Cache.PreviewActor.Get());
		Cache.PreviewActor.Reset();
		Cache.World.Reset();
		Cache.GeometryBuild.Reset();
		Cache.Projection.Reset();
		Cache.ProjectionHits.Empty();
		Cache.ProjectionStats = {};
		Cache.ExpectedTriangles = 0;
		Cache.GeometryStarted = Cache.GeometryWaitSeconds = 0.0;
		Cache.FirstGeometrySeconds = -1.0;
		Cache.bGeometryReused = false;
		Cache.bProjectionPending = false;
		Cache.bPublicationPending = false;
		Cache.bInitialized = false;
	}

	void ReleaseAllPIEPreviewCaches()
	{
		for (TPair<
			TWeakObjectPtr<AComposableCameraMeshSurfaceStorageActor>,
			FMeshLayerPIEPreviewCache>& Pair : MeshLayerPIEPreviewCaches)
		{
			ReleasePIEPreviewCache(Pair.Value);
		}
		MeshLayerPIEPreviewCaches.Reset();
		MeshLayerPIEPreviewWorkOrder.Empty();
		NextMeshLayerPIEProjectionJob = NextMeshLayerPIEPublicationJob = 0;
	}

	void HandlePrePIEEnded(bool /*bIsSimulating*/)
	{
		// PrePIEEnded fires before EndPlayMap starts dismantling PIE worlds.
		// Destroy our Level-owned preview actors here and prevent the ticker
		// from repopulating them while UWorld::FinishDestroy releases FScene.
		bMeshLayerPIEIsEnding = true;
		UE::ComposableCamera::MeshEditor::SetPreviewViewPIEEnding(true);
		ReleaseAllPIEPreviewCaches();
	}

	void HandlePostPIEStarted(bool /*bIsSimulating*/)
	{
		bMeshLayerPIEIsEnding = false;
		UE::ComposableCamera::MeshEditor::SetPreviewViewPIEEnding(false);
	}

	void BuildPIEPreviewCache(
		const AComposableCameraMeshSurfaceStorageActor& StorageActor,
		FMeshLayerPIEPreviewCache& OutCache)
	{
		using namespace UE::ComposableCamera::MeshEditor;
		TRACE_CPUPROFILER_EVENT_SCOPE(CCS_MeshLayers_PIEPreviewBuild);
		ReleasePIEPreviewCache(OutCache);

		UWorld* World = StorageActor.GetWorld();
		if (!World || World->IsBeingCleanedUp() || World->GetNetMode() == NM_DedicatedServer)
		{
			return;
		}

		OutCache.GeometryStarted = FPlatformTime::Seconds();
		const APlayerController* Controller = World->GetFirstPlayerController();
		const APlayerCameraManager* Camera = Controller ? Controller->PlayerCameraManager.Get() : nullptr;
		const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
		// The first PIE camera cache can still be at the origin before its first
		// update. Prefer the possessed Pawn then, rather than sorting a distant
		// player location behind tiles around (0,0). This affects preview order only.
		const FVector Focus = Camera && Camera->GetCameraCacheTime() > 0.0f
			? StorageActor.GetActorTransform().InverseTransformPosition(Camera->GetCameraLocation())
			: (Pawn ? StorageActor.GetActorTransform().InverseTransformPosition(Pawn->GetActorLocation()) : FVector::ZeroVector);
		OutCache.GeometryBuild.Begin(StorageActor.GetRuntimeData(), StorageActor.GetLayers(), false, true, Focus, &StorageActor.GetEditorPreview());
		OutCache.DataRevision = StorageActor.GetEditorDataRevision();

		OutCache.World = World;
		OutCache.StorageTransform = StorageActor.GetActorTransform();
		OutCache.ProjectionTransform = OutCache.StorageTransform;
		OutCache.ProjectionQueryParams = FCollisionQueryParams(SCENE_QUERY_STAT(CCSMeshLayerPIEProjection), true, &StorageActor);
		OutCache.ProjectionHits.Reserve(8);
		// Initialized includes background geometry, queued fitting and empty/disabled documents.
		OutCache.bInitialized = true;
	}

	bool TickMeshLayerPreview(float /*DeltaTime*/)
	{
		UpdateRequestedEditorPreviewMode();
#if !UE_BUILD_SHIPPING
		TRACE_CPUPROFILER_EVENT_SCOPE(CCS_MeshLayers_PIEPreviewTick);
		using namespace UE::ComposableCamera::MeshEditor;
		PumpPreviewGeometryBuilds();
		if (!bMeshLayerPreviewRequested || bMeshLayerEditModeEntered || !GEngine || IsEngineExitRequested())
		{
			return true;
		}
		if (auto* Preview = GLevelEditorModeTools().GetActiveModeTyped<FComposableCameraMeshLayerPreviewEdMode>(
			FComposableCameraMeshLayerPreviewEdMode::ModeId))
		{
			Preview->AdvancePreview();
		}
		// PIE teardown must not suspend the independent editor-world loader.
		if (bMeshLayerPIEIsEnding) { return true; }
		++MeshLayerPIEPreviewTickSerial;
		// Only newly discovered geometry does floor fitting. A shared budget
		// prevents multiple documents/PIE worlds multiplying startup trace work.
		int32 RemainingProjectionQueries = PIEPreviewProjectionQueryLimit;
		double RemainingProjectionSeconds = PIEPreviewProjectionSeconds;

		for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
		{
			if (!ShouldDrawPIEPreview(
				bMeshLayerPreviewRequested,
				bMeshLayerPIEIsEnding,
				WorldContext.WorldType))
			{
				continue;
			}

			UWorld* World = WorldContext.World();
			if (!World || World->IsBeingCleanedUp()
				|| World->GetNetMode() == NM_DedicatedServer)
			{
				continue;
			}

			for (TActorIterator<AComposableCameraMeshSurfaceStorageActor> Iterator(World);
				Iterator;
				++Iterator)
			{
				AComposableCameraMeshSurfaceStorageActor* StorageActor = *Iterator;
				FMeshLayerPIEPreviewCache& Cache =
					MeshLayerPIEPreviewCaches.FindOrAdd(StorageActor);
				Cache.LastSeenTick = MeshLayerPIEPreviewTickSerial;
				if (!Cache.bInitialized
					|| Cache.DataRevision != StorageActor->GetEditorDataRevision()
					|| Cache.World.Get() != World
					|| Cache.PreviewActor.IsStale())
				{
					BuildPIEPreviewCache(*StorageActor, Cache);
				}
				else if (!Cache.StorageTransform.Equals(StorageActor->GetActorTransform()))
				{
					if (AActor* PreviewActor = Cache.PreviewActor.Get())
					{
						UE::ComposableCamera::MeshEditor::UpdatePIEPreviewTransform(*PreviewActor, StorageActor->GetActorTransform());
					}
					Cache.StorageTransform = StorageActor->GetActorTransform();
				}
				FPreviewGeometryBuildResult Result;
				// Consume the next final tile as soon as the current tile finishes.
				// Its fitting/publication does not wait for the document's future.
				if (!Cache.bProjectionPending && !Cache.bPublicationPending && Cache.GeometryBuild.TakeResult(Result))
				{
					Cache.bGeometryReused = Result.bReusedGeometry;
					if (Result.bComplete)
					{
						Cache.GeometryWaitSeconds = FPlatformTime::Seconds() - Cache.GeometryStarted;
						Cache.ExpectedTriangles = Result.ExpectedTriangles;
					}
					else
					{
						if (Cache.FirstGeometrySeconds < 0.0) { Cache.FirstGeometrySeconds = FPlatformTime::Seconds() - Cache.GeometryStarted; }
						Cache.ExpectedTriangles += Result.ExpectedTriangles;
						Cache.Projection.Begin(MoveTemp(Result.LocalMeshes), MoveTemp(Result.ProjectionVertexCache), Cache.PreviewActor.IsValid());
						Cache.bProjectionPending = !Cache.Projection.IsComplete();
						Cache.bPublicationPending = Cache.bProjectionPending;
					}
				}
			}
		}

		for (auto CacheIterator = MeshLayerPIEPreviewCaches.CreateIterator();
			CacheIterator;
			++CacheIterator)
		{
			const AComposableCameraMeshSurfaceStorageActor* StorageActor =
				CacheIterator.Key().Get();
			if (!StorageActor
				|| CacheIterator.Value().LastSeenTick != MeshLayerPIEPreviewTickSerial)
			{
				ReleasePIEPreviewCache(CacheIterator.Value());
				CacheIterator.RemoveCurrent();
			}
		}

		// Reuse weak-key scratch storage. Capacity grows only when new documents appear.
		MeshLayerPIEPreviewWorkOrder.Reset();
		if (MeshLayerPIEPreviewWorkOrder.Max() < MeshLayerPIEPreviewCaches.Num())
		{
			MeshLayerPIEPreviewWorkOrder.Reserve(MeshLayerPIEPreviewCaches.Num());
		}
		for (const auto& Pair : MeshLayerPIEPreviewCaches) { MeshLayerPIEPreviewWorkOrder.Add(Pair.Key); }
		RunPIEPreviewWorkRoundRobin(MeshLayerPIEPreviewWorkOrder.Num(), NextMeshLayerPIEProjectionJob,
			[&](int32 JobIndex)
			{
				if (RemainingProjectionQueries <= 0 || RemainingProjectionSeconds <= 0.0)
				{
					return EPIEPreviewWorkResult::BudgetExhausted;
				}
				const auto StorageKey = MeshLayerPIEPreviewWorkOrder[JobIndex];
				AComposableCameraMeshSurfaceStorageActor* StorageActor = StorageKey.Get();
				FMeshLayerPIEPreviewCache* Cache = MeshLayerPIEPreviewCaches.Find(StorageKey);
				UWorld* World = Cache ? Cache->World.Get() : nullptr;
				if (!StorageActor || !Cache || !Cache->bProjectionPending || !World || World->IsBeingCleanedUp())
				{
					return EPIEPreviewWorkResult::Skipped;
				}
				TRACE_CPUPROFILER_EVENT_SCOPE(CCS_MeshLayers_PIEPreviewProjection);
				int32 Queries = 0;
				const double ProjectionStarted = FPlatformTime::Seconds();
				const bool bFinished = Cache->Projection.Advance([World, Cache](const FVector& Vertex)
				{
					return ProjectPIEPreviewVertex(*World, Cache->ProjectionTransform, Vertex,
						Cache->ProjectionQueryParams, Cache->ProjectionHits, &Cache->ProjectionStats);
				}, FMath::Min(RemainingProjectionQueries, PIEPreviewProjectionQuerySlice), RemainingProjectionSeconds, &Queries);
				RemainingProjectionQueries -= Queries;
				// Only fitting consumes the shared time budget, not discovery or uploads.
				RemainingProjectionSeconds -= FPlatformTime::Seconds() - ProjectionStarted;
				if (bFinished)
				{
					Cache->bProjectionPending = false;
					Cache->ProjectionHits.Empty();
				}
				return EPIEPreviewWorkResult::Advanced;
			});
		FPreviewPublicationBudget PublicationBudget(FPlatformTime::Seconds());
		RunPIEPreviewWorkRoundRobin(MeshLayerPIEPreviewWorkOrder.Num(), NextMeshLayerPIEPublicationJob,
			[&](int32 JobIndex)
			{
				if (!PublicationBudget.CanAdvance(FPlatformTime::Seconds()))
				{
					return EPIEPreviewWorkResult::BudgetExhausted;
				}
				const auto StorageKey = MeshLayerPIEPreviewWorkOrder[JobIndex];
				AComposableCameraMeshSurfaceStorageActor* StorageActor = StorageKey.Get();
				FMeshLayerPIEPreviewCache* Cache = MeshLayerPIEPreviewCaches.Find(StorageKey);
				if (!StorageActor || !Cache || !Cache->bPublicationPending) { return EPIEPreviewWorkResult::Skipped; }
				// One chunk per visit, so an earlier document cannot monopolize publication.
				auto ReadyMeshes = Cache->Projection.TakeReadyMeshes(1);
				if (ReadyMeshes.IsEmpty())
				{
					if (!Cache->bProjectionPending && Cache->Projection.IsPublicationComplete())
					{
						Cache->bPublicationPending = false;
						Cache->Projection.Reset();
					}
					return EPIEPreviewWorkResult::Skipped;
				}
				PublicationBudget.Consume();
				AActor* PreviewActor = AppendPIEPreviewMeshes(*StorageActor, Cache->PreviewActor.Get(), ReadyMeshes);
				// Keep an existing actor tracked for release if append validation fails.
				if (PreviewActor) { Cache->PreviewActor = PreviewActor; }
				Cache->bInitialized = IsValid(PreviewActor);
				return EPIEPreviewWorkResult::Advanced;
			});
		MeshLayerPIEPreviewWorkOrder.Reset();
#endif
		return true;
	}

	void SetMeshLayerPreviewRequested(bool bRequested)
	{
		bMeshLayerPreviewRequested = bRequested;
		UE::ComposableCamera::MeshEditor::SetPreviewViewRequested(bRequested && !bMeshLayerEditModeEntered);
#if !UE_BUILD_SHIPPING
		if (bRequested && !MeshLayerPIEPreviewTickerHandle.IsValid())
		{
			MeshLayerPIEPreviewTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateStatic(&TickMeshLayerPreview),
				0.0f);
		}
		else if (!bRequested && MeshLayerPIEPreviewTickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(MeshLayerPIEPreviewTickerHandle);
			MeshLayerPIEPreviewTickerHandle.Reset();
		}
#endif
		if (!bRequested)
		{
			ReleaseAllPIEPreviewCaches();
		}
	}

	FSlateIcon GetMeshLayerModeIcon()
	{
		const TSharedRef<FComposableCameraEditorStyle> Style =
			FComposableCameraEditorStyle::Get();
		return FSlateIcon(
			Style->GetStyleSetName(),
			TEXT("MeshCameraLayers.Mode"),
			TEXT("MeshCameraLayers.Mode.Small"));
	}
}

void FComposableCameraMeshLayerTool::Register()
{
	UE::ComposableCamera::MeshEditor::InitializePreviewGeometryBuilds();
	UE::ComposableCamera::MeshEditor::InitializePreviewViewExtension();
	if (!MeshLayerPrePIEEndedHandle.IsValid())
	{
		MeshLayerPrePIEEndedHandle = FEditorDelegates::PrePIEEnded.AddStatic(
			&HandlePrePIEEnded);
	}
	if (!MeshLayerPostPIEStartedHandle.IsValid())
	{
		MeshLayerPostPIEStartedHandle = FEditorDelegates::PostPIEStarted.AddStatic(
			&HandlePostPIEStarted);
	}

	FEditorModeRegistry::Get().RegisterMode<FComposableCameraMeshLayerEdMode>(
		FComposableCameraMeshLayerEdMode::ModeId,
		LOCTEXT("EditModeName", "Mesh Camera Layers"),
		GetMeshLayerModeIcon(),
		true,
		750);
	FEditorModeRegistry::Get().RegisterMode<FComposableCameraMeshLayerPreviewEdMode>(
		FComposableCameraMeshLayerPreviewEdMode::ModeId,
		LOCTEXT("PreviewModeName", "Mesh Camera Layer Preview"),
		GetMeshLayerModeIcon(),
		false,
		751);

	if (UToolMenus::IsToolMenuUIEnabled())
	{
		MeshLayerToolStartupCallbackHandle = UToolMenus::RegisterStartupCallback(
			FSimpleMulticastDelegate::FDelegate::CreateStatic(
				&FComposableCameraMeshLayerTool::RegisterMenus));
	}
}

void FComposableCameraMeshLayerTool::Unregister()
{
	SetMeshLayerPreviewRequested(false);
	if (MeshLayerPrePIEEndedHandle.IsValid())
	{
		FEditorDelegates::PrePIEEnded.Remove(MeshLayerPrePIEEndedHandle);
		MeshLayerPrePIEEndedHandle.Reset();
	}
	if (MeshLayerPostPIEStartedHandle.IsValid())
	{
		FEditorDelegates::PostPIEStarted.Remove(MeshLayerPostPIEStartedHandle);
		MeshLayerPostPIEStartedHandle.Reset();
	}
	if (IsEditModeActive())
	{
		GLevelEditorModeTools().DeactivateMode(FComposableCameraMeshLayerEdMode::ModeId);
	}
	if (IsPreviewModeActive())
	{
		GLevelEditorModeTools().DeactivateMode(FComposableCameraMeshLayerPreviewEdMode::ModeId);
	}
	UE::ComposableCamera::MeshEditor::ShutdownPreviewGeometryBuilds();
	UE::ComposableCamera::MeshEditor::ShutdownPreviewViewExtension();

	if (MeshLayerToolStartupCallbackHandle.IsValid())
	{
		UToolMenus::UnRegisterStartupCallback(MeshLayerToolStartupCallbackHandle);
		MeshLayerToolStartupCallbackHandle.Reset();
	}
	if (UToolMenus::IsToolMenuUIEnabled())
	{
		UToolMenus::Get()->UnregisterOwnerByName(MeshLayerToolMenuOwner);
	}

	FEditorModeRegistry::Get().UnregisterMode(FComposableCameraMeshLayerPreviewEdMode::ModeId);
	FEditorModeRegistry::Get().UnregisterMode(FComposableCameraMeshLayerEdMode::ModeId);
	// Mode destruction may retire native snapshots. Join only after those producers stop.
	UE::ComposableCamera::MeshEditor::ShutdownMeshLayerEditWork();
	FlushRenderingCommands();
}

void FComposableCameraMeshLayerTool::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(MeshLayerToolMenuOwner);
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(FComposableCameraEditorToolsMenu::MenuName);
	if (!Menu)
	{
		return;
	}

	FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("MeshLayers"));
	Section.Label = LOCTEXT("Section", "Mesh Camera Layers");
	Section.AddMenuEntry(
		TEXT("ComposableCameraMeshLayerEdit"),
		LOCTEXT("Edit", "Edit Mesh Camera Layers"),
		LOCTEXT("EditTooltip", "Open the Level-local mesh camera layer painting tool."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateStatic(&FComposableCameraMeshLayerTool::ToggleEditMode),
			FCanExecuteAction(),
			FIsActionChecked::CreateStatic(&FComposableCameraMeshLayerTool::IsEditModeActive)),
		EUserInterfaceActionType::ToggleButton);
	Section.AddMenuEntry(
		TEXT("ComposableCameraMeshLayerPreview"),
		LOCTEXT("Preview", "Show Mesh Camera Layers"),
		LOCTEXT("PreviewTooltip", "Toggle read-only rendering for all loaded mesh camera layers, including PIE."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateStatic(&FComposableCameraMeshLayerTool::TogglePreviewMode),
			FCanExecuteAction(),
			FIsActionChecked::CreateStatic(&FComposableCameraMeshLayerTool::IsPreviewModeActive)),
		EUserInterfaceActionType::ToggleButton);
}

void FComposableCameraMeshLayerTool::ToggleEditMode()
{
	if (IsEngineExitRequested())
	{
		return;
	}

	if (IsEditModeActive())
	{
		GLevelEditorModeTools().DeactivateMode(FComposableCameraMeshLayerEdMode::ModeId);
		return;
	}

	if (IsPreviewModeActive())
	{
		GLevelEditorModeTools().DeactivateMode(FComposableCameraMeshLayerPreviewEdMode::ModeId);
	}
	GLevelEditorModeTools().ActivateMode(FComposableCameraMeshLayerEdMode::ModeId);
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

void FComposableCameraMeshLayerTool::TogglePreviewMode()
{
	if (IsEngineExitRequested())
	{
		return;
	}

	if (IsPreviewModeActive())
	{
		GLevelEditorModeTools().DeactivateMode(FComposableCameraMeshLayerPreviewEdMode::ModeId);
		SetMeshLayerPreviewRequested(false);
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports();
		}
		return;
	}

	if (IsEditModeActive())
	{
		GLevelEditorModeTools().DeactivateMode(FComposableCameraMeshLayerEdMode::ModeId);
	}
	SetMeshLayerPreviewRequested(true);
	UpdateEditorPreviewMode();
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

void FComposableCameraMeshLayerTool::NotifyEditModeChanged(bool bEntered)
{
	bMeshLayerEditModeEntered = bEntered;
	UE::ComposableCamera::MeshEditor::SetPreviewViewRequested(bMeshLayerPreviewRequested && !bEntered);
	if (bEntered) { ReleaseAllPIEPreviewCaches(); }
}

void FComposableCameraMeshLayerTool::UpdateEditorPreviewMode()
{
	UpdateRequestedEditorPreviewMode();
}

bool FComposableCameraMeshLayerTool::IsEditModeActive()
{
	return !IsEngineExitRequested()
		&& GLevelEditorModeTools().IsModeActive(FComposableCameraMeshLayerEdMode::ModeId);
}

bool FComposableCameraMeshLayerTool::IsPreviewModeActive()
{
	return !IsEngineExitRequested()
		&& (bMeshLayerPreviewRequested
			|| GLevelEditorModeTools().IsModeActive(
				FComposableCameraMeshLayerPreviewEdMode::ModeId));
}

#undef LOCTEXT_NAMESPACE
