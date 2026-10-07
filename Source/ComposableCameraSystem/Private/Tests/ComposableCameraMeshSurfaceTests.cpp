// Copyright 2026 Sulley. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "MeshCamera/ComposableCameraMeshSurfaceTypes.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Editor/TransBuffer.h"
#include "Engine/World.h"
#include "MeshCamera/ComposableCameraMeshSurfaceStorageActor.h"
#include "ScopedTransaction.h"
#include "UObject/StrongObjectPtr.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraMeshSurfaceLayerSetTest,
	"System.Engine.ComposableCameraSystem.MeshCamera.SurfaceLayerSet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshSurfaceLayerSetTest::RunTest(const FString& /*Parameters*/)
{
	TestNull(
		TEXT("Layer schema no longer exposes a numeric Priority"),
		FindFProperty<FProperty>(
			FComposableCameraMeshLayerDefinition::StaticStruct(),
			TEXT("Priority")));

	TArray<FComposableCameraMeshLayerDefinition> Layers;
	Layers.SetNum(2);
	Layers[0].LayerId = FGuid::NewGuid();
	Layers[0].Name = TEXT("Base");
	Layers[1].LayerId = FGuid::NewGuid();
	Layers[1].Name = TEXT("Nested");

	FComposableCameraMeshSurfaceRuntimeData Data;
	Data.Vertices = {
		FVector3f(-100.0f, -100.0f, 0.0f),
		FVector3f(100.0f, -100.0f, 0.0f),
		FVector3f(0.0f, 100.0f, 0.0f),
		FVector3f(-100.0f, -100.0f, 0.0f),
		FVector3f(100.0f, -100.0f, 0.0f),
		FVector3f(0.0f, 100.0f, 0.0f)
	};
	Data.Indices = { 0, 1, 2, 3, 4, 5 };
	Data.TriangleLayerIndices = { 0, 1 };
	Data.RebuildSpatialIndex();

	TArray<int32, TInlineAllocator<16>> LayerIndices;
	FVector SurfacePosition = FVector::ZeroVector;
	double Distance = 0.0;
	UTEST_TRUE(
		"Overlapping surface is found",
		Data.QueryLocalRayLayers(
			FVector(0.0, 0.0, 100.0),
			FVector::DownVector,
			200.0,
			5.0,
			Layers,
			LayerIndices,
			SurfacePosition,
			Distance));
	UTEST_EQUAL("Both overlapping Layers remain active", LayerIndices.Num(), 2);
	if (LayerIndices.Num() == 2)
	{
		UTEST_EQUAL("Top list row is returned first", LayerIndices[0], 0);
		UTEST_EQUAL("Lower list row remains in the active set", LayerIndices[1], 1);
	}
	UTEST_TRUE("Surface height is preserved", FMath::IsNearlyEqual(SurfacePosition.Z, 0.0));

	Data.Vertices.Append({
		FVector3f(-100.0f, -100.0f, 50.0f),
		FVector3f(100.0f, -100.0f, 50.0f),
		FVector3f(0.0f, 100.0f, 50.0f)
	});
	Data.Indices.Append({ 6, 7, 8 });
	Data.TriangleLayerIndices.Add(0);
	Data.RebuildSpatialIndex();
	LayerIndices.Reset();
	UTEST_TRUE(
		"Closer separate surface is found",
		Data.QueryLocalRayLayers(
			FVector(0.0, 0.0, 100.0),
			FVector::DownVector,
			200.0,
			5.0,
			Layers,
			LayerIndices,
			SurfacePosition,
			Distance));
	UTEST_EQUAL("Only Layers on the closest floor are returned", LayerIndices.Num(), 1);
	if (LayerIndices.Num() == 1)
	{
		UTEST_EQUAL("Closer floor keeps its authored Layer", LayerIndices[0], 0);
	}
	UTEST_TRUE("Closer floor height is returned", FMath::IsNearlyEqual(SurfacePosition.Z, 50.0));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraMeshSurfaceSpatialIndexTest,
	"System.Engine.ComposableCameraSystem.MeshCamera.SpatialIndexEquivalenceAndPruning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshSurfaceSpatialIndexTest::RunTest(const FString& /*Parameters*/)
{
	TArray<FComposableCameraMeshLayerDefinition> Layers;
	Layers.SetNum(3);
	FComposableCameraMeshSurfaceRuntimeData Linear;
	for (int32 Y = 0; Y < 32; ++Y)
	{
		for (int32 X = 0; X < 32; ++X)
		{
			for (int32 Layer = 0; Layer < 3; ++Layer)
			{
				const float Z = Layer == 0 ? 0.f : (Layer == 1 ? 50.f : 53.f);
				const int32 Base = Linear.Vertices.Num();
				Linear.Vertices.Append({ FVector3f(X * 100.f - 45.f, Y * 100.f - 45.f, Z),
					FVector3f(X * 100.f + 45.f, Y * 100.f - 45.f, Z), FVector3f(X * 100.f, Y * 100.f + 45.f, Z) });
				Linear.Indices.Append({ Base, Base + 1, Base + 2 });
				Linear.TriangleLayerIndices.Add(Layer);
			}
		}
	}
	// Invalid vertex and Layer indices remain harmless on either path.
	Linear.Indices.Append({ -1, 0, 1, 0, 1, 2 });
	Linear.TriangleLayerIndices.Append({ 0, 99 });
	FComposableCameraMeshSurfaceRuntimeData Accelerated = Linear;
	Accelerated.RebuildSpatialIndex();
	UTEST_TRUE("BVH was built outside query", Accelerated.HasSpatialIndex());
	FRandomStream Random(1052026);
	for (int32 Sample = 0; Sample < 128; ++Sample)
	{
		Layers[2].bEnabled = Sample % 3 != 0;
		const FVector Origin(Sample < 32 ? (Sample % 8) * 100.0 : Random.FRandRange(-100.f, 3300.f),
			Sample < 32 ? (Sample / 8) * 100.0 : Random.FRandRange(-100.f, 3300.f), Sample % 5 == 0 ? 40.0 : 100.0);
		const FVector Direction = Sample < 32 ? FVector::DownVector
			: FVector(Random.FRandRange(-0.1f, 0.1f), Random.FRandRange(-0.1f, 0.1f), -1.0);
		TArray<int32, TInlineAllocator<16>> ExpectedLayers, ActualLayers;
		FVector ExpectedPosition = FVector::ZeroVector, ActualPosition = FVector::ZeroVector;
		double ExpectedDistance = 0.0, ActualDistance = 0.0;
		const double Tolerance = Sample % 2 ? 5.0 : 0.0;
		const bool bExpected = Linear.QueryLocalRayLayers(Origin, Direction, 200.0, Tolerance,
			Layers, ExpectedLayers, ExpectedPosition, ExpectedDistance);
		const bool bActual = Accelerated.QueryLocalRayLayers(Origin, Direction, 200.0, Tolerance,
			Layers, ActualLayers, ActualPosition, ActualDistance);
		UTEST_EQUAL("Hit/miss matches linear reference", bActual, bExpected);
		UTEST_TRUE("Nearest-floor Layer set/order matches reference", ActualLayers == ExpectedLayers);
		if (bExpected)
		{
			UTEST_TRUE("Surface position matches reference", ActualPosition.Equals(ExpectedPosition, 1.e-7));
			UTEST_TRUE("Surface distance matches reference", FMath::IsNearlyEqual(ActualDistance, ExpectedDistance, 1.e-7));
		}
	}
	Layers[2].bEnabled = true;
	TArray<int32, TInlineAllocator<16>> FoundLayers;
	FVector Position = FVector::ZeroVector;
	double Distance = 0.0;
	int32 LinearTests = 0, IndexedTests = 0;
	UTEST_TRUE("Reference query hits", Linear.QueryLocalRayLayers(FVector(0, 0, 100), FVector::DownVector,
		200.0, 5.0, Layers, FoundLayers, Position, Distance, &LinearTests));
	UTEST_TRUE("Indexed query hits", Accelerated.QueryLocalRayLayers(FVector(0, 0, 100), FVector::DownVector,
		200.0, 5.0, Layers, FoundLayers, Position, Distance, &IndexedTests));
	UTEST_TRUE("Spatial query prunes most triangle predicates", IndexedTests < LinearTests / 8);
	UTEST_EQUAL("Closest-floor overlap remains intact", FoundLayers.Num(), 2);
	UTEST_FALSE("Remote document is rejected by root bounds", Accelerated.QueryLocalRayLayers(
		FVector(-100000, -100000, 100), FVector::DownVector, 200.0, 5.0,
		Layers, FoundLayers, Position, Distance, &IndexedTests));
	UTEST_EQUAL("Bounds miss performs no triangle predicates", IndexedTests, 0);

	TArray<int32, TInlineAllocator<16>> EdgeExpected, EdgeActual;
	const FVector ToleratedEdge(-45.0001, -45.0001, 100.0);
	const bool bEdgeExpected = Linear.QueryLocalRayLayers(ToleratedEdge, FVector::DownVector,
		200.0, 5.0, Layers, EdgeExpected, Position, Distance);
	UTEST_TRUE("Triangle edge tolerance accepts fixture", bEdgeExpected);
	UTEST_EQUAL("Bounds padding preserves edge tolerance", Accelerated.QueryLocalRayLayers(ToleratedEdge,
		FVector::DownVector, 200.0, 5.0, Layers, EdgeActual, Position, Distance), bEdgeExpected);
	UTEST_TRUE("Edge Layer set matches reference", EdgeActual == EdgeExpected);
	Accelerated.Indices.Append({ 0, 1, 2 });
	Accelerated.TriangleLayerIndices.Add(0);
	UTEST_FALSE("Changed geometry counts invalidate acceleration", Accelerated.HasSpatialIndex());
	Accelerated.RebuildSpatialIndex();
	UTEST_TRUE("Explicit rebuild restores acceleration", Accelerated.HasSpatialIndex());
	Accelerated.Reset();
	UTEST_FALSE("Reset clears acceleration", Accelerated.HasSpatialIndex());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshSurfaceMedianPartitionTest,
	"System.Engine.ComposableCameraSystem.MeshCamera.MedianPartitionEquivalence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FComposableCameraMeshSurfaceMedianPartitionTest::RunTest(const FString&)
{
	TArray<FComposableCameraMeshLayerDefinition> Layers; Layers.SetNum(3);
	FComposableCameraMeshSurfaceRuntimeData Linear;
	// Equal X/Y centroids, repeated triangles and an odd leaf split exercise selection ties.
	for (int32 Height = 0; Height < 11; ++Height)
	{
		Linear.Vertices.Append({FVector3f(-50, -50, Height), FVector3f(50, -50, Height), FVector3f(0, 50, Height)});
	}
	for (int32 Triangle = 0; Triangle < 65; ++Triangle)
	{
		const int32 First = ((Triangle * 7) % 11) * 3;
		Linear.Indices.Append({First, First + 1, First + 2}); Linear.TriangleLayerIndices.Add(Triangle % 3);
	}
	Linear.Indices.Append({-1, 0, 1}); Linear.TriangleLayerIndices.Add(0);
	FComposableCameraMeshSurfaceRuntimeData Accelerated = Linear;
	for (int32 Rebuild = 0; Rebuild < 2; ++Rebuild)
	{
		Accelerated.RebuildSpatialIndex();
		UTEST_TRUE("Selection produces a usable BVH", Accelerated.HasSpatialIndex());
		UTEST_TRUE("Selection never permutes serialized topology", Accelerated.Vertices == Linear.Vertices
			&& Accelerated.Indices == Linear.Indices && Accelerated.TriangleLayerIndices == Linear.TriangleLayerIndices);
		for (int32 Sample = 0; Sample < 12; ++Sample)
		{
			Layers[0].bEnabled = Sample % 2 != 0;
			const FVector Origin(Sample == 11 ? 1000 : 0, 0, Sample % 3 == 0 ? 5.0 : 20.0);
			const double Tolerance = Sample % 3 == 0 ? 0 : 5;
			TArray<int32, TInlineAllocator<16>> ExpectedLayers, ActualLayers;
			TArray<double, TInlineAllocator<16>> ExpectedDistances, ActualDistances;
			FVector ExpectedPosition = FVector::ZeroVector, ActualPosition = FVector::ZeroVector;
			double ExpectedDistance = 0, ActualDistance = 0;
			const bool bExpected = Linear.QueryLocalRayLayers(Origin, FVector::DownVector, 30, Tolerance,
				Layers, ExpectedLayers, ExpectedPosition, ExpectedDistance, nullptr, &ExpectedDistances);
			const bool bActual = Accelerated.QueryLocalRayLayers(Origin, FVector::DownVector, 30, Tolerance,
				Layers, ActualLayers, ActualPosition, ActualDistance, nullptr, &ActualDistances);
			UTEST_EQUAL("Odd/tied partitions preserve hit and miss", bActual, bExpected);
			UTEST_TRUE("Every layer retains its own nearest hit and row order", ActualLayers == ExpectedLayers && ActualDistances == ExpectedDistances);
			if (bExpected)
			{
				UTEST_TRUE("Nearest surface position and distance remain exact", ActualPosition.Equals(ExpectedPosition, 1.e-7)
					&& FMath::IsNearlyEqual(ActualDistance, ExpectedDistance, 1.e-7));
			}
		}
	}
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraMeshSurfaceTransformedIndexTest,
	"System.Engine.ComposableCameraSystem.MeshCamera.SpatialIndexTransformedDocument",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshSurfaceTransformedIndexTest::RunTest(const FString& /*Parameters*/)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	const bool bResult = [this, World]()
	{
		const FTransform Transform(FRotator(20, 37, 11), FVector(1234, -876, 200), FVector(2, 0.5, 1.5));
		AComposableCameraMeshSurfaceStorageActor* Storage = World->SpawnActor<AComposableCameraMeshSurfaceStorageActor>(
			AComposableCameraMeshSurfaceStorageActor::StaticClass(), Transform);
		if (!TestNotNull(TEXT("Transformed document exists"), Storage)) return false;
		TArray<FComposableCameraMeshLayerDefinition> Layers;
		Layers.SetNum(1);
		Layers[0].LayerId = FGuid::NewGuid();
		FComposableCameraMeshSurfaceAuthoringData Source;
		Source.Vertices = { FVector3f(-500, -500, 0), FVector3f(500, -500, 0), FVector3f(0, 500, 0) };
		Source.Indices = { 0, 1, 2 };
		Source.TriangleLayerIds = { Layers[0].LayerId };
		Storage->SetAuthoringData(Layers, Source);
		if (!TestTrue(TEXT("Authoring rebuild prepares BVH before query"), Storage->GetRuntimeData().HasSpatialIndex())) return false;
		FComposableCameraMeshLayerQueryResults Results;
		const FVector Surface = Transform.TransformPosition(FVector::ZeroVector);
		if (!TestTrue(TEXT("Rotated/nonuniformly scaled document is hit"),
			Storage->QueryLayers(Surface + FVector(0, 0, 100), 200.0, 5.0, Results))) return false;
		TestEqual(TEXT("Transformed hit has one Layer"), Results.Num(), 1);
		TestTrue(TEXT("Transform does not alter Layer identity"), Results[0].LayerId == Layers[0].LayerId);
		TestTrue(TEXT("World surface position remains correct"), Results[0].SurfacePosition.Equals(Surface, 1.e-6));
		TestTrue(TEXT("World distance remains correct"), FMath::IsNearlyEqual(Results[0].VerticalDistance, 100.0, 1.e-6));

		if (!TestNotNull(TEXT("Editor transactions available"), GEditor)) return false;
		TStrongObjectPtr<UTransBuffer> Transactions(NewObject<UTransBuffer>());
		Transactions->Initialize(8 * 1024 * 1024);
		TGuardValue<TObjectPtr<UTransactor>> TransactionGuard(GEditor->Trans, Transactions.Get());
		Storage->SetFlags(RF_Transactional);
		FComposableCameraMeshSurfaceAuthoringData MovedSource = Source;
		for (FVector3f& Vertex : MovedSource.Vertices) Vertex.X += 5000.f;
		{
			const FScopedTransaction Transaction(NSLOCTEXT("CCSMeshQueryTest", "MoveGeometry", "Move query geometry"));
			Storage->Modify();
			Storage->SetAuthoringData(Layers, MovedSource);
		}
		TestFalse(TEXT("Moved same-count geometry misses original ray"),
			Storage->QueryLayers(Surface + FVector(0, 0, 100), 200.0, 5.0, Results));
		TestTrue(TEXT("Geometry transaction can Undo"), Transactions->Undo());
		TestTrue(TEXT("Undo rebuilds BVH for restored same-count geometry"), Storage->GetRuntimeData().HasSpatialIndex()
			&& Storage->QueryLayers(Surface + FVector(0, 0, 100), 200.0, 5.0, Results));
		TestTrue(TEXT("Geometry transaction can Redo"), Transactions->Redo());
		TestFalse(TEXT("Redo rebuilds BVH for moved geometry"),
			Storage->QueryLayers(Surface + FVector(0, 0, 100), 200.0, 5.0, Results));
		Transactions->Reset(NSLOCTEXT("CCSMeshQueryTest", "ResetBuffer", "Query test complete"));
		return true;
	}();
	World->DestroyWorld(false);
	return bResult;
}
#endif

#endif
