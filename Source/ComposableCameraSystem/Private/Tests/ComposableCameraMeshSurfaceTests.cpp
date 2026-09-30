// Copyright 2026 Sulley. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "MeshCamera/ComposableCameraMeshSurfaceTypes.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

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

#endif
