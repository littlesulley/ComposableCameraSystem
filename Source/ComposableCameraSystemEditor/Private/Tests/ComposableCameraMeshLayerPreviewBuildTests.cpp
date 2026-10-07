// Copyright 2026 Sulley. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "MeshCamera/ComposableCameraMeshLayerPreviewBuild.h"
#include "MeshCamera/ComposableCameraMeshSurfaceStorageActor.h"
#include "MeshCamera/ComposableCameraMeshSurfaceTypes.h"
#include "Components/DynamicMeshComponent.h"
#include "DataAssets/ComposableCameraMeshProfile.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "PrimitiveSceneProxy.h"
#include "RenderingThread.h"
#include "SceneView.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"

namespace
{
	using namespace UE::ComposableCamera::MeshEditor;

	void CheckEditorPreviewViews(FAutomationTestBase& Test, UWorld& World,
		UDynamicMeshComponent& Component, bool bExpectedInGameView)
	{
		// Test the engine's scene-proxy visibility gate, not just registration or triangle counts.
		World.SendAllEndOfFrameUpdates();
		FlushRenderingCommands();
		FPrimitiveSceneProxy* Proxy = Component.GetSceneProxy();
		if (!Test.TestNotNull(TEXT("Editor preview has a render scene proxy"), Proxy)) { return; }
		FEngineShowFlags EditorFlags(ESFIM_Editor);
		EditorFlags.SetEditor(true); EditorFlags.SetGame(false);
		FEngineShowFlags GameFlags(ESFIM_Game);
		GameFlags.SetEditor(false); GameFlags.SetGame(true);
		FSceneViewFamilyContext EditorFamily(FSceneViewFamily::ConstructionValues(nullptr, World.Scene, EditorFlags).SetTime(FGameTime()));
		FSceneViewFamilyContext GameFamily(FSceneViewFamily::ConstructionValues(nullptr, World.Scene, GameFlags).SetTime(FGameTime()));
		FSceneViewInitOptions Options;
		Options.SetViewRectangle(FIntRect(0, 0, 64, 64));
		Options.ViewFamily = &EditorFamily;
		FSceneView EditorView(Options);
		Options.ViewFamily = &GameFamily;
		FSceneView GameView(Options);
		bool bShownInEditor = false, bShownInGameView = false;
		ENQUEUE_RENDER_COMMAND(CCSMeshLayerPreviewVisibilityTest)(
			[Proxy, &EditorView, &GameView, &bShownInEditor, &bShownInGameView](FRHICommandListImmediate&)
			{
				bShownInEditor = Proxy->GetViewRelevance(&EditorView).bDrawRelevance;
				bShownInGameView = Proxy->GetViewRelevance(&GameView).bDrawRelevance;
			});
		FlushRenderingCommands();
		Test.TestTrue(TEXT("Published preview is visible in the normal editor view"), bShownInEditor);
		Test.TestEqual(TEXT("Published preview obeys the expected Game View visibility"), bShownInGameView, bExpectedInGameView);
	}

	double PreviewBuildMeshArea(const FResolvedSurfaceLayerMesh& Mesh)
	{
		double Area = 0.0;
		for (int32 Index = 0; Index < Mesh.Indices.Num(); Index += 3)
		{
			const FVector& A = Mesh.LocalVertices[Mesh.Indices[Index]];
			const FVector& B = Mesh.LocalVertices[Mesh.Indices[Index + 1]];
			const FVector& C = Mesh.LocalVertices[Mesh.Indices[Index + 2]];
			Area += FMath::Abs(FVector::CrossProduct(B - A, C - A).Z) * 0.5;
		}
		return Area;
	}

	struct FAsyncPreviewBuildTestState
	{
		FPreviewGeometryBuild EditorBuild, PIEBuild, ReplacementBuild;
		TArray<FResolvedSurfaceLayerMesh> Expected;
		int32 ExpectedTriangles = 0;
		int32 EditorFaceCopies = 1;
		double Started = FPlatformTime::Seconds();
		bool bEditorChecked = false, bPIEChecked = false, bReplacementChecked = false;
	};

	class FCheckAsyncPreviewBuildCommand : public IAutomationLatentCommand
	{
	public:
		FCheckAsyncPreviewBuildCommand(FAutomationTestBase* InTest, TSharedRef<FAsyncPreviewBuildTestState> InState)
			: Test(InTest), State(InState) {}
		virtual bool Update() override
		{
			PumpPreviewGeometryBuilds();
			FPreviewGeometryBuildResult Result;
			if (!State->bEditorChecked && State->EditorBuild.TakeResult(Result))
			{
				State->bEditorChecked = true;
				Test->TestTrue(TEXT("Editor clipping and native topology run off the game thread"), Result.bBuiltOffGameThread);
				Test->TestTrue(TEXT("Editor worker returns only native publication chunks"), Result.LocalMeshes.IsEmpty());
				Test->TestEqual(TEXT("Worker exports complete baseline triangle count"), Result.ExpectedTriangles, State->ExpectedTriangles);
				TArray<double> Areas;
				Areas.Init(0.0, State->Expected.Num());
				int32 Triangles = 0;
				for (const auto& Chunk : Result.NativeMeshes)
				{
					Test->TestTrue(TEXT("Each native upload stays bounded"), Chunk.Mesh.TriangleCount() <= PIEPreviewChunkTriangleCount);
					Test->TestEqual(TEXT("No worker topology is rejected"), Chunk.RejectedTriangles, 0);
					Triangles += Chunk.Mesh.TriangleCount();
					if (!Areas.IsValidIndex(Chunk.LayerIndex)) { Test->AddError(TEXT("Worker lost Layer identity")); continue; }
					Test->TestTrue(TEXT("Layer color survives source mutation"), Chunk.Color == State->Expected[Chunk.LayerIndex].Color);
					for (const int32 Triangle : Chunk.Mesh.TriangleIndicesItr())
					{
						FVector A, B, C;
						Chunk.Mesh.GetTriVertices(Triangle, A, B, C);
						Areas[Chunk.LayerIndex] += FMath::Abs(FVector::CrossProduct(B - A, C - A).Z) * 0.5;
					}
				}
				Test->TestEqual(TEXT("Native chunks retain exactly the requested face copies"),
					Triangles, State->ExpectedTriangles * State->EditorFaceCopies);
				for (int32 Index = 0; Index < Areas.Num(); ++Index)
				{
					Test->TestTrue(TEXT("Overlapping Layer coverage matches synchronous reference"),
						FMath::IsNearlyEqual(Areas[Index], PreviewBuildMeshArea(State->Expected[Index]) * State->EditorFaceCopies, 1.e-5));
				}
				Test->TestFalse(TEXT("Results are consumed once"), State->EditorBuild.TakeResult(Result));
			}
			if (!State->bPIEChecked && State->PIEBuild.TakeResult(Result))
			{
				State->bPIEChecked = true;
				Test->TestTrue(TEXT("PIE clipping runs off the game thread"), Result.bBuiltOffGameThread);
				Test->TestTrue(TEXT("PIE reuses the editor's identical resolved geometry"), Result.bReusedGeometry);
				Test->TestEqual(TEXT("PIE snapshot retains visible Layer count"), Result.LocalMeshes.Num(), State->Expected.Num());
				for (int32 Index = 0; Index < Result.LocalMeshes.Num() && Index < State->Expected.Num(); ++Index)
				{
					Test->TestTrue(TEXT("PIE snapshot preserves every position/index/color after source mutation"),
						Result.LocalMeshes[Index].LocalVertices == State->Expected[Index].LocalVertices
						&& Result.LocalMeshes[Index].Indices == State->Expected[Index].Indices
						&& Result.LocalMeshes[Index].Color == State->Expected[Index].Color);
				}
				const SIZE_T PreparedCapacity = Result.ProjectionVertexCache.GetAllocatedSize();
				Test->TestTrue(TEXT("Large projection hash is allocated on the worker"), PreparedCapacity > 0);
				FPIEPreviewSurfaceProjection Projection;
				Projection.Begin(MoveTemp(Result.LocalMeshes), MoveTemp(Result.ProjectionVertexCache));
				Projection.Advance([](const FVector& Vertex) { return Vertex; }, MAX_int32);
				int32 PublishedTriangles = 0;
				while (!Projection.IsPublicationComplete())
				{
					const auto Chunks = Projection.TakeReadyMeshes(2);
					for (const auto& Chunk : Chunks) { PublishedTriangles += Chunk.Indices.Num() / 3; }
					if (Chunks.IsEmpty() && !Projection.IsPublicationComplete()) { Test->AddError(TEXT("Complete fitting stalled publication")); break; }
				}
				Test->TestEqual(TEXT("PIE keeps complete coverage through bounded tail publication"), PublishedTriangles, State->ExpectedTriangles);
			}
			if (!State->bReplacementChecked && State->ReplacementBuild.TakeResult(Result))
			{
				State->bReplacementChecked = true;
				Test->TestTrue(TEXT("Cancelled generation cannot overwrite its empty replacement"),
					Result.ExpectedTriangles == 0 && Result.NativeMeshes.IsEmpty() && Result.LocalMeshes.IsEmpty());
			}
			if (State->bEditorChecked && State->bPIEChecked && State->bReplacementChecked) { return true; }
			if (FPlatformTime::Seconds() - State->Started < 20.0) { return false; }
			State->EditorBuild.Reset(); State->PIEBuild.Reset(); State->ReplacementBuild.Reset();
			Test->AddError(TEXT("Async preview did not finish within 20 seconds"));
			return true;
		}
	private:
		FAutomationTestBase* Test;
		TSharedRef<FAsyncPreviewBuildTestState> State;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshLayerAsyncPreviewBuildTest,
	"ComposableCameraSystem.Editor.MeshCamera.AsyncPreviewBuild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshLayerAsyncPreviewBuildTest::RunTest(const FString&)
{
	using namespace UE::ComposableCamera::MeshEditor;
	TArray<FComposableCameraMeshLayerDefinition> Layers;
	Layers.SetNum(2);
	Layers[0].LayerId = FGuid::NewGuid(); Layers[1].LayerId = FGuid::NewGuid();
	Layers[0].DebugColor = FLinearColor(0.8f, 0.2f, 0.1f, 0.35f);
	Layers[1].DebugColor = FLinearColor(0.1f, 0.8f, 0.2f, 0.4f);
	FComposableCameraMeshSurfaceRuntimeData Data;
	Data.Vertices.Append({FVector3f(0, 0, 0), FVector3f(600, 0, 0), FVector3f(0, 600, 0),
		FVector3f(0, 0, 0), FVector3f(600, 0, 0), FVector3f(0, 600, 0)});
	Data.Indices.Append({0, 1, 2, 3, 4, 5});
	Data.TriangleLayerIndices.Append({0, 1});
	// Higher-priority triangle covers only a corner of the lower-priority surface.
	Data.Vertices[1].X = 160; Data.Vertices[2].Y = 160;
	const auto State = MakeShared<FAsyncPreviewBuildTestState>();
	State->EditorFaceCopies = GEngine && GEngine->GeomMaterial && !GEngine->GeomMaterial->IsTwoSided() ? 2 : 1;
	FResolvedSurfaceVisualization Visualization;
	BuildRuntimeVisualization(Data, Layers, Visualization);
	BuildVisualizationMeshes(Visualization, Layers, State->Expected);
	if (!TestEqual(TEXT("Overlapping fixture has two visible Layers"), State->Expected.Num(), 2)) { return false; }
	for (const auto& Mesh : State->Expected) { State->ExpectedTriangles += Mesh.Indices.Num() / 3; }
	TestTrue(TEXT("Fixture exceeds a single upload chunk"), State->ExpectedTriangles > PIEPreviewChunkTriangleCount);
	Layers[0].Profile = NewObject<UComposableCameraMeshProfile>();
	TWeakObjectPtr<UComposableCameraMeshProfile> WeakProfile = Layers[0].Profile;
	State->EditorBuild.Begin(Data, Layers, true);
	State->PIEBuild.Begin(Data, Layers);
	State->ReplacementBuild.Begin(Data, Layers, true);
	State->ReplacementBuild.Begin(FComposableCameraMeshSurfaceRuntimeData(), Layers, true);
	FPreviewGeometryBuild Cancelled;
	Cancelled.Begin(Data, Layers, true);
	Cancelled.Reset();
	FPreviewGeometryBuildResult Ignored;
	TestFalse(TEXT("Turning Show off immediately discards the pending handle"), Cancelled.IsPending());
	TestFalse(TEXT("Cancelled job cannot publish"), Cancelled.TakeResult(Ignored));
	Data.Reset(); Layers[0].Profile = nullptr;
	for (auto& Layer : Layers) { Layer.bEnabled = false; Layer.DebugColor = FLinearColor::Black; }
	CollectGarbage(RF_NoFlags);
	TestFalse(TEXT("Background snapshots retain no Profile UObject"), WeakProfile.IsValid());
	ADD_LATENT_AUTOMATION_COMMAND(FCheckAsyncPreviewBuildCommand(this, State));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshLayerEditorPreviewBackfacesTest,
	"ComposableCameraSystem.Editor.MeshCamera.EditorPreviewBackfaces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshLayerEditorPreviewBackfacesTest::RunTest(const FString&)
{
	using namespace UE::ComposableCamera::MeshEditor;
	TArray<FResolvedSurfaceLayerMesh> Source;
	Source.SetNum(1);
	Source[0].LayerIndex = 2; Source[0].Color = FColor(70, 180, 130, 90);
	// Include shared vertices, both source windings, a slope and a short publication tail.
	Source[0].LocalVertices = {FVector(0, 0, VisualizationSurfaceOffset),
		FVector(10, 0, VisualizationSurfaceOffset), FVector(0, 10, VisualizationSurfaceOffset),
		FVector(10, 10, 5 + VisualizationSurfaceOffset), FVector(20, 10, 5 + VisualizationSurfaceOffset)};
	Source[0].Indices = {0, 1, 2, 1, 2, 3, 1, 4, 3};
	const auto Original = Source;
	TArray<FNativePreviewLayerMesh> Native;
	BuildNativePreviewMeshes(Source, Native, 4, nullptr, true);
	if (!TestEqual(TEXT("Two-face uploads include a bounded short tail"), Native.Num(), 2)) { return false; }
	const FTransform Transform(FRotator(15, 40, 0), FVector(150, 200, 300), FVector(-2, 1.5, 0.8));
	int32 TotalTriangles = 0;
	for (const auto& Chunk : Native)
	{
		const auto& Mesh = Chunk.Mesh;
		TestTrue(TEXT("Reverse faces count toward the total upload limit"), Mesh.TriangleCount() <= 4);
		TestEqual(TEXT("No disconnected reverse face is rejected"), Chunk.RejectedTriangles, 0);
		TestEqual(TEXT("Both faces keep triangle accounting"), Mesh.TriangleCount(), Chunk.ExpectedTriangles);
		TestTrue(TEXT("Both faces keep Layer identity and color"), Chunk.LayerIndex == Source[0].LayerIndex && Chunk.Color == Source[0].Color);
		const int32 FrontVertices = Mesh.VertexCount() / 2;
		const int32 FrontTriangles = Mesh.TriangleCount() / 2;
		for (int32 Vertex = 0; Vertex < FrontVertices; ++Vertex)
		{
			const FVector Position = Mesh.GetVertex(Vertex);
			TestTrue(TEXT("Backface copy keeps exact XY and height without burying or lifting the surface"),
				Position.Equals(Mesh.GetVertex(Vertex + FrontVertices)) && Source[0].LocalVertices.Contains(Position));
		}
		for (int32 Triangle = 0; Triangle < FrontTriangles; ++Triangle)
		{
			FVector A, B, C, D, E, F;
			Mesh.GetTriVertices(Triangle, A, B, C);
			Mesh.GetTriVertices(Triangle + FrontTriangles, D, E, F);
			TestTrue(TEXT("Reverse winding covers exactly the original triangle"), A.Equals(D) && B.Equals(F) && C.Equals(E));
			const auto Back = Mesh.GetTriangle(Triangle + FrontTriangles);
			TestTrue(TEXT("Reverse face uses disconnected vertices, avoiding non-manifold rejection"),
				Back.A >= FrontVertices && Back.B >= FrontVertices && Back.C >= FrontVertices);
			A = Transform.TransformPosition(A); B = Transform.TransformPosition(B); C = Transform.TransformPosition(C);
			D = Transform.TransformPosition(D); E = Transform.TransformPosition(E); F = Transform.TransformPosition(F);
			TestTrue(TEXT("Above and below remain opposite-facing with a rotated, mirrored document"),
				FVector::DotProduct(FVector::CrossProduct(B - A, C - A), FVector::CrossProduct(E - D, F - D)) < 0.0);
		}
		TotalTriangles += Mesh.TriangleCount();
	}
	TestEqual(TEXT("Single-sided editor material receives both faces exactly once"), TotalTriangles, 6);
	BuildNativePreviewMeshes(Source, Native, 4);
	if (!TestEqual(TEXT("Single-face route needs only one chunk"), Native.Num(), 1)) { return false; }
	TestEqual(TEXT("PIE or a two-sided editor material needs no extra geometry"), Native[0].Mesh.TriangleCount(), 3);
	TestTrue(TEXT("Preview face preparation leaves source positions and topology unchanged"),
		Source[0].LocalVertices == Original[0].LocalVertices && Source[0].Indices == Original[0].Indices);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshLayerEditorPreviewPublicationTest,
	"ComposableCameraSystem.Editor.MeshCamera.EditorPreviewPublication",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshLayerEditorPreviewPublicationTest::RunTest(const FString&)
{
	using namespace UE::ComposableCamera::MeshEditor;
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	const bool bResult = [this, World]()
	{
		if (!TestNotNull(TEXT("Editor preview material exists"), GEngine ? GEngine->GeomMaterial.Get() : nullptr)) { return false; }
		AComposableCameraMeshSurfaceStorageActor* Storage = World->SpawnActor<AComposableCameraMeshSurfaceStorageActor>();
		if (!TestNotNull(TEXT("Editor storage exists"), Storage)) { return false; }
		Storage->SetActorTransform(FTransform(FVector(150, 200, 300)));
		TArray<FResolvedSurfaceLayerMesh> Source;
		Source.SetNum(1);
		Source[0].LayerIndex = 2; Source[0].Color = FColor(70, 180, 130, 90);
		for (int32 Index = 0; Index < 7; ++Index)
		{
			const int32 Base = Source[0].LocalVertices.Num();
			const FVector A(Index * 10, 0, VisualizationSurfaceOffset);
			Source[0].LocalVertices.Append({A, A + FVector(5, 0, 0), A + FVector(0, 5, 0)});
			Source[0].Indices.Append({Base, Base + 1, Base + 2});
		}
		TArray<FNativePreviewLayerMesh> Native;
		const int32 FaceCopies = GEngine->GeomMaterial->IsTwoSided() ? 1 : 2;
		BuildNativePreviewMeshes(Source, Native, 3 * FaceCopies, nullptr, FaceCopies == 2);
		if (!TestEqual(TEXT("Native chunks include a short tail"), Native.Num(), 3)) { return false; }
		TestNull(TEXT("PIE route rejects Editor worlds"), AppendNativePreviewMeshes(*Storage, nullptr, Native));
		World->GetPackage()->SetDirtyFlag(false);
		AActor* Preview = AppendNativePreviewMeshes(*Storage, nullptr, MakeArrayView(Native.GetData(), 1), true);
		if (!TestNotNull(TEXT("Editor publishes the requested chunk"), Preview)) { return false; }
		TestFalse(TEXT("Read-only preview creation does not dirty the Level"), World->GetPackage()->IsDirty());
		TestTrue(TEXT("Editor preview is transient, source-Level owned and excluded from PIE duplication"),
			Preview->HasAnyFlags(RF_Transient) && Preview->GetLevel() == Storage->GetLevel()
			&& Preview->HasAnyFlags(RF_DuplicateTransient) && !Preview->ShouldDuplicateInPIE());
		TestTrue(TEXT("Game View is allowed to render the requested overlay"), !Preview->IsEditorOnly() && !Preview->IsHidden());
		TestTrue(TEXT("Editor preview retains document transform"), Preview->GetActorTransform().Equals(Storage->GetActorTransform()));
		TArray<UDynamicMeshComponent*> Components;
		Preview->GetComponents(Components);
		if (!TestEqual(TEXT("One publication does not register other queued chunks"), Components.Num(), 1)) { return false; }
		UDynamicMeshComponent* First = Components[0];
		CheckEditorPreviewViews(*this, *World, *First, true);
		// Both independent legacy flags reproduce the regression; registration alone
		// would pass in each case even though G view contains no mesh.
		Preview->SetActorHiddenInGame(true);
		CheckEditorPreviewViews(*this, *World, *First, false);
		Preview->SetActorHiddenInGame(false);
		Preview->bIsEditorOnlyActor = true;
		Preview->MarkComponentsRenderStateDirty();
		CheckEditorPreviewViews(*this, *World, *First, false);
		Preview->bIsEditorOnlyActor = false;
		Preview->MarkComponentsRenderStateDirty();
		CheckEditorPreviewViews(*this, *World, *First, true);
		TestTrue(TEXT("Editor overlay cannot capture selection or collision"),
			!First->bSelectable && First->GetCollisionEnabled() == ECollisionEnabled::NoCollision && !First->PrimaryComponentTick.bCanEverTick);
		UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(First->GetMaterial(0));
		if (!TestNotNull(TEXT("Editor color material exists"), Material)) { return false; }
		TestTrue(TEXT("Editor retains authoring overlay material and color"), Material->Parent == GEngine->GeomMaterial.Get()
			&& Material->K2_GetVectorParameterValue(NAME_Color).Equals(FLinearColor(Source[0].Color)));
		AActor* Reused = AppendNativePreviewMeshes(*Storage, Preview, MakeArrayView(Native.GetData() + 1, 2), true);
		TestTrue(TEXT("Later chunks reuse the same persistent actor"), Reused == Preview);
		Preview->GetComponents(Components);
		int32 Submitted = 0;
		for (const auto* Component : Components)
		{
			Component->ProcessMesh([&Submitted](const UE::Geometry::FDynamicMesh3& Mesh) { Submitted += Mesh.TriangleCount(); });
		}
		TestEqual(TEXT("Bounded editor publication retains full coverage and the material's face policy"), Submitted, 7 * FaceCopies);
		DestroyPIEPreviewActor(Preview);
		for (const auto* Component : Components) { TestFalse(TEXT("Show off unregisters every editor chunk"), Component->IsRegistered()); }
		return true;
	}();
	World->DestroyWorld(false);
	return bResult;
}

namespace
{
	struct FPreviewCacheCheckJob
	{
		FPreviewGeometryBuild Build;
		TArray<FResolvedSurfaceLayerMesh> Expected;
		bool bExpectReuse = false;
		bool bChecked = false;
	};
	struct FPreviewCacheCheckState
	{
		TArray<FPreviewCacheCheckJob> Jobs;
		double Started = FPlatformTime::Seconds();
	};
	class FCheckPreviewGeometryCacheCommand : public IAutomationLatentCommand
	{
	public:
		FCheckPreviewGeometryCacheCommand(FAutomationTestBase* InTest, TSharedRef<FPreviewCacheCheckState> InState)
			: Test(InTest), State(InState) {}
		virtual bool Update() override
		{
			PumpPreviewGeometryBuilds(); bool bComplete = true;
			for (auto& Job : State->Jobs)
			{
				if (Job.bChecked) { continue; }
				FPreviewGeometryBuildResult Result;
				if (!Job.Build.TakeResult(Result)) { bComplete = false; continue; }
				Job.bChecked = true;
				Test->TestEqual(TEXT("Exact snapshots reuse; same-count geometry/Layer changes invalidate"), Result.bReusedGeometry, Job.bExpectReuse);
				Test->TestTrue(TEXT("Cache matching/copies remain off the game thread"), Result.bBuiltOffGameThread);
				Test->TestEqual(TEXT("Cache preserves resolved Layer count"), Result.LocalMeshes.Num(), Job.Expected.Num());
				for (int32 Mesh = 0; Mesh < FMath::Min(Result.LocalMeshes.Num(), Job.Expected.Num()); ++Mesh)
				{
					Test->TestTrue(TEXT("Cached or rebuilt geometry exactly matches complete resolution"),
						Result.LocalMeshes[Mesh].LocalVertices == Job.Expected[Mesh].LocalVertices
						&& Result.LocalMeshes[Mesh].Indices == Job.Expected[Mesh].Indices
						&& Result.LocalMeshes[Mesh].Color == Job.Expected[Mesh].Color
						&& Result.LocalMeshes[Mesh].LayerIndex == Job.Expected[Mesh].LayerIndex);
				}
			}
			if (bComplete) { return true; }
			if (FPlatformTime::Seconds() - State->Started < 20.0) { return false; }
			for (auto& Job : State->Jobs) { Job.Build.Reset(); }
			Test->AddError(TEXT("Preview cache jobs did not finish within 20 seconds")); return true;
		}
	private:
		FAutomationTestBase* Test;
		TSharedRef<FPreviewCacheCheckState> State;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshPreviewCacheInvalidationTest,
	"ComposableCameraSystem.Editor.MeshCamera.PreviewGeometryCacheInvalidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshPreviewCacheInvalidationTest::RunTest(const FString&)
{
	using namespace UE::ComposableCamera::MeshEditor;
	TArray<FComposableCameraMeshLayerDefinition> Layers; Layers.SetNum(2);
	Layers[0].LayerId = FGuid::NewGuid(); Layers[1].LayerId = FGuid::NewGuid();
	FComposableCameraMeshSurfaceRuntimeData Data;
	Data.Vertices = {FVector3f(0, 0, 0), FVector3f(40, 0, 0), FVector3f(0, 40, 0),
		FVector3f(0, 0, 0), FVector3f(100, 0, 0), FVector3f(0, 100, 0)};
	Data.Indices = {0, 1, 2, 3, 4, 5}; Data.TriangleLayerIndices = {0, 1};
	const auto State = MakeShared<FPreviewCacheCheckState>(); State->Jobs.Reserve(7);
	auto Queue = [&](const FComposableCameraMeshSurfaceRuntimeData& Source,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Definitions, bool bReuse)
	{
		auto& Job = State->Jobs.AddDefaulted_GetRef(); Job.bExpectReuse = bReuse;
		FResolvedSurfaceVisualization Resolved; BuildRuntimeVisualization(Source, Definitions, Resolved);
		BuildVisualizationMeshes(Resolved, Definitions, Job.Expected);
		Job.Build.Begin(Source, Definitions);
	};
	Queue(Data, Layers, false); Queue(Data, Layers, true);
	auto Changed = Data; Changed.Vertices[0].Z = 11.0f; Queue(Changed, Layers, false);
	Changed = Data; Swap(Changed.Indices[1], Changed.Indices[2]); Queue(Changed, Layers, false);
	Changed = Data; Changed.TriangleLayerIndices[0] = 1; Queue(Changed, Layers, false);
	auto Definitions = Layers; Definitions[0].DebugColor = FLinearColor(0.12f, 0.35f, 0.78f, 0.24f);
	Queue(Data, Definitions, false);
	Definitions = Layers; Definitions[1].bEnabled = false; Queue(Data, Definitions, false);
	ADD_LATENT_AUTOMATION_COMMAND(FCheckPreviewGeometryCacheCommand(this, State));
	return true;
}

namespace
{
	TArray<FString> PreviewTriangleSignatures(TConstArrayView<UE::ComposableCamera::MeshEditor::FResolvedSurfaceLayerMesh> Meshes)
	{
		TArray<FString> Signatures;
		for (const auto& Mesh : Meshes)
		{
			for (int32 Index = 0; Index + 2 < Mesh.Indices.Num(); Index += 3)
			{
				TArray<FString> Vertices;
				for (int32 Corner = 0; Corner < 3; ++Corner)
				{
					const FVector& Vertex = Mesh.LocalVertices[Mesh.Indices[Index + Corner]];
					Vertices.Add(FString::Printf(TEXT("%.9f,%.9f,%.9f"),
						FMath::Abs(Vertex.X) < 1.e-9 ? 0.0 : Vertex.X,
						FMath::Abs(Vertex.Y) < 1.e-9 ? 0.0 : Vertex.Y,
						FMath::Abs(Vertex.Z) < 1.e-9 ? 0.0 : Vertex.Z));
				}
				Vertices.Sort();
				Signatures.Add(FString::Printf(TEXT("%d|%s|%s|%s|%s"), Mesh.LayerIndex, *Mesh.Color.ToString(),
					*Vertices[0], *Vertices[1], *Vertices[2]));
			}
		}
		Signatures.Sort();
		return Signatures;
	}

	void MakeStreamingPreviewFixture(FComposableCameraMeshSurfaceRuntimeData& Data,
		TArray<FComposableCameraMeshLayerDefinition>& Layers)
	{
		Layers.SetNum(4);
		for (int32 Index = 0; Index < Layers.Num(); ++Index)
		{
			Layers[Index].LayerId = FGuid::NewGuid();
			Layers[Index].DebugColor = FLinearColor(0.2f * Index, 0.7f, 0.3f, 0.3f);
		}
		Layers[3].bEnabled = false;
		auto Triangle = [&](int32 Layer, const FVector3f& A, const FVector3f& B, const FVector3f& C)
		{
			const int32 Base = Data.Vertices.Num();
			Data.Vertices.Append({A, B, C}); Data.Indices.Append({Base, Base + 1, Base + 2});
			Data.TriangleLayerIndices.Add(Layer);
		};
		// Negative coordinates, tile seams, intersecting slopes, overlapping paint,
		// a separate storey and disabled bounds must match complete resolution.
		Triangle(1, FVector3f(-330, -220, 0), FVector3f(730, -220, 0), FVector3f(-330, 660, 0));
		Triangle(0, FVector3f(-330, -220, 8), FVector3f(730, -220, 70), FVector3f(-330, 660, -35));
		Triangle(0, FVector3f(-320, -210, 8), FVector3f(360, -210, 45), FVector3f(-320, 320, -18));
		Triangle(2, FVector3f(-330, -220, 300), FVector3f(-330, 660, 300), FVector3f(730, -220, 300));
		Triangle(3, FVector3f(-5000, -5000, 0), FVector3f(5000, -5000, 0), FVector3f(-5000, 5000, 0));
	}

	struct FStreamingPreviewCheckState
	{
		FPreviewGeometryBuild EditorBuild, PIEBuild;
		TArray<FResolvedSurfaceLayerMesh> PIE;
		TArray<FString> Expected;
		int32 EditorTriangles = 0, ExpectedTriangles = 0, EditorBatches = 0, PIEBatches = 0, FaceCopies = 1;
		int32 PIEPublishedTriangles = 0;
		bool bEditorComplete = false, bPIEComplete = false;
		double Started = FPlatformTime::Seconds();
	};
	class FCheckStreamingPreviewCommand : public IAutomationLatentCommand
	{
	public:
		FCheckStreamingPreviewCommand(FAutomationTestBase* InTest, TSharedRef<FStreamingPreviewCheckState> InState)
			: Test(InTest), State(InState) {}
		virtual bool Update() override
		{
			PumpPreviewGeometryBuilds();
			FPreviewGeometryBuildResult Result;
			while (State->EditorBuild.TakeResult(Result))
			{
				Test->TestTrue(TEXT("Every editor tile is prepared off-thread"), Result.bBuiltOffGameThread);
				if (Result.bComplete)
				{
					State->bEditorComplete = true;
					Test->TestEqual(TEXT("Editor terminal count matches full coverage"), Result.ExpectedTriangles, State->ExpectedTriangles);
					Test->TestEqual(TEXT("Terminal result follows all editor chunks"), State->EditorTriangles, State->ExpectedTriangles * State->FaceCopies);
					continue;
				}
				++State->EditorBatches;
				Test->TestTrue(TEXT("Partial batch retains its pending handle"), State->EditorBuild.IsPending());
				for (const auto& Chunk : Result.NativeMeshes)
				{
					Test->TestEqual(TEXT("Streamed native topology rejects no triangles"), Chunk.RejectedTriangles, 0);
					State->EditorTriangles += Chunk.Mesh.TriangleCount();
				}
				if (State->EditorBatches == 1)
				{
					Test->TestTrue(TEXT("Editor can publish its first final tile before receiving the rest"),
						State->EditorTriangles > 0 && State->EditorTriangles < State->ExpectedTriangles * State->FaceCopies);
				}
			}
			while (State->PIEBuild.TakeResult(Result))
			{
				Test->TestTrue(TEXT("PIE reuses tiled geometry even with a different focus"), Result.bReusedGeometry);
				if (Result.bComplete)
				{
					State->bPIEComplete = true;
					Test->TestEqual(TEXT("PIE terminal count matches full coverage"), Result.ExpectedTriangles, State->ExpectedTriangles);
					Test->TestEqual(TEXT("PIE per-batch fitting publishes every tail before the terminal marker"),
						State->PIEPublishedTriangles, State->ExpectedTriangles);
					Test->TestTrue(TEXT("Streamed tiles preserve every triangle, height, Layer and color"),
						PreviewTriangleSignatures(State->PIE) == State->Expected);
					continue;
				}
				++State->PIEBatches;
				Test->TestTrue(TEXT("PIE hash storage is prepared on the worker for each batch"), Result.ProjectionVertexCache.GetAllocatedSize() > 0);
				State->PIE.Append(Result.LocalMeshes);
				int32 Vertices = 0;
				for (const auto& Mesh : Result.LocalMeshes) { Vertices += Mesh.LocalVertices.Num(); }
				FPIEPreviewSurfaceProjection Projection;
				Projection.Begin(MoveTemp(Result.LocalMeshes), MoveTemp(Result.ProjectionVertexCache), State->PIEPublishedTriangles > 0);
				const int32 MaxSteps = Vertices / 64 + Result.ExpectedTriangles / PIEPreviewChunkTriangleCount + 16;
				for (int32 Step = 0; Step < MaxSteps && !Projection.IsPublicationComplete(); ++Step)
				{
					Projection.Advance([](const FVector& Vertex) { return Vertex; }, 64);
					for (const auto& Chunk : Projection.TakeReadyMeshes(1)) { State->PIEPublishedTriangles += Chunk.Indices.Num() / 3; }
				}
				Test->TestTrue(TEXT("Each tile's fitting and publication complete without stalling the next tile"), Projection.IsPublicationComplete());
			}
			if (State->bEditorComplete && State->bPIEComplete)
			{
				Test->TestTrue(TEXT("Both worlds receive several batches before their terminal marker"), State->EditorBatches > 1 && State->PIEBatches > 1);
				return true;
			}
			if (FPlatformTime::Seconds() - State->Started < 20.0) { return false; }
			State->EditorBuild.Reset(); State->PIEBuild.Reset(); Test->AddError(TEXT("Streaming preview timed out")); return true;
		}
	private:
		FAutomationTestBase* Test;
		TSharedRef<FStreamingPreviewCheckState> State;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshStreamingPreviewTest,
	"ComposableCameraSystem.Editor.MeshCamera.StreamingPreviewGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshStreamingPreviewTest::RunTest(const FString&)
{
	using namespace UE::ComposableCamera::MeshEditor;
	FComposableCameraMeshSurfaceRuntimeData Data;
	TArray<FComposableCameraMeshLayerDefinition> Layers;
	MakeStreamingPreviewFixture(Data, Layers);
	FResolvedSurfaceVisualization Full;
	BuildRuntimeVisualization(Data, Layers, Full);
	TArray<FResolvedSurfaceLayerMesh> Reference;
	BuildVisualizationMeshes(Full, Layers, Reference);
	const auto State = MakeShared<FStreamingPreviewCheckState>();
	State->Expected = PreviewTriangleSignatures(Reference);
	State->ExpectedTriangles = State->Expected.Num();
	State->FaceCopies = GEngine && GEngine->GeomMaterial && !GEngine->GeomMaterial->IsTwoSided() ? 2 : 1;
	if (!TestTrue(TEXT("Fixture spans many tiles"), State->ExpectedTriangles > PIEPreviewChunkTriangleCount * 4)) { return false; }
	State->EditorBuild.Begin(Data, Layers, true, true, FVector(700, -200, 0));
	State->PIEBuild.Begin(Data, Layers, false, true, FVector(-300, 600, 0));
	FPreviewGeometryBuild Cancelled;
	Cancelled.Begin(Data, Layers, true, true); Cancelled.Reset();
	FPreviewGeometryBuildResult Ignored;
	TestFalse(TEXT("Cancelled stream cannot expose queued tiles"), Cancelled.TakeResult(Ignored));
	// A first tile is already final; stopping immediately does not require the
	// remaining tiles, and all of its triangles occur in the complete reference.
	std::atomic_bool Stop{false};
	TArray<FResolvedSurfaceLayerMesh> First;
	int32 Tiles = 0;
	BuildRuntimeVisualizationTiles(Data, Layers, FVector2D(700, -200), [&](FResolvedSurfaceVisualization&& Tile)
	{
		++Tiles; BuildVisualizationMeshes(Tile, Layers, First); Stop.store(true, std::memory_order_relaxed);
	}, &Stop);
	TestEqual(TEXT("Cancellation after first final tile skips the remaining tiles"), Tiles, 1);
	TestTrue(TEXT("First tile is nonempty and smaller than the document"), !First.IsEmpty()
		&& PreviewTriangleSignatures(First).Num() < State->Expected.Num());
	for (const auto& Mesh : First)
	{
		for (const FVector& Vertex : Mesh.LocalVertices)
		{
			TestTrue(TEXT("First cold tile is near the requested focus, not the far negative corner"), Vertex.X >= 640.0 - 1.e-8);
			TestTrue(TEXT("Startup resolves only an 8-by-8-cell final region before the regular tile"),
				Vertex.X >= 660.0 - 1.e-8 && Vertex.X <= 740.0 + 1.e-8
				&& Vertex.Y >= -240.0 - 1.e-8 && Vertex.Y <= -160.0 + 1.e-8);
		}
	}
	Data.Reset(); Layers.Empty(); // Worker snapshots must remain independent.
	ADD_LATENT_AUTOMATION_COMMAND(FCheckStreamingPreviewCommand(this, State));
	return true;
}

#endif
