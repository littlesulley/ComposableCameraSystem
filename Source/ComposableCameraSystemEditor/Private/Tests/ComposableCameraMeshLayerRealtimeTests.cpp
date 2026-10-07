// Copyright 2026 Sulley. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "MeshCamera/ComposableCameraMeshLayerSavedPreview.h"
#include "MeshCamera/ComposableCameraMeshLayerDocumentBuild.h"
#include "MeshCamera/ComposableCameraMeshLayerEditWork.h"
#include "MeshCamera/ComposableCameraMeshLayerStrokeCoverage.h"
#include "MeshCamera/ComposableCameraMeshLayerPreviewBuild.h"
#include "MeshCamera/ComposableCameraMeshLayerEdMode.h"
#include "MeshCamera/ComposableCameraMeshLayerShapes.h"
#include "MeshCamera/ComposableCameraMeshLayerToolSettings.h"
#include "DataAssets/ComposableCameraMeshProfile.h"
#include "MeshCamera/ComposableCameraMeshSurfaceStorageActor.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/ThreadSafeCounter.h"
#include "Misc/AutomationTest.h"
#include "PrimitiveSceneProxy.h"
#include "RenderingThread.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UObject/Package.h"
#include "UObject/Class.h"

namespace
{
	using namespace UE::ComposableCamera::MeshEditor;
	void AddRealtimeTriangle(FComposableCameraMeshSurfaceAuthoringData& Data, const FGuid& Layer, double X)
	{
		const int32 First = Data.Vertices.Num();
		Data.Vertices.Append({FVector3f(X, 0, 0), FVector3f(X + 40, 0, 8), FVector3f(X, 40, 16)});
		Data.Indices.Append({First, First + 1, First + 2}); Data.TriangleLayerIds.Add(Layer);
	}
	double RealtimePreviewArea(const FPreparedEditPreview& Prepared)
	{
		double Area = 0;
		auto AddGeometry = [&Area](const auto& Geometry)
		{
			for (int32 Index = 0; Index < Geometry.Indices.Num(); Index += 3)
			{
				const FVector3f A = Geometry.Vertices[Geometry.Indices[Index]].Position;
				const FVector3f B = Geometry.Vertices[Geometry.Indices[Index + 1]].Position;
				const FVector3f C = Geometry.Vertices[Geometry.Indices[Index + 2]].Position;
				Area += FMath::Abs(FVector3f::CrossProduct(B - A, C - A).Z) * 0.5;
			}
		};
		for (const auto& Tile : Prepared.Tiles)
		{
			for (const auto& Pair : Tile.Geometry) { AddGeometry(Pair.Value); }
			for (const auto& Pair : Tile.SharedGeometry) { AddGeometry(*Pair.Value.Geometry); }
		}
		return Area;
	}
	double RealtimeActorArea(FMeshLayerEditPreview& Preview)
	{
		double Area = 0; TArray<UComposableCameraMeshLayerEditPreviewComponent*> Components;
		if (Preview.GetActor()) { Preview.GetActor()->GetComponents(Components); }
		for (const auto* Component : Components)
		{
			const auto Vertices = Component->GetVertices(); const auto Indices = Component->GetIndices();
			for (int32 Index = 0; Index < Indices.Num(); Index += 3)
			{ Area += FMath::Abs(FVector3f::CrossProduct(Vertices[Indices[Index + 1]].Position - Vertices[Indices[Index]].Position,
				Vertices[Indices[Index + 2]].Position - Vertices[Indices[Index]].Position).Z) * 0.5; }
		}
		return Area;
	}
	class FRealtimeFixtureModeTools final : public FEditorModeTools
	{
	public:
		explicit FRealtimeFixtureModeTools(UWorld& World) : FixtureWorld(&World) {}
		virtual UWorld* GetWorld() const override { return FixtureWorld.Get(); }
	private:
		TWeakObjectPtr<UWorld> FixtureWorld;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshSavedPreviewTest,
	"ComposableCameraSystem.Editor.MeshCamera.SavedPreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FComposableCameraMeshSavedPreviewTest::RunTest(const FString&)
{
	TArray<FComposableCameraMeshLayerDefinition> Layers; Layers.AddDefaulted(); Layers[0].LayerId = FGuid::NewGuid();
	FComposableCameraMeshSurfaceAuthoringData Data; AddRealtimeTriangle(Data, Layers[0].LayerId, -1000); AddRealtimeTriangle(Data, Layers[0].LayerId, 1000);
	FResolvedSurfaceVisualization Original; BuildAuthoringVisualization(Data, Layers, Original);
	auto Saved = SaveEditorPreview(Original, Layers);
	TArray<uint8> Bytes;
	{ FMemoryWriter Writer(Bytes); FObjectAndNameAsStringProxyArchive Archive(Writer, false);
		FComposableCameraMeshSurfaceEditorPreview::StaticStruct()->SerializeItem(Archive, &Saved, nullptr); }
	FComposableCameraMeshSurfaceEditorPreview Reloaded;
	{ FMemoryReader Reader(Bytes); FObjectAndNameAsStringProxyArchive Archive(Reader, false);
		FComposableCameraMeshSurfaceEditorPreview::StaticStruct()->SerializeItem(Archive, &Reloaded, nullptr); }
	FResolvedSurfaceVisualization Loaded;
	TestTrue(TEXT("Exact editor cache survives reflected serialization"), LoadEditorPreview(Reloaded, Layers, Loaded));
	const auto Expected = PrepareEditPreview(Original, Layers); const auto Actual = PrepareEditPreview(Loaded, Layers);
	const FBox2D InvalidRegion(ForceInit); const auto FullFallback = PrepareEditPreview(Loaded, Layers, nullptr, &InvalidRegion);
	TestTrue(TEXT("Invalid regional bounds preserve full-document fallback"), FullFallback.Tiles.Num() == Expected.Tiles.Num()
		&& FMath::IsNearlyEqual(RealtimePreviewArea(FullFallback), RealtimePreviewArea(Expected), 1.e-5));
	TestEqual(TEXT("Saved cache retains distant/negative tiles"), Actual.Tiles.Num(), Expected.Tiles.Num());
	TestTrue(TEXT("Saved cache retains exact fill area"), FMath::IsNearlyEqual(RealtimePreviewArea(Actual), RealtimePreviewArea(Expected), 1.e-5));
	TestEqual(TEXT("Saved cache retains resolved cell count"), Loaded.Cells.Num(), Original.Cells.Num());
	bool bExact = Loaded.Cells.Num() == Original.Cells.Num();
	for (int32 Index = 0; bExact && Index < Loaded.Cells.Num(); ++Index)
	{
		const auto& A = Original.Cells[Index]; const auto& B = Loaded.Cells[Index];
		bExact = A.LocalPosition == B.LocalPosition && A.LocalNormal == B.LocalNormal && A.LayerIndex == B.LayerIndex
			&& A.bFullCoverage == B.bFullCoverage && A.Patches.Num() == B.Patches.Num();
		for (int32 Patch = 0; bExact && Patch < A.Patches.Num(); ++Patch)
		{ bExact = A.Patches[Patch].LocalVertices == B.Patches[Patch].LocalVertices && A.Patches[Patch].LocalNormal == B.Patches[Patch].LocalNormal; }
	}
	TestTrue(TEXT("Positions, height, normals and clipped polygons remain exact"), bExact);
	Reloaded.Version = 99; const int32 Before = Loaded.Cells.Num();
	TestFalse(TEXT("Unsupported cache requests source fallback"), LoadEditorPreview(Reloaded, Layers, Loaded));
	TestEqual(TEXT("Rejected cache does not partially replace output"), Loaded.Cells.Num(), Before);
	Reloaded = Saved; Reloaded.Bounds = FBox2D(ForceInit);
	TestFalse(TEXT("Corrupt bounds cannot reach grid addressing"), LoadEditorPreview(Reloaded, Layers, Loaded));
	FMeshLayerDocumentBuild Document; const FGuid Revision = FGuid::NewGuid();
	TestTrue(TEXT("Saved Edit path starts directly"), Document.StartSaved(Saved, Data, Layers, Revision));
	FPreparedEditPreview Tile; int32 Batches = 0, TileCount = 0; FDocumentPreviewResult Result;
	const double Deadline = FPlatformTime::Seconds() + 5.0; bool bDone = false;
	while (FPlatformTime::Seconds() < Deadline)
	{
		while (Document.TakeTile(Revision, Tile)) { ++Batches; TileCount += Tile.Tiles.Num(); }
		if (Document.Take(Revision, Result)) { bDone = true; break; }
		FPlatformProcess::SleepNoStats(0.001f);
	}
	TestTrue(TEXT("Saved Edit load completes"), bDone);
	TestEqual(TEXT("Saved Edit emits one complete document rather than spatial resolver batches"), Batches, 1);
	TestEqual(TEXT("One document includes all distant geometry"), TileCount, Expected.Tiles.Num());
	FPreviewGeometryBuild Show; FComposableCameraMeshSurfaceRuntimeData Runtime;
	Show.Begin(Runtime, Layers, false, true, FVector::ZeroVector, &Saved);
	FPreviewGeometryBuildResult ShowResult; int32 ShowBatches = 0; double ShowArea = 0; bDone = false;
	const double ShowDeadline = FPlatformTime::Seconds() + 5;
	while (FPlatformTime::Seconds() < ShowDeadline)
	{
		if (!Show.TakeResult(ShowResult)) { FPlatformProcess::SleepNoStats(0.001f); continue; }
		if (ShowResult.bComplete) { bDone = true; break; }
		++ShowBatches;
		for (const auto& Mesh : ShowResult.LocalMeshes) { for (int32 Index = 0; Index < Mesh.Indices.Num(); Index += 3)
		{
			const auto& A = Mesh.LocalVertices[Mesh.Indices[Index]];
			const auto& B = Mesh.LocalVertices[Mesh.Indices[Index + 1]];
			const auto& C = Mesh.LocalVertices[Mesh.Indices[Index + 2]];
			ShowArea += FMath::Abs(FVector::CrossProduct(B - A, C - A).Z) * 0.5;
		} }
	}
	TestTrue(TEXT("Saved Show bypasses runtime source resolution"), bDone && ShowBatches == 1 && FMath::IsNearlyEqual(ShowArea, RealtimePreviewArea(Expected), 1.e-5));
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	if (!TestNotNull(TEXT("Storage fixture exists"), World)) { return false; }
	auto* Actor = World->SpawnActor<AComposableCameraMeshSurfaceStorageActor>();
	Actor->SetAuthoringData(Layers, Data); Actor->SetEditorPreview(MoveTemp(Saved));
	const uint64 Identity = Actor->GetEditorDataRevision();
	Data.Vertices[0].Z += 4; Actor->SetAuthoringData(Layers, Data);
	TestTrue(TEXT("Same-count source mutation invalidates saved display and content identity"), Actor->GetEditorDataRevision() != Identity && Actor->GetEditorPreview().Version == 0);
	World->DestroyWorld(false); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshSavePreparationTest,
	"ComposableCameraSystem.Editor.MeshCamera.SavePreparation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FComposableCameraMeshSavePreparationTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	if (!TestNotNull(TEXT("Save fixture exists"), World)) { return false; }
	const bool bResult = [this, World]()
	{
		FComposableCameraMeshLayerEdMode Mode; Mode.TargetLevel = World->PersistentLevel;
		Mode.Settings = NewObject<UComposableCameraMeshLayerToolSettings>();
		Mode.Settings->Layers.SetNum(2); Mode.Settings->NormalizeLayers();
		const FGuid LayerId = Mode.Settings->Layers[0].LayerId;
		auto& Source = Mode.Settings->WorkingData;
		Source.Vertices = {FVector3f(0, 0, 0), FVector3f(40, 0, 8), FVector3f(40, 40, 24),
			FVector3f(0, 40, 16), FVector3f(1000, 1000, 1000)};
		Source.Indices = {0, 1, 2, 0, 2, 3}; Source.TriangleLayerIds = {LayerId, LayerId};
		auto& Shape = Source.Shapes.AddDefaulted_GetRef(); Shape.ShapeId = FGuid::NewGuid(); Shape.LayerId = LayerId;
		Shape.ControlPoints = {FVector2D(0, 0), FVector2D(40, 40)};
		Shape.Erasures.AddDefaulted_GetRef().Radius = 3;
		Source.TriangleShapeIds = {Shape.ShapeId, Shape.ShapeId};
		const auto ShapeIds = Source.TriangleShapeIds; const auto Controls = Shape.ControlPoints;
		Mode.Settings->TouchDocument(); Mode.bDirty = true; Mode.CaptureSavedDocument();
		const auto SavedSource = Mode.SavedDocument->WorkingData;
		Mode.AuthoringIndex->Build(Source);
		auto Checkpoint = MakeShared<FEditPreviewCheckpoint, ESPMode::ThreadSafe>();
		Checkpoint->Revision = Mode.Settings->DocumentRevision; Checkpoint->Index = *Mode.AuthoringIndex;
		BuildAuthoringVisualization(Source, Mode.Settings->Layers, Checkpoint->Visualization);
		Mode.EditingCheckpoint = Checkpoint; Mode.bVisualizationDirty = false;
		const auto Expected = SaveEditorPreview(Checkpoint->Visualization, Mode.Settings->Layers);
		bool bReused = false; const auto Cached = Mode.PrepareSavePreview(bReused);
		TestTrue(TEXT("Save reads the immutable exact cache without replacing the editable base"), bReused
			&& Mode.EditingCheckpoint == Checkpoint && Mode.Visualization.Cells.IsEmpty()
			&& FComposableCameraMeshSurfaceEditorPreview::StaticStruct()->CompareScriptStruct(&Expected, &Cached, 0));
		Mode.RemoveOrphanedTriangles();
		TestEqual(TEXT("Compaction removes unused vertices without expanding shared corners"), Source.Vertices.Num(), 4);
		TestTrue(TEXT("Compaction preserves triangle order, ownership, controls and erasures"), Source.Indices == SavedSource.Indices
			&& Source.TriangleShapeIds == ShapeIds && Source.Shapes.Num() == 1 && Source.Shapes[0].ControlPoints == Controls
			&& Source.Shapes[0].Erasures.Num() == 1 && Source.Shapes[0].Erasures[0].Radius == 3);
		TestFalse(TEXT("Changed source counts invalidate the authoring index"), Mode.AuthoringIndex->IsCurrent(Source));
		Mode.AuthoringIndex->Build(Source); const auto* IndexVertices = Source.Vertices.GetData();
		Mode.RemoveOrphanedTriangles();
		TestTrue(TEXT("No-op compaction retains source allocation and current index"), Source.Vertices.GetData() == IndexVertices && Mode.AuthoringIndex->IsCurrent(Source));
		const auto Compacted = Mode.PrepareSavePreview(bReused);
		TestTrue(TEXT("Index compaction keeps the same exact polygons, normals and heights"), bReused
			&& FComposableCameraMeshSurfaceEditorPreview::StaticStruct()->CompareScriptStruct(&Expected, &Compacted, 0));
		Source.Vertices[0].Z += 20; Mode.bVisualizationDirty = true;
		const auto Rebuilt = Mode.PrepareSavePreview(bReused);
		FResolvedSurfaceVisualization Reference; BuildAuthoringVisualization(Source, Mode.Settings->Layers, Reference);
		const auto RebuiltExpected = SaveEditorPreview(Reference, Mode.Settings->Layers);
		TestTrue(TEXT("Same-count edits rebuild missing coverage once and retain it"), !bReused && !Mode.bVisualizationDirty
			&& !FComposableCameraMeshSurfaceEditorPreview::StaticStruct()->CompareScriptStruct(&Expected, &Rebuilt, 0)
			&& FComposableCameraMeshSurfaceEditorPreview::StaticStruct()->CompareScriptStruct(&RebuiltExpected, &Rebuilt, 0));
		(void)Mode.PrepareSavePreview(bReused); TestTrue(TEXT("The next preparation reuses the rebuilt coverage"), bReused);
		TestTrue(TEXT("Preparation does not advance the successful-save baseline or clear dirty state"), Mode.IsDirty()
			&& Mode.SavedDocument->WorkingData.Vertices == SavedSource.Vertices && Mode.SavedDocument->WorkingData.Indices == SavedSource.Indices);

		auto* Actor = World->SpawnActor<AComposableCameraMeshSurfaceStorageActor>();
		if (!TestNotNull(TEXT("Storage fixture exists"), Actor)) { return false; }
		Actor->SetAuthoringData(Mode.Settings->Layers, Source); Actor->SetEditorPreview(FComposableCameraMeshSurfaceEditorPreview(Rebuilt));
		TestEqual(TEXT("Runtime retains shared indexed vertices"), Actor->GetRuntimeData().Vertices.Num(), 4);
		TestTrue(TEXT("Runtime preserves source topology and prepares bounds/BVH"), Actor->GetRuntimeData().Indices == Source.Indices && Actor->GetRuntimeData().HasSpatialIndex());
		const auto* StoredCells = Actor->GetEditorPreview().Cells.GetData(); const auto* RuntimeVertices = Actor->GetRuntimeData().Vertices.GetData();
		const uint64 Revision = Actor->GetEditorDataRevision();
		auto& Layer = Mode.Settings->Layers[0]; Layer.Name = TEXT("Saved metadata"); Layer.DebugColor = FLinearColor::Red;
		Layer.Profile = NewObject<UComposableCameraMeshProfile>(); Layer.TraceChannel = ECC_Camera;
		TestTrue(TEXT("Unchanged stored coverage matches exact editing polygons despite metadata changes"),
			MatchesEditorPreview(Actor->GetEditorPreview(), Mode.GetVisualization(), Mode.Settings->Layers));
		auto Corrupt = Actor->GetEditorPreview(); Corrupt.Bounds = FBox2D(ForceInit);
		TestFalse(TEXT("Version-one cache with corrupt bounds cannot be retained by Save"), MatchesEditorPreview(Corrupt, Mode.GetVisualization(), Mode.Settings->Layers));
		Corrupt = Actor->GetEditorPreview();
		if (!TestFalse(TEXT("Saved fixture has exact cells"), Corrupt.Cells.IsEmpty())) { return false; }
		Corrupt.Cells[0].Normal = -Corrupt.Cells[0].Normal;
		TestFalse(TEXT("Same-count stale normals cannot take the stored-preview shortcut"), MatchesEditorPreview(Corrupt, Mode.GetVisualization(), Mode.Settings->Layers));
		Source.Shapes[0].ControlPoints[1].X += 5;
		Actor->SetAuthoringData(Mode.Settings->Layers, Source);
		FComposableCameraMeshLayerQueryResults Hits;
		TestTrue(TEXT("Metadata keeps exact stored coverage and query geometry while updating content identity"), Actor->GetEditorPreview().Cells.GetData() == StoredCells
			&& Actor->GetEditorPreview().Version == 1 && Actor->GetRuntimeData().Vertices.GetData() == RuntimeVertices
			&& Actor->GetRuntimeData().HasSpatialIndex() && Actor->GetEditorDataRevision() != Revision);
		TestTrue(TEXT("Metadata and shape controls reach their current consumers"), Actor->QueryLayers(FVector(30, 10, 100), 200, 0, Hits)
			&& Hits.Num() == 1 && Hits[0].LayerName == Layer.Name && Hits[0].Profile == Layer.Profile
			&& Actor->GetAuthoringData().Shapes[0].ControlPoints == Source.Shapes[0].ControlPoints);
		Layer.bEnabled = false; Actor->SetAuthoringData(Mode.Settings->Layers, Source);
		TestTrue(TEXT("Enabled policy invalidates coverage but retains query geometry"), Actor->GetEditorPreview().Version == 0
			&& Actor->GetRuntimeData().Vertices.GetData() == RuntimeVertices && Actor->GetRuntimeData().HasSpatialIndex());
		TestFalse(TEXT("Disabled layer is excluded immediately"), Actor->QueryLayers(FVector(30, 10, 100), 200, 0, Hits));
		Layer.bEnabled = true; Mode.Settings->Layers.Swap(0, 1); Actor->SetAuthoringData(Mode.Settings->Layers, Source);
		TestTrue(TEXT("Layer reorder rebuilds row mapping without changing stable identity"), Actor->QueryLayers(FVector(30, 10, 100), 200, 0, Hits)
			&& Hits.Num() == 1 && Hits[0].LayerId == LayerId && Hits[0].LayerOrder == 1);
		// Simulate an actor with source loaded but disposable/cooked query output missing.
		const_cast<FComposableCameraMeshSurfaceRuntimeData&>(Actor->GetRuntimeData()).Reset();
		Actor->SetAuthoringData(Mode.Settings->Layers, Source);
		TestTrue(TEXT("Equal source cannot reuse a missing nonempty query document"), Actor->GetRuntimeData().HasSpatialIndex()
			&& Actor->QueryLayers(FVector(30, 10, 100), 200, 0, Hits));
		Actor->SetEditorPreview(FComposableCameraMeshSurfaceEditorPreview(Rebuilt));
		Source.TriangleShapeIds.Add(FGuid()); Actor->SetAuthoringData(Mode.Settings->Layers, Source);
		TestTrue(TEXT("Invalid shape ownership cannot reuse otherwise identical cooked geometry"), Actor->GetRuntimeData().Indices.IsEmpty()
			&& Actor->GetEditorPreview().Version == 0);
		(void)Source.TriangleShapeIds.Pop(EAllowShrinking::No); Actor->SetAuthoringData(Mode.Settings->Layers, Source);
		TestTrue(TEXT("Repairing ownership rebuilds the missing query output"), Actor->GetRuntimeData().HasSpatialIndex());
		Mode.Settings->WorkingData.Reset(); Mode.bVisualizationDirty = true;
		const auto Empty = Mode.PrepareSavePreview(bReused);
		TestTrue(TEXT("Empty document has a valid exact empty saved cache"), !bReused && Empty.Version == 1 && Empty.Cells.IsEmpty());
		(void)Mode.PrepareSavePreview(bReused); TestTrue(TEXT("Empty exact coverage is reusable too"), bReused);
		Source = SavedSource; const FGuid DeletedLayer = FGuid::NewGuid();
		Source.Indices.Append({0, 1, 4, -1, 0, 2}); Source.TriangleLayerIds.Append({DeletedLayer, LayerId});
		Source.TriangleShapeIds.Append({FGuid(), FGuid()});
		Source.Shapes.AddDefaulted_GetRef().LayerId = DeletedLayer;
		Actor->SetAuthoringData(Mode.Settings->Layers, Source);
		TestTrue(TEXT("Runtime remapping excludes deleted-layer and invalid-index triangles"), Actor->GetRuntimeData().Vertices.Num() == 4
			&& Actor->GetRuntimeData().Indices == SavedSource.Indices && Actor->GetRuntimeData().TriangleLayerIndices.Num() == 2);
		Mode.RemoveOrphanedTriangles();
		TestTrue(TEXT("Source cleanup filters orphan data while retaining shared live topology and ownership"), Source.IsConsistent()
			&& Source.Vertices.Num() == 4 && Source.Indices == SavedSource.Indices && Source.TriangleShapeIds == ShapeIds && Source.Shapes.Num() == 1);
		return true;
	}();
	World->DestroyWorld(false); return bResult;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshFusedPreviewTest,
	"ComposableCameraSystem.Editor.MeshCamera.FusedStrokePreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FComposableCameraMeshFusedPreviewTest::RunTest(const FString&)
{
	TArray<FComposableCameraMeshLayerDefinition> Layers; Layers.AddDefaulted(); Layers[0].LayerId = FGuid::NewGuid();
	FComposableCameraMeshSurfaceAuthoringData Data; AddRealtimeTriangle(Data, Layers[0].LayerId, 0);
	FMeshLayerAuthoringIndex Index; Index.Build(Data); FResolvedSurfaceVisualization Cache;
	FMeshLayerStrokeCoverage Coverage; Coverage.EnablePreparedPreview();
	const FBox2D Bounds(FVector2D(0, 0), FVector2D(40, 40));
	Coverage.Queue(Data, Layers, Index, Bounds, 0, 10, true);
	FBox2D Changed(ForceInit); bool bFull = false;
	Coverage.Advance(Cache, Changed, bFull); FPreparedEditPreview Prepared; bool bPreparedFull = false;
	int32 Publications = 0; double Area = 0;
	const double Deadline = FPlatformTime::Seconds() + 5; bool bDone = false;
	while (FPlatformTime::Seconds() < Deadline)
	{
		while (Coverage.TakePrepared(Prepared, bPreparedFull)) { ++Publications; Area += RealtimePreviewArea(Prepared); }
		if (Coverage.Advance(Cache, Changed, bFull)) { bDone = true; break; }
		FPlatformProcess::SleepNoStats(0.001f);
	}
	TestTrue(TEXT("Coverage resolves and emits native geometry from the same work"), bDone && Publications == 1 && bPreparedFull);
	TestTrue(TEXT("Fused output matches exact source area"), FMath::IsNearlyEqual(Area, 800.0, 1.e-5));
	Data.Reset(); Index.Build(Data); Coverage.Queue(Data, Layers, Index, Bounds, INDEX_NONE, Cache.CellSize, false);
	Coverage.Advance(Cache, Changed, bFull); bDone = false; Publications = 0;
	const double EmptyDeadline = FPlatformTime::Seconds() + 5;
	while (FPlatformTime::Seconds() < EmptyDeadline)
	{
		while (Coverage.TakePrepared(Prepared, bPreparedFull)) { ++Publications; TestEqual(TEXT("Full Erase publishes empty geometry"), Prepared.Tiles.Num(), 0); }
		if (Coverage.Advance(Cache, Changed, bFull)) { bDone = true; break; }
		FPlatformProcess::SleepNoStats(0.001f);
	}
	TestTrue(TEXT("Empty Erase cannot leave a stale derived cache"), bDone && Publications == 1 && Cache.Cells.IsEmpty()); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshRealtimeEditingTest,
	"ComposableCameraSystem.Editor.MeshCamera.RealtimeEditing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FComposableCameraMeshRealtimeEditingTest::RunTest(const FString&)
{
	if (!TestNotNull(TEXT("Editor exists"), GEditor) || GEditor->IsTransactionActive()) { return false; }
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	if (!TestNotNull(TEXT("Fixture exists"), World)) { return false; }
	const bool bResult = [this, World]()
	{
		FRealtimeFixtureModeTools Owner(*World); FComposableCameraMeshLayerEdMode Mode; Mode.Owner = &Owner;
		Mode.TargetLevel = World->PersistentLevel;
		Mode.Settings = NewObject<UComposableCameraMeshLayerToolSettings>(GetTransientPackage(), NAME_None, RF_Transactional);
		Mode.SelectionEditor = NewObject<UComposableCameraMeshLayerSelection>();
		Mode.Settings->Layers.AddDefaulted(); Mode.Settings->NormalizeLayers();
		Mode.Settings->Layers[0].TraceChannel = ECC_Camera;
		AddRealtimeTriangle(Mode.Settings->WorkingData, Mode.Settings->GetActiveLayerId(), 1000);
		// Starts outside the Shape footprint but inside its edge grid cell.
		AddRealtimeTriangle(Mode.Settings->WorkingData, Mode.Settings->GetActiveLayerId(), 20.5);
		Mode.Settings->TouchDocument(); Mode.SavedRevision = Mode.Settings->DocumentRevision;
		BuildAuthoringVisualization(Mode.Settings->WorkingData, Mode.Settings->Layers, Mode.Visualization);
		Mode.AuthoringIndex->Build(Mode.Settings->WorkingData); Mode.bVisualizationDirty = false;
		Mode.bEditPreviewDirty = !Mode.EditPreview->Update(*World->PersistentLevel, FTransform::Identity, Mode.Visualization, Mode.Settings->Layers);
		Mode.PreviewRevision = Mode.Settings->DocumentRevision; Mode.RememberPreview();
		TArray<UComposableCameraMeshLayerEditPreviewComponent*> Components; Mode.EditPreview->GetActor()->GetComponents(Components);
		if (!TestEqual(TEXT("Distant and edge-neighbor components exist"), Components.Num(), 2)) { return false; }
		auto** Match = Components.FindByPredicate([](const auto* Item) { return Item->GetVertices()[0].Position.X >= 1000; });
		if (!TestNotNull(TEXT("Distant component is retained"), Match)) { return false; }
		auto* Component = *Match;
		const uint64 GeometryRevision = Component->GetGeometryRevision();
		const auto Checkpoint = Mode.EditingCheckpoint; const auto Geometry = Component->GetSharedGeometry();
		World->SendAllEndOfFrameUpdates(); FlushRenderingCommands(); const auto* Proxy = Component->GetSceneProxy();
		if (!TestNotNull(TEXT("Persistent GPU proxy exists"), Proxy)) { return false; }
		Mode.SelectionEditor->Layer = Mode.Settings->Layers[0]; Mode.SelectionEditor->Layer.DebugColor = FLinearColor(0.2f, 0.8f, 0.4f, 0.3f);
		Mode.SelectionEditor->Layer.Name = TEXT("Live name");
		{ const FScopedTransaction Transaction(FText::FromString(TEXT("Mesh metadata regression"))); Mode.Settings->Modify(); Mode.ApplySelectedLayer(); }
		Mode.RememberPreview();
		World->SendAllEndOfFrameUpdates(); FlushRenderingCommands();
		TestTrue(TEXT("Color/Name preserve immutable polygons, geometry, component and scene proxy"), Mode.EditingCheckpoint == Checkpoint
			&& Geometry == Component->GetSharedGeometry() && GeometryRevision == Component->GetGeometryRevision() && Proxy == Component->GetSceneProxy());
		TestTrue(TEXT("Metadata revision retains the same editable checkpoint"), Mode.EditPreview->FindCheckpoint(Mode.Settings->DocumentRevision) == Checkpoint && Mode.IsDirty());
		TestTrue(TEXT("Color parameter uses exact live linear color"), Component->GetFillColor() == Mode.SelectionEditor->Layer.DebugColor);
		// Repeat the render-thread colored-proxy replacement, then restore the document color.
		Component->SetFillColor(FLinearColor(0.7f, 0.1f, 0.5f, 0.3f));
		World->SendAllEndOfFrameUpdates(); FlushRenderingCommands();
		TestTrue(TEXT("Repeated color replacement retains geometry and scene proxy"), Component->GetFillColor() == FLinearColor(0.7f, 0.1f, 0.5f, 0.3f)
			&& Component->GetSharedGeometry() == Geometry && Component->GetGeometryRevision() == GeometryRevision && Component->GetSceneProxy() == Proxy);
		Component->SetFillColor(Mode.Settings->Layers[0].DebugColor);
		World->SendAllEndOfFrameUpdates(); FlushRenderingCommands();
		GEditor->RegisterForUndo(&Mode); GEditor->UndoTransaction();
		Mode.EditPreview->AdvanceQueuedUpdates(*World->PersistentLevel, FTransform::Identity, Mode.Settings->Layers, 0);
		TestTrue(TEXT("Metadata Undo restores saved color and identity without clipping"), !Mode.IsDirty() && Mode.EditingCheckpoint == Checkpoint && Component->GetSharedGeometry() == Geometry);
		GEditor->RedoTransaction(); Mode.EditPreview->AdvanceQueuedUpdates(*World->PersistentLevel, FTransform::Identity, Mode.Settings->Layers, 0);
		GEditor->UnregisterForUndo(&Mode);
		TestTrue(TEXT("Metadata Redo reuses the same editable base"), Mode.IsDirty() && Mode.EditingCheckpoint == Checkpoint && Component->GetFillColor() == Mode.Settings->Layers[0].DebugColor);
		// Fixed-capacity topology/position updates preserve GPU resources until growth exceeds capacity.
		TArray<FDynamicMeshVertex> Vertices; Vertices.Add(FDynamicMeshVertex(FVector3f(0, 0, 0))); Vertices.Add(FDynamicMeshVertex(FVector3f(20, 0, 0))); Vertices.Add(FDynamicMeshVertex(FVector3f(0, 20, 0)));
		TArray<uint32> Indices {0, 1, 2}; Component->SetGeometry(MoveTemp(Vertices), MoveTemp(Indices), Component->GetFillColor());
		World->SendAllEndOfFrameUpdates(); FlushRenderingCommands();
		TestTrue(TEXT("Small topology replacement retains scene proxy"), Proxy == Component->GetSceneProxy());
		// Restore the document before testing temporary Shape publication/history.
		Mode.EditPreview->QueueRestoreRevision(*World->PersistentLevel, Mode.Settings->DocumentRevision);
		Mode.EditPreview->AdvanceQueuedUpdates(*World->PersistentLevel, FTransform::Identity, Mode.Settings->Layers, 0);
		AActor* Floor = World->SpawnActor<AActor>(); auto* Mesh = NewObject<UStaticMeshComponent>(Floor); Floor->SetRootComponent(Mesh); Floor->AddInstanceComponent(Mesh);
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"))); Mesh->SetRelativeScale3D(FVector(10, 10, 0.1)); Floor->SetActorLocation(FVector(0, 0, -5));
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Mesh->SetCollisionResponseToAllChannels(ECR_Ignore); Mesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Block); Mesh->RegisterComponent();
		const auto Source = Mode.Settings->WorkingData; const FGuid Revision = Mode.Settings->DocumentRevision;
		FComposableCameraMeshAuthoredShape Shape; Shape.LayerId = Mode.Settings->GetActiveLayerId(); Shape.Type = EComposableCameraMeshShapeType::Rectangle;
		Shape.ControlPoints = {FVector2D(-20, -20), FVector2D(20, 20)}; Shape.SampleSpacing = 20; Shape.ProjectionDistance = 100;
		Mode.RequestLiveShapePreview(Shape);
		TestFalse(TEXT("Shape draft has same-event filled feedback"), Mode.LiveDraftFill.Indices.IsEmpty());
		const double Deadline = FPlatformTime::Seconds() + 5;
		while (!Mode.bLiveShapeVisible && FPlatformTime::Seconds() < Deadline) { Mode.AdvanceLiveShapePreview(); FPlatformProcess::SleepNoStats(0.001f); }
		TestTrue(TEXT("Live Shape projection replaces temporary fill without a source write"), Mode.bLiveShapeVisible
			&& Mode.Settings->WorkingData.Vertices == Source.Vertices && Mode.Settings->WorkingData.Indices == Source.Indices && Mode.Settings->DocumentRevision == Revision);
		Mode.EditPreview->AdvanceQueuedUpdates(*World->PersistentLevel, FTransform::Identity, Mode.Settings->Layers, 0);
		TestTrue(TEXT("Live edge cells preserve original coverage just outside the Shape"), FMath::IsNearlyEqual(RealtimeActorArea(*Mode.EditPreview), 3200.0, 0.01));
		FComposableCameraMeshAuthoredShape FarShape = Shape;
		for (auto& Point : FarShape.ControlPoints) { Point += FVector2D(100000, 100000); }
		Floor->SetActorLocation(FVector(100000, 100000, -5)); Mode.RequestLiveShapePreview(FarShape);
		const double FarDeadline = FPlatformTime::Seconds() + 5;
		while (FPlatformTime::Seconds() < FarDeadline)
		{
			Mode.AdvanceLiveShapePreview();
			if (!Mode.LiveShapeTask && !Mode.bLiveShapeRequested) { break; }
			FPlatformProcess::SleepNoStats(0.001f);
		}
		Mode.EditPreview->AdvanceQueuedUpdates(*World->PersistentLevel, FTransform::Identity, Mode.Settings->Layers, 0);
		TestTrue(TEXT("Distant temporary move resolves separate footprints, retaining original neighbors"), !Mode.LiveShapeTask
			&& Mode.LiveDraftFill.Indices.IsEmpty() && FMath::IsNearlyEqual(RealtimeActorArea(*Mode.EditPreview), 3200.0, 0.01));
		Mode.CancelLiveShapePreview(); Mode.EditPreview->AdvanceQueuedUpdates(*World->PersistentLevel, FTransform::Identity, Mode.Settings->Layers, 0);
		TestTrue(TEXT("Cancel restores exact document geometry and leaves no draft"), !Mode.bLiveShapeVisible && Mode.LiveDraftFill.Indices.IsEmpty()
			&& Component->GetSharedGeometry() == Geometry && Mode.Settings->WorkingData.Indices == Source.Indices);
		FarShape.ShapeId = FGuid::NewGuid(); Mode.QueueShapeCreation(FarShape, World);
		const double CommitDeadline = FPlatformTime::Seconds() + 5;
		while (Mode.IsCreatingShapes() && FPlatformTime::Seconds() < CommitDeadline)
		{ Mode.AdvanceShapeCreation(); FPlatformProcess::SleepNoStats(0.001f); }
		Mode.EditPreview->AdvanceQueuedUpdates(*World->PersistentLevel, FTransform::Identity, Mode.Settings->Layers, 0);
		if (!TestTrue(TEXT("Budgeted Shape commit installs complete source"), !Mode.IsCreatingShapes() && Mode.HasSelectedShape())) { return false; }
		Mode.PreviewRevision = Mode.Settings->DocumentRevision; Mode.RememberPreview();
		Mode.DeleteSelectedShape();
		TestTrue(TEXT("Shape deletion schedules local coverage rather than document reconstruction"), Mode.Settings->WorkingData.Shapes.IsEmpty()
			&& Mode.StrokeCoverage->HasPending() && !Mode.DocumentBuild->HasPending());
		Mode.AdvanceStrokeCoverage(true); Mode.EditPreview->AdvanceQueuedUpdates(*World->PersistentLevel, FTransform::Identity, Mode.Settings->Layers, 0);
		TestTrue(TEXT("Deleted Shape fill disappears while unrelated geometry remains exact"), Mode.Settings->WorkingData.Indices == Source.Indices
			&& FMath::IsNearlyEqual(RealtimeActorArea(*Mode.EditPreview), 1600.0, 0.01));
		Mode.Owner = nullptr; return true;
	}();
	World->DestroyWorld(false); return bResult;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshResidentLoadingTest,
	"ComposableCameraSystem.Editor.MeshCamera.ResidentLoadingAndBrush",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FComposableCameraMeshResidentLoadingTest::RunTest(const FString&)
{
	if (!TestNotNull(TEXT("Editor exists"), GEditor) || GEditor->IsTransactionActive()) { return false; }
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	if (!TestNotNull(TEXT("Resident fixture exists"), World)) { return false; }
	const bool bResult = [this, World]()
	{
		FRealtimeFixtureModeTools Owner(*World); FComposableCameraMeshLayerEdMode Mode; Mode.Owner = &Owner;
		Mode.TargetLevel = World->PersistentLevel;
		Mode.Settings = NewObject<UComposableCameraMeshLayerToolSettings>(GetTransientPackage(), NAME_None, RF_Transactional);
		Mode.SelectionEditor = NewObject<UComposableCameraMeshLayerSelection>();
		Mode.Settings->Layers.AddDefaulted(); Mode.Settings->NormalizeLayers();
		Mode.Settings->Layers[0].TraceChannel = ECC_Camera;
		Mode.Settings->BrushRadius = 25; Mode.Settings->ProjectionDistance = 100;
		AddRealtimeTriangle(Mode.Settings->WorkingData, Mode.Settings->GetActiveLayerId(), -1000);
		AddRealtimeTriangle(Mode.Settings->WorkingData, Mode.Settings->GetActiveLayerId(), 1000);
		AddRealtimeTriangle(Mode.Settings->WorkingData, Mode.Settings->GetActiveLayerId(), 0);
		Mode.Settings->TouchDocument(); Mode.SavedRevision = Mode.Settings->DocumentRevision;
		const auto Original = Mode.Settings->WorkingData;
		const FGuid LoadRevision = Mode.Settings->DocumentRevision;
		FResolvedSurfaceVisualization Base; BuildAuthoringVisualization(Original, Mode.Settings->Layers, Base);
		const auto Saved = SaveEditorPreview(Base, Mode.Settings->Layers);
		Mode.DocumentBuild->StartResident(Original, Mode.Settings->Layers, LoadRevision, &Saved);
		// Deliberately withhold the completed base from the mode. This exercises the load/edit
		// race deterministically, independent of how quickly the machine finishes native work.
		Mode.DocumentBuild->Wait(); FDocumentPreviewIndex EarlyIndex;
		if (!TestTrue(TEXT("Resident load releases native index before publication"), Mode.DocumentBuild->TakeIndex(LoadRevision, EarlyIndex))) { return false; }
		*Mode.AuthoringIndex = MoveTemp(EarlyIndex.Index); Mode.OpeningCellSize = EarlyIndex.CellSize; Mode.bOpeningIndexInstalled = true;
		Mode.OpeningRegionVisualization.CellSize = EarlyIndex.CellSize;
		Mode.OpeningRegionVisualization.LocalBounds = Mode.AuthoringIndex->GetProjectedBounds(Mode.Settings->Layers);
		AActor* Floor = World->SpawnActor<AActor>(); auto* Mesh = NewObject<UStaticMeshComponent>(Floor);
		Floor->SetRootComponent(Mesh); Floor->AddInstanceComponent(Mesh); Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Mesh->SetRelativeScale3D(FVector(10, 10, 0.1)); Floor->SetActorLocation(FVector(0, 0, -5));
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
		Mesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Block); Mesh->RegisterComponent();
		Mode.bHasHoverHit = true; Mode.HoverHit.ImpactPoint = FVector(100, 100, 0); Mode.HoverHit.ImpactNormal = FVector::UpVector;
		Mode.BeginStroke(); Mode.QueuePaintAtHover(nullptr);
		TestTrue(TEXT("Beginning Brush retains the in-flight base"), Mode.DocumentBuild->IsResident() && Mode.DocumentBuild->GetRevision() == LoadRevision);
		auto DrainFeedback = [&Mode]()
		{
			const double Deadline = FPlatformTime::Seconds() + 5;
			do
			{
				Mode.AdvancePainting(1024, 0.02);
				if (!Mode.StrokeTask && Mode.QueuedStampIndex >= Mode.QueuedStamps.Num() && !Mode.OpeningRegionCoverage->HasPending()) { return true; }
				FPlatformProcess::SleepNoStats(0.001f);
			} while (FPlatformTime::Seconds() < Deadline);
			return false;
		};
		if (!TestTrue(TEXT("Brush source and local feedback finish while base is withheld"), DrainFeedback())) { Mode.FinishStroke(true); return false; }
		TestTrue(TEXT("New Brush fill appears without consuming or cancelling base"), RealtimeActorArea(*Mode.EditPreview) > 800
			&& Mode.DocumentBuild->IsResident() && Mode.DocumentBuild->GetRevision() == LoadRevision);
		TestTrue(TEXT("Exact FIFO coverage waits for base rather than starting a whole-source rebuild"), Mode.StrokeCoverage->HasPending()
			&& Mode.StrokeCoverage->GetLastSnapshotTriangleCount() == 0);
		Mode.Settings->Tool = EComposableCameraMeshDrawTool::Erase; Mode.Settings->BrushRadius = 50;
		Mode.HoverHit.ImpactPoint = FVector(20, 20, 0); Mode.QueuePaintAtHover(nullptr);
		if (!TestTrue(TEXT("Erase feedback also completes during withheld load"), DrainFeedback())) { Mode.FinishStroke(true); return false; }
		FResolvedSurfaceVisualization Expected; BuildAuthoringVisualization(Mode.Settings->WorkingData, Mode.Settings->Layers, Expected);
		const double ExpectedArea = RealtimePreviewArea(PrepareEditPreview(Expected, Mode.Settings->Layers));
		TestTrue(TEXT("Complete touched regions include new Brush and exclude erased baseline triangle"),
			FMath::IsNearlyEqual(RealtimeActorArea(*Mode.EditPreview) + 1600, ExpectedArea, 0.01));
		if (!TestNotNull(TEXT("Local feedback actor exists before base publication"), Mode.EditPreview->GetActor())) { Mode.FinishStroke(true); return false; }
		TArray<UComposableCameraMeshLayerEditPreviewComponent*> Before; Mode.EditPreview->GetActor()->GetComponents(Before);
		TArray<TSharedPtr<const FEditPreviewGeometry, ESPMode::ThreadSafe>> Geometry;
		for (const auto* Component : Before) { Geometry.Add(Component->GetSharedGeometry()); }
		// Nonstructural metadata must use current color even for buffers prepared by an older worker.
		Mode.SelectionEditor->Layer = Mode.Settings->Layers[0]; Mode.SelectionEditor->Layer.Name = TEXT("Changed while loading");
		Mode.SelectionEditor->Layer.DebugColor = FLinearColor(0.3f, 0.7f, 0.2f, 0.3f); Mode.ApplySelectedLayer();
		Mode.AdvanceDocumentPreview(FVector2D::ZeroVector, true);
		TestTrue(TEXT("One installation publishes distant source while Brush transaction remains open"), Mode.bPainting
			&& !Mode.DocumentBuild->HasPending() && !Mode.EditPreview->HasQueuedUpdates()
			&& FMath::IsNearlyEqual(RealtimeActorArea(*Mode.EditPreview), ExpectedArea, 0.01));
		for (int32 Index = 0; Index < Before.Num(); ++Index)
		{ TestTrue(TEXT("Stale base preserves edited region buffers"), IsValid(Before[Index]) && Before[Index]->GetSharedGeometry() == Geometry[Index]); }
		TArray<UComposableCameraMeshLayerEditPreviewComponent*> After; Mode.EditPreview->GetActor()->GetComponents(After);
		for (const auto* Component : After)
		{ TestTrue(TEXT("Initial document uses latest metadata color"), Component->GetFillColor() == Mode.Settings->Layers[0].DebugColor); }
		TestTrue(TEXT("Base index cannot replace incrementally edited index"), Mode.AuthoringIndex->IsCurrent(Mode.Settings->WorkingData));
		Mode.FinishStroke(false, true); Mode.AdvanceStrokeCoverage(true);
		Mode.EditPreview->AdvanceQueuedUpdates(*World->PersistentLevel, FTransform::Identity, Mode.Settings->Layers, 0);
		TestTrue(TEXT("FIFO Brush/Erase converges to exact complete source"), !Mode.StrokeCoverage->HasPending()
			&& FMath::IsNearlyEqual(RealtimeActorArea(*Mode.EditPreview), ExpectedArea, 0.01));
		World->SendAllEndOfFrameUpdates(); FlushRenderingCommands();
		After.Reset(); Mode.EditPreview->GetActor()->GetComponents(After);
		for (const auto* Component : After) { TestNotNull(TEXT("Resident buffers initialize a live scene proxy"), Component->GetSceneProxy()); }
		// Cache fallback also emits one complete native document; cancellation must discard it.
		FMeshLayerDocumentBuild Fallback; Fallback.StartResident(Original, Mode.Settings->Layers, LoadRevision);
		Fallback.Wait(); FPreparedEditPreview Document;
		TestTrue(TEXT("Uncached load emits a complete shared document"), Fallback.TakeTile(LoadRevision, Document)
			&& Document.Tiles.Num() >= 3 && FMath::IsNearlyEqual(RealtimePreviewArea(Document), 2400.0, 0.01));
		TestFalse(TEXT("Resident fallback never emits a second spatial batch"), Fallback.TakeTile(LoadRevision, Document));
		Fallback.Cancel(); FDocumentPreviewResult Cancelled;
		TestFalse(TEXT("Cancelled load cannot return its obsolete complete cache"), Fallback.Take(LoadRevision, Cancelled));
		Mode.Settings->Tool = EComposableCameraMeshDrawTool::Brush; Mode.Settings->BrushRadius = 25;
		Mode.bVisualizationDirty = true;
		Mode.DocumentBuild->StartResident(Mode.Settings->WorkingData, Mode.Settings->Layers, Mode.Settings->DocumentRevision);
		const auto BeforeCancel = Mode.Settings->WorkingData;
		Mode.BeginStroke(); Mode.HoverHit.ImpactPoint = FVector(150, 150, 0); Mode.QueuePaintAtHover(nullptr); Mode.FinishStroke(true);
		TestTrue(TEXT("Stroke discard cancels both opening pipelines and restores source"), !Mode.DocumentBuild->HasPending()
			&& !Mode.OpeningRegionCoverage->HasPending() && !Mode.StrokeCoverage->HasPending()
			&& Mode.OpeningEditedRegions.IsEmpty() && Mode.Settings->WorkingData.Indices == BeforeCancel.Indices);
		// Hold both owned workers so an empty, already-current index cannot mask the
		// fact that opening's index has not arrived yet. Always release before asserting.
		FEvent* Gate = FPlatformProcess::GetSynchEventFromPool(true); FThreadSafeCounter Started;
		auto Block = [Gate, &Started]() { Started.Increment(); Gate->Wait(5000); };
		auto First = AsyncMeshLayerEdit(Block); auto Second = AsyncMeshLayerEdit(Block);
		const double GateDeadline = FPlatformTime::Seconds() + 2;
		while (Started.GetValue() != 2 && FPlatformTime::Seconds() < GateDeadline) { FPlatformProcess::SleepNoStats(0.001f); }
		const bool bWorkersHeld = Started.GetValue() == 2;
		bool bInputRetained = false;
		if (bWorkersHeld)
		{
			Mode.Settings->WorkingData.Reset(); Mode.Settings->TouchDocument(); Mode.AuthoringIndex->Build(Mode.Settings->WorkingData);
			Mode.bVisualizationDirty = true;
			Mode.DocumentBuild->StartResident(Mode.Settings->WorkingData, Mode.Settings->Layers, Mode.Settings->DocumentRevision);
			Mode.BeginStroke(); Mode.QueuePaintAtHover(nullptr); Mode.AdvancePainting(1024, 0.02);
			bInputRetained = Mode.Settings->WorkingData.Indices.IsEmpty() && Mode.QueuedStampIndex == 0
				&& !Mode.StrokeTask && Mode.DocumentBuild->IsResident() && !Mode.bOpeningIndexInstalled;
		}
		Gate->Trigger(); First.Wait(); Second.Wait(); FPlatformProcess::ReturnSynchEventToPool(Gate);
		TestTrue(TEXT("Native scheduling gate entered both workers"), bWorkersHeld);
		TestTrue(TEXT("Empty/current index cannot process Brush before opening index release"), bInputRetained);
		if (bWorkersHeld)
		{
			Mode.AdvancePainting(MAX_int32, 0.0);
			TestTrue(TEXT("Queued Brush resumes after empty base arrives with a current edited index"), !Mode.Settings->WorkingData.Indices.IsEmpty()
				&& Mode.AuthoringIndex->IsCurrent(Mode.Settings->WorkingData));
			Mode.FinishStroke(true);
		}
		Mode.Owner = nullptr; return true;
	}();
	World->DestroyWorld(false); return bResult;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshAuthoringHierarchyTest,
	"ComposableCameraSystem.Editor.MeshCamera.AuthoringHierarchy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FComposableCameraMeshAuthoringHierarchyTest::RunTest(const FString&)
{
	TArray<FComposableCameraMeshLayerDefinition> Layers; Layers.AddDefaulted(); Layers[0].LayerId = FGuid::NewGuid();
	FComposableCameraMeshSurfaceAuthoringData Data;
	for (int32 Triangle = 0; Triangle < 900; ++Triangle) { AddRealtimeTriangle(Data, Layers[0].LayerId, Triangle * 100.0); }
	FMeshLayerAuthoringIndex Index; Index.Build(Data); TArray<int32> Candidates;
	Index.FindVisualizationCandidates(FBox2D(FVector2D(0, 0), FVector2D(40, 40)), Layers, Candidates);
	TestTrue(TEXT("Hierarchical pruning excludes distant blocks"), Candidates.Num() > 0 && Candidates.Num() < 900);
	bool bAscending = true; for (int32 Id = 1; Id < Candidates.Num(); ++Id) { bAscending &= Candidates[Id] > Candidates[Id - 1]; }
	TestTrue(TEXT("Priority resolution keeps ascending original triangle order"), bAscending);
	const int32 First = Data.TriangleLayerIds.Num(); AddRealtimeTriangle(Data, Layers[0].LayerId, -500); Index.Append(Data, First);
	TestTrue(TEXT("Append refreshes hierarchical document bounds"), Index.GetProjectedBounds(Layers).Min.X == -500);
	Data.TriangleLayerIds.SetNum(10); Data.Indices.SetNum(30); Data.Vertices.SetNum(30); Index.MarkChanged(0, First + 1); Index.RefreshChanged(Data);
	Index.FindVisualizationCandidates(FBox2D(FVector2D(-500, -10), FVector2D(100000, 100)), Layers, Candidates);
	TestTrue(TEXT("Shrinking clears obsolete leaves and source IDs"), Index.IsCurrent(Data) && Candidates.Num() == 10 && Index.GetProjectedBounds(Layers).Min.X == 0);
	return true;
}
#endif
