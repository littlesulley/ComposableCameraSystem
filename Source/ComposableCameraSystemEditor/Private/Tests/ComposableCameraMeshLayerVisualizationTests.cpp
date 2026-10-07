// Copyright 2026 Sulley. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "MeshCamera/ComposableCameraMeshLayerRendering.h"
#include "MeshCamera/ComposableCameraMeshLayerPIEPreview.h"
#include "MeshCamera/ComposableCameraMeshLayerShapes.h"
#include "MeshCamera/ComposableCameraMeshLayerAuthoringIndex.h"
#include "MeshCamera/ComposableCameraMeshLayerStrokeCoverage.h"
#include "MeshCamera/ComposableCameraMeshLayerEditPreview.h"
#include "MeshCamera/ComposableCameraMeshLayerDocumentBuild.h"
#include "MeshCamera/ComposableCameraMeshSurfaceTypes.h"
#include "MeshCamera/ComposableCameraMeshSurfaceStorageActor.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/LineBatchComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "CollisionQueryParams.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/Engine.h"
#include "Engine/HitResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "UDynamicMesh.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	void AddExactVisualizationTriangle(FComposableCameraMeshSurfaceAuthoringData& Data, const FGuid& LayerId,
		const FVector& A, const FVector& B, const FVector& C)
	{
		const int32 Base = Data.Vertices.Num();
		Data.Vertices.Append({FVector3f(A), FVector3f(B), FVector3f(C)});
		Data.Indices.Append({Base, Base + 1, Base + 2});
		Data.TriangleLayerIds.Add(LayerId);
	}

	double VisualizationMeshArea(const UE::ComposableCamera::MeshEditor::FResolvedSurfaceLayerMesh& Mesh)
	{
		double Area = 0.0;
		for (int32 Index = 0; Index + 2 < Mesh.Indices.Num(); Index += 3)
		{
			if (!Mesh.LocalVertices.IsValidIndex(Mesh.Indices[Index])
				|| !Mesh.LocalVertices.IsValidIndex(Mesh.Indices[Index + 1])
				|| !Mesh.LocalVertices.IsValidIndex(Mesh.Indices[Index + 2])) { return -1.0; }
			const FVector& A = Mesh.LocalVertices[Mesh.Indices[Index]];
			const FVector& B = Mesh.LocalVertices[Mesh.Indices[Index + 1]];
			const FVector& C = Mesh.LocalVertices[Mesh.Indices[Index + 2]];
			Area += FMath::Abs(FVector::CrossProduct(B - A, C - A).Z) * 0.5;
		}
		return Area;
	}

	bool InProjectedTriangle(const FVector& Point, const FVector& A, const FVector& B, const FVector& C)
	{
		const double Area = FVector::CrossProduct(B - A, C - A).Z;
		if (FMath::Abs(Area) <= UE_DOUBLE_SMALL_NUMBER) { return false; }
		const double V = FVector::CrossProduct(Point - A, C - A).Z / Area;
		const double W = FVector::CrossProduct(B - A, Point - A).Z / Area;
		return V >= -1.e-8 && W >= -1.e-8 && V + W <= 1.0 + 1.e-8;
	}

	bool VisualizationCovers(const UE::ComposableCamera::MeshEditor::FResolvedSurfaceLayerMesh& Mesh, const FVector& Point)
	{
		for (int32 Index = 0; Index + 2 < Mesh.Indices.Num(); Index += 3)
		{
			if (InProjectedTriangle(Point, Mesh.LocalVertices[Mesh.Indices[Index]],
				Mesh.LocalVertices[Mesh.Indices[Index + 1]], Mesh.LocalVertices[Mesh.Indices[Index + 2]])) { return true; }
		}
		return false;
	}

	double VisualizationPatchArea(const UE::ComposableCamera::MeshEditor::FResolvedSurfacePatch& Patch)
	{
		double Area = 0.0;
		for (int32 Index = 1; Index + 1 < Patch.LocalVertices.Num(); ++Index)
		{
			Area += FMath::Abs(FVector::CrossProduct(Patch.LocalVertices[Index] - Patch.LocalVertices[0],
				Patch.LocalVertices[Index + 1] - Patch.LocalVertices[0]).Z) * 0.5;
		}
		return Area;
	}

	bool CellCoversPoint(const UE::ComposableCamera::MeshEditor::FResolvedSurfaceCell& Cell, int32 Layer, const FVector& Point)
	{
		for (const auto& Patch : Cell.Patches)
		{
			if (Patch.LayerIndex != Layer || FMath::Abs(FVector::DotProduct(Point - Patch.LocalVertices[0], Patch.LocalNormal)) > 1.e-3) { continue; }
			for (int32 Index = 1; Index + 1 < Patch.LocalVertices.Num(); ++Index)
			{
				if (InProjectedTriangle(Point, Patch.LocalVertices[0], Patch.LocalVertices[Index], Patch.LocalVertices[Index + 1])) { return true; }
			}
		}
		return false;
	}

	bool SameVisualizationCoverage(const UE::ComposableCamera::MeshEditor::FResolvedSurfaceCell& A,
		const UE::ComposableCamera::MeshEditor::FResolvedSurfaceCell& B)
	{
		// Retained cells may use a different tessellation after local source cuts.
		// Compare Layer area and interior coverage in both directions, not vertex order.
		TMap<int32, double> AreasA, AreasB;
		for (const auto& Patch : A.Patches) { AreasA.FindOrAdd(Patch.LayerIndex) += VisualizationPatchArea(Patch); }
		for (const auto& Patch : B.Patches) { AreasB.FindOrAdd(Patch.LayerIndex) += VisualizationPatchArea(Patch); }
		for (const auto& Pair : AreasA)
		{
			const double* Other = AreasB.Find(Pair.Key);
			if (!FMath::IsNearlyEqual(Pair.Value, Other ? *Other : 0.0, 1.e-3)) { return false; }
		}
		for (const auto& Pair : AreasB)
		{
			const double* Other = AreasA.Find(Pair.Key);
			if (!FMath::IsNearlyEqual(Pair.Value, Other ? *Other : 0.0, 1.e-3)) { return false; }
		}
		auto ContainsInteriors = [](const auto& Source, const auto& Target)
		{
			for (const auto& Patch : Source.Patches)
			{
				if (VisualizationPatchArea(Patch) <= 1.e-3) { continue; } // FVector3f cut roundoff.
				for (int32 Index = 1; Index + 1 < Patch.LocalVertices.Num(); ++Index)
				{
					if (FMath::Abs(FVector::CrossProduct(Patch.LocalVertices[Index] - Patch.LocalVertices[0],
						Patch.LocalVertices[Index + 1] - Patch.LocalVertices[0]).Z) <= UE_DOUBLE_SMALL_NUMBER) { continue; }
					const FVector Center = (Patch.LocalVertices[0] + Patch.LocalVertices[Index] + Patch.LocalVertices[Index + 1]) / 3.0;
					if (!CellCoversPoint(Target, Patch.LayerIndex, Center)) { return false; }
				}
			}
			return true;
		};
		return ContainsInteriors(A, B) && ContainsInteriors(B, A);
	}

	bool SameResolvedVisualization(const UE::ComposableCamera::MeshEditor::FResolvedSurfaceVisualization& A,
		const UE::ComposableCamera::MeshEditor::FResolvedSurfaceVisualization& B)
	{
		if (!FMath::IsNearlyEqual(A.CellSize, B.CellSize) || A.LocalBounds.bIsValid != B.LocalBounds.bIsValid) { return false; }
		if (A.LocalBounds.bIsValid && (!A.LocalBounds.Min.Equals(B.LocalBounds.Min) || !A.LocalBounds.Max.Equals(B.LocalBounds.Max))) { return false; }
		auto ContainsCoverage = [](const auto& Source, const auto& Target)
		{
			for (const auto& Pair : Source.CellsByGrid)
			{
				const auto* Other = Target.CellsByGrid.Find(Pair.Key);
				for (int32 Index : Pair.Value)
				{
					const auto& Cell = Source.Cells[Index];
					double Area = 0.0; for (const auto& Patch : Cell.Patches) { Area += VisualizationPatchArea(Patch); }
					if (Area <= 1.e-3) { continue; }
					if (!Other || !Other->ContainsByPredicate([&](int32 TargetIndex)
					{
						return SameVisualizationCoverage(Cell, Target.Cells[TargetIndex]);
					})) { return false; }
				}
			}
			return true;
		};
		return ContainsCoverage(A, B) && ContainsCoverage(B, A);
	}

	void AddMeshVisualizationTriangle(
		FComposableCameraMeshSurfaceAuthoringData& Data,
		const FGuid& LayerId,
		float Height = 0.0f,
		const FVector2D& Offset = FVector2D::ZeroVector)
	{
		const int32 FirstVertex = Data.Vertices.Add(FVector3f(FVector(Offset.X, Offset.Y, Height)));
		Data.Vertices.Add(FVector3f(FVector(Offset.X + 100.0, Offset.Y, Height)));
		Data.Vertices.Add(FVector3f(FVector(Offset.X, Offset.Y + 100.0, Height)));
		Data.Indices.Add(FirstVertex);
		Data.Indices.Add(FirstVertex + 1);
		Data.Indices.Add(FirstVertex + 2);
		Data.TriangleLayerIds.Add(LayerId);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshIncrementalVisualizationTest,
	"ComposableCameraSystem.Editor.MeshCamera.IncrementalVisualization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshIncrementalVisualizationTest::RunTest(const FString&)
{
	using namespace UE::ComposableCamera::MeshEditor;
	TArray<FComposableCameraMeshLayerDefinition> Layers;
	Layers.SetNum(2);
	Layers[0].LayerId = FGuid::NewGuid(); Layers[1].LayerId = FGuid::NewGuid();
	FComposableCameraMeshSurfaceAuthoringData Data;
	AddMeshVisualizationTriangle(Data, Layers[1].LayerId);
	AddMeshVisualizationTriangle(Data, Layers[1].LayerId, 100.0f); // Stacked surface.
	AddMeshVisualizationTriangle(Data, Layers[0].LayerId, 0.0f, FVector2D(500.0, 500.0));
	FResolvedSurfaceVisualization Incremental;
	BuildAuthoringVisualization(Data, Layers, Incremental);
	auto MatchesFullBuild = [&]()
	{
		FResolvedSurfaceVisualization Full;
		BuildAuthoringVisualization(Data, Layers, Full);
		if (!FMath::IsNearlyEqual(Full.CellSize, Incremental.CellSize)) { return false; }
		TSet<int32> IndexedCells;
		for (const auto& Pair : Incremental.CellsByGrid)
		{
			for (int32 Index : Pair.Value)
			{
				if (!Incremental.Cells.IsValidIndex(Index) || IndexedCells.Contains(Index)) { return false; }
				const auto& Cell = Incremental.Cells[Index];
				const FIntPoint Grid(FMath::FloorToInt(Cell.LocalPosition.X / Incremental.CellSize),
					FMath::FloorToInt(Cell.LocalPosition.Y / Incremental.CellSize));
				if (Grid != Pair.Key) { return false; }
				IndexedCells.Add(Index);
			}
		}
		if (IndexedCells.Num() != Incremental.Cells.Num()) { return false; }
		auto MatchesCoverage = [](const auto& Source, const auto& Target)
		{
			for (const auto& Pair : Source.CellsByGrid)
			{
				const auto* TargetIndices = Target.CellsByGrid.Find(Pair.Key);
				for (int32 Index : Pair.Value)
				{
					const auto& Cell = Source.Cells[Index];
					double Area = 0.0;
					for (const auto& Patch : Cell.Patches) { Area += VisualizationPatchArea(Patch); }
					if (Area <= 1.e-3) { continue; } // Ignore submillimeter float boundary slivers.
					if (!TargetIndices || !TargetIndices->ContainsByPredicate([&](int32 TargetIndex)
					{
						const auto& Other = Target.Cells[TargetIndex];
						return SameVisualizationCoverage(Cell, Other); // Center Z is only a summary; compare all actual patch heights.
					})) { return false; }
				}
			}
			return true;
		};
		return MatchesCoverage(Incremental, Full) && MatchesCoverage(Full, Incremental);
	};
	AddMeshVisualizationTriangle(Data, Layers[0].LayerId);
	FVisualizationUpdateStats Stats;
	UpdateAuthoringVisualization(Data, Layers, FBox2D(FVector2D(0.0, 0.0), FVector2D(100.0, 100.0)), Incremental, &Stats);
	TestFalse(TEXT("A brush-sized change keeps the existing grid"), Stats.bFullRebuild);
	TestEqual(TEXT("Far triangles are excluded from rasterization"), Stats.RasterizedTriangles, 3);
	TestTrue(TEXT("Local paint preserves remote cells and stacked Layer winners"), MatchesFullBuild());
	FComposableCameraMeshEraseStamp Stamp;
	Stamp.Center = FVector(20.0, 20.0, 0.0); Stamp.Radius = 10.0; Stamp.Depth = 10.0;
	FBox2D ChangedBounds(ForceInit);
	TestTrue(TEXT("Fixture erases top Layer coverage"), EraseShapeGeometry(Data, Layers[0].LayerId, Stamp, false, nullptr, &ChangedBounds));
	UpdateAuthoringVisualization(Data, Layers, ChangedBounds, Incremental, &Stats);
	TestFalse(TEXT("Local erasure avoids a full rebuild"), Stats.bFullRebuild);
	TestTrue(TEXT("Erase reveals lower Layer without corrupting sparse-cell indices"), MatchesFullBuild());
	// Boundary fragments smaller than a grid cell retain their actual clipped coverage.
	const int32 Base = Data.Vertices.Num();
	Data.Vertices.Append({FVector3f(101.0f, 101.0f, 0.0f), FVector3f(102.0f, 101.0f, 0.0f), FVector3f(101.0f, 102.0f, 0.0f)});
	Data.Indices.Append({Base, Base + 1, Base + 2}); Data.TriangleLayerIds.Add(Layers[0].LayerId);
	Data.TriangleShapeIds.SetNum(Data.TriangleLayerIds.Num());
	UpdateAuthoringVisualization(Data, Layers, FBox2D(FVector2D(101.0, 101.0), FVector2D(102.0, 102.0)), Incremental);
	TestTrue(TEXT("Sub-cell triangle coverage matches a full build"), MatchesFullBuild());
	AddMeshVisualizationTriangle(Data, Layers[0].LayerId, 0.0f, FVector2D(10000.0, 10000.0));
	Data.TriangleShapeIds.SetNum(Data.TriangleLayerIds.Num());
	UpdateAuthoringVisualization(Data, Layers, FBox2D(FVector2D(10000.0, 10000.0), FVector2D(10100.0, 10100.0)), Incremental, &Stats);
	TestTrue(TEXT("Substantial document growth regrids instead of growing an unbounded cache"), Stats.bFullRebuild);
	TestTrue(TEXT("Growth fallback matches ordinary full build"), MatchesFullBuild());
	UpdateAuthoringVisualization(Data, Layers, FBox2D(ForceInit), Incremental, &Stats);
	TestTrue(TEXT("Invalid regional bounds request a full rebuild"), Stats.bFullRebuild);
	TestTrue(TEXT("Full fallback retains distant coverage without clipping to inactive dirty bounds"), MatchesFullBuild());
	Data.Reset();
	UpdateAuthoringVisualization(Data, Layers, ChangedBounds, Incremental);
	TestTrue(TEXT("Erase-to-empty clears cells and their lookup together"), Incremental.Cells.IsEmpty() && Incremental.CellsByGrid.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraMeshEraseLocalVisualizationTest,
	"ComposableCameraSystem.Editor.MeshCamera.EraseLocalVisualization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshEraseLocalVisualizationTest::RunTest(const FString&)
{
	using namespace UE::ComposableCamera::MeshEditor;
	TArray<FComposableCameraMeshLayerDefinition> Layers;
	Layers.SetNum(2);
	Layers[0].LayerId = FGuid::NewGuid(); Layers[1].LayerId = FGuid::NewGuid();
	FComposableCameraMeshSurfaceAuthoringData Data;
	AddExactVisualizationTriangle(Data, Layers[1].LayerId, FVector(0, 0, 0), FVector(1000, 0, 0), FVector(0, 1000, 0));
	AddExactVisualizationTriangle(Data, Layers[0].LayerId, FVector(0, 0, 0), FVector(1000, 0, 0), FVector(0, 1000, 0));
	FResolvedSurfaceVisualization Visualization;
	BuildAuthoringVisualization(Data, Layers, Visualization);
	const FIntPoint RemoteGrid(50, 10);
	const auto RemoteBefore = Visualization.Cells[Visualization.CellsByGrid.FindChecked(RemoteGrid)[0]];
	TestTrue(TEXT("Fixture has thousands of cells outside a small eraser"), Visualization.Cells.Num() > 1000);
	FComposableCameraMeshEraseStamp Stamp;
	Stamp.Center = FVector(205, 205, 0); Stamp.Radius = 2.0; Stamp.Depth = 10.0;
	FBox2D Bounds(ForceInit);
	TestTrue(TEXT("Small eraser cuts a large source triangle"), EraseShapeGeometry(Data, Layers[0].LayerId, Stamp, false, nullptr, &Bounds));
	TestTrue(TEXT("Dirty bounds stay inside the actual small cut"), Bounds.bIsValid
		&& Bounds.Min.X >= 203.0 - 1.e-4 && Bounds.Max.X <= 207.0 + 1.e-4
		&& Bounds.Min.Y >= 203.0 - 1.e-4 && Bounds.Max.Y <= 207.0 + 1.e-4);
	FVisualizationUpdateStats Stats;
	UpdateAuthoringVisualization(Data, Layers, Bounds, Visualization, &Stats);
	TestFalse(TEXT("Local cut avoids full rasterization"), Stats.bFullRebuild);
	TestTrue(TEXT("Cell work is bounded by the cut rather than the large source triangle"), Stats.CellTests < 100);
	const auto* RemoteIndices = Visualization.CellsByGrid.Find(RemoteGrid);
	if (TestNotNull(TEXT("Remote cache remains indexed"), RemoteIndices) && !RemoteIndices->IsEmpty())
	{
		const auto& RemoteAfter = Visualization.Cells[(*RemoteIndices)[0]];
		TestTrue(TEXT("Remote coverage, surface and Layer remain unchanged"), SameVisualizationCoverage(RemoteBefore, RemoteAfter)
			&& RemoteBefore.LocalPosition.Equals(RemoteAfter.LocalPosition) && RemoteBefore.LayerIndex == RemoteAfter.LayerIndex);
	}
	TArray<FResolvedSurfaceLayerMesh> Meshes;
	BuildVisualizationMeshes(Visualization, Layers, Meshes);
	const auto* Upper = Meshes.FindByPredicate([](const auto& Mesh) { return Mesh.LayerIndex == 0; });
	const auto* Lower = Meshes.FindByPredicate([](const auto& Mesh) { return Mesh.LayerIndex == 1; });
	if (TestNotNull(TEXT("Upper Layer remains visible"), Upper) && TestNotNull(TEXT("Lower Layer appears in the tiny cut"), Lower))
	{
		const double CutArea = 16.0 * FMath::Square(Stamp.Radius) * FMath::Sin(2.0 * UE_DOUBLE_PI / 32.0);
		TestTrue(TEXT("Upper area is source area minus the cut"), FMath::IsNearlyEqual(VisualizationMeshArea(*Upper), 500000.0 - CutArea, 0.01));
		TestTrue(TEXT("Only cut coverage exposes the lower Layer"), FMath::IsNearlyEqual(VisualizationMeshArea(*Lower), CutArea, 0.01));
		TestFalse(TEXT("Tiny hole stays empty in upper preview"), VisualizationCovers(*Upper, Stamp.Center));
		TestTrue(TEXT("Tiny hole reveals lower preview"), VisualizationCovers(*Lower, Stamp.Center));
	}
	TestTrue(TEXT("Legacy optional Shape ownership remains consistent after in-place cuts"), Data.IsConsistent() && Data.TriangleShapeIds.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraMeshDisjointPreviewPatchesTest,
	"ComposableCameraSystem.Editor.MeshCamera.DisjointPreviewPatches",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshDisjointPreviewPatchesTest::RunTest(const FString&)
{
	using namespace UE::ComposableCamera::MeshEditor;
	TArray<FComposableCameraMeshLayerDefinition> Layers;
	Layers.AddDefaulted(); Layers[0].LayerId = FGuid::NewGuid();
	FComposableCameraMeshSurfaceAuthoringData Data;
	AddExactVisualizationTriangle(Data, Layers[0].LayerId, FVector(1, 1, 0), FVector(2, 1, 0), FVector(1, 2, 0));
	for (int32 Index = 0; Index < 8; ++Index)
	{
		// Disjoint triangle crosses an earlier triangle's supporting edge line.
		AddExactVisualizationTriangle(Data, Layers[0].LayerId, FVector(4, 0.5, 0), FVector(5, 0.5, 0), FVector(4, 1.5, 0));
	}
	FResolvedSurfaceVisualization Visualization;
	BuildAuthoringVisualization(Data, Layers, Visualization);
	TestEqual(TEXT("Fixture stays inside one cache cell"), Visualization.Cells.Num(), 1);
	if (Visualization.Cells.Num() == 1)
	{
		TestEqual(TEXT("Disjoint footprints and repeats create only two original patches"), Visualization.Cells[0].Patches.Num(), 2);
		for (const auto& Patch : Visualization.Cells[0].Patches)
		{
			TestEqual(TEXT("Separation preserves each triangle without extra fragments"), Patch.LocalVertices.Num(), 3);
		}
	}
	TArray<FResolvedSurfaceLayerMesh> Meshes;
	BuildVisualizationMeshes(Visualization, Layers, Meshes);
	TestTrue(TEXT("Disjoint union emits exact area once"), Meshes.Num() == 1 && FMath::IsNearlyEqual(VisualizationMeshArea(Meshes[0]), 1.0, 1.e-6));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraMeshLayerVisualizationTest,
	"ComposableCameraSystem.Editor.MeshCamera.ResolvedVisualization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshLayerVisualizationTest::RunTest(const FString& /*Parameters*/)
{
	TArray<FComposableCameraMeshLayerDefinition> Layers;
	Layers.SetNum(2);
	Layers[0].LayerId = FGuid::NewGuid();
	Layers[1].LayerId = FGuid::NewGuid();

	FComposableCameraMeshSurfaceAuthoringData SingleLayerData;
	AddMeshVisualizationTriangle(SingleLayerData, Layers[0].LayerId);
	UE::ComposableCamera::MeshEditor::FResolvedSurfaceVisualization SingleVisualization;
	UE::ComposableCamera::MeshEditor::BuildAuthoringVisualization(
		SingleLayerData,
		Layers,
		SingleVisualization);
	TestTrue(TEXT("Single painted triangle produces visible cells"),
		!SingleVisualization.Cells.IsEmpty());

	FComposableCameraMeshSurfaceAuthoringData DuplicateData = SingleLayerData;
	AddMeshVisualizationTriangle(DuplicateData, Layers[0].LayerId);
	UE::ComposableCamera::MeshEditor::FResolvedSurfaceVisualization DuplicateVisualization;
	UE::ComposableCamera::MeshEditor::BuildAuthoringVisualization(
		DuplicateData,
		Layers,
		DuplicateVisualization);
	TestEqual(TEXT("Repeated paint resolves to one visual cell per surface location"),
		DuplicateVisualization.Cells.Num(),
		SingleVisualization.Cells.Num());
	TestTrue(TEXT("Repeated paint does not change visualization resolution"),
		FMath::IsNearlyEqual(DuplicateVisualization.CellSize, SingleVisualization.CellSize));

	FComposableCameraMeshSurfaceAuthoringData OverlapData;
	AddMeshVisualizationTriangle(OverlapData, Layers[1].LayerId);
	AddMeshVisualizationTriangle(OverlapData, Layers[0].LayerId);
	UE::ComposableCamera::MeshEditor::FResolvedSurfaceVisualization OrderedVisualization;
	UE::ComposableCamera::MeshEditor::BuildAuthoringVisualization(
		OverlapData,
		Layers,
		OrderedVisualization);
	const bool bOnlyTopRowVisible = OrderedVisualization.Cells.ContainsByPredicate(
		[](const UE::ComposableCamera::MeshEditor::FResolvedSurfaceCell& Cell)
		{
			return Cell.LayerIndex == 0;
		}) && !OrderedVisualization.Cells.ContainsByPredicate(
		[](const UE::ComposableCamera::MeshEditor::FResolvedSurfaceCell& Cell)
		{
			return Cell.LayerIndex == 1;
		});
	TestTrue(TEXT("Top Layer row draws above lower rows independent of triangle insertion order"),
		bOnlyTopRowVisible);

	TArray<UE::ComposableCamera::MeshEditor::FResolvedSurfaceLayerMesh>
		OrderedMeshes;
	UE::ComposableCamera::MeshEditor::BuildVisualizationMeshes(
		OrderedVisualization,
		Layers,
		OrderedMeshes);
	TestEqual(TEXT("PIE mesh cache emits only the resolved winning Layer"),
		OrderedMeshes.Num(), 1);
	if (OrderedMeshes.Num() == 1)
	{
		TestEqual(TEXT("PIE mesh cache keeps the top-row Layer index"),
			OrderedMeshes[0].LayerIndex, 0);
		TestTrue(TEXT("Resolved mesh preserves exact triangle area without duplicate opacity"),
			FMath::IsNearlyEqual(VisualizationMeshArea(OrderedMeshes[0]), 5000.0, 1.e-5));
		TestFalse(TEXT("Resolved mesh does not fill an uncovered boundary-cell corner"),
			VisualizationCovers(OrderedMeshes[0], FVector(96.0, 6.0, 0.0)));
	}

	Layers[0].bEnabled = false;
	UE::ComposableCamera::MeshEditor::FResolvedSurfaceVisualization DisabledHighVisualization;
	UE::ComposableCamera::MeshEditor::BuildAuthoringVisualization(
		OverlapData,
		Layers,
		DisabledHighVisualization);
	TestTrue(TEXT("Disabled top row reveals enabled lower row"),
		DisabledHighVisualization.Cells.ContainsByPredicate(
			[](const UE::ComposableCamera::MeshEditor::FResolvedSurfaceCell& Cell)
			{
				return Cell.LayerIndex == 1;
			}));

	Layers[1].bEnabled = false;
	UE::ComposableCamera::MeshEditor::FResolvedSurfaceVisualization DisabledVisualization;
	UE::ComposableCamera::MeshEditor::BuildAuthoringVisualization(
		OverlapData,
		Layers,
		DisabledVisualization);
	TestTrue(TEXT("Disabled Layers produce no visual cells"),
		DisabledVisualization.Cells.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraMeshVisualizationBoundaryTest,
	"ComposableCameraSystem.Editor.MeshCamera.VisualizationBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshVisualizationBoundaryTest::RunTest(const FString&)
{
	using namespace UE::ComposableCamera::MeshEditor;
	TArray<FComposableCameraMeshLayerDefinition> Layers;
	Layers.SetNum(2);
	Layers[0].LayerId = FGuid::NewGuid(); Layers[1].LayerId = FGuid::NewGuid();
	const FVector A(3.0, 7.0, 0.0), B(93.0, 19.0, 9.0), C(17.0, 91.0, 6.0);
	const FVector Normal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
	const double ExpectedArea = FMath::Abs(FVector::CrossProduct(B - A, C - A).Z) * 0.5;
	FComposableCameraMeshSurfaceAuthoringData Data;
	AddExactVisualizationTriangle(Data, Layers[0].LayerId, A, B, C);
	FResolvedSurfaceVisualization Visualization;
	TArray<FResolvedSurfaceLayerMesh> Meshes;
	auto CheckOutline = [&](const TCHAR* Label)
	{
		BuildVisualizationMeshes(Visualization, Layers, Meshes);
		const auto* Mesh = Meshes.FindByPredicate([](const auto& Candidate) { return Candidate.LayerIndex == 0; });
		if (!TestNotNull(Label, Mesh)) { return; }
		TestTrue(TEXT("Clipped preview preserves projected source area"),
			FMath::IsNearlyEqual(VisualizationMeshArea(*Mesh), ExpectedArea, 1.e-5));
		bool bExactBoundary = true;
		for (const FVector& Vertex : Mesh->LocalVertices)
		{
			bExactBoundary &= InProjectedTriangle(Vertex, A, B, C)
				&& FMath::Abs(FVector::DotProduct(Vertex - FVector::UpVector * 1.5 - A, Normal)) < 1.e-5;
		}
		TestTrue(TEXT("Every preview vertex stays inside the sloped source footprint and on its offset plane"), bExactBoundary);
	};
	BuildAuthoringVisualization(Data, Layers, Visualization);
	const double FineCellSize = Visualization.CellSize;
	CheckOutline(TEXT("Oblique triangle has a clipped preview mesh"));
	Swap(Data.Indices[1], Data.Indices[2]);
	BuildAuthoringVisualization(Data, Layers, Visualization);
	CheckOutline(TEXT("Clockwise source winding preserves coverage"));
	FComposableCameraMeshSurfaceRuntimeData Runtime;
	Runtime.Vertices = Data.Vertices; Runtime.Indices = Data.Indices; Runtime.TriangleLayerIndices.Add(0);
	BuildRuntimeVisualization(Runtime, Layers, Visualization);
	CheckOutline(TEXT("Saved runtime preview follows the same clipped boundary"));
	AddMeshVisualizationTriangle(Data, Layers[1].LayerId, 0.0f, FVector2D(10000.0, 10000.0));
	BuildAuthoringVisualization(Data, Layers, Visualization);
	TestTrue(TEXT("Distant coverage coarsens the spatial cache"), Visualization.CellSize > FineCellSize);
	CheckOutline(TEXT("Coarser cells preserve the same actual silhouette"));

	FComposableCameraMeshEraseStamp Stamp;
	Stamp.Center = FVector(30.0, 35.0, 3.0); Stamp.Radius = 2.0; Stamp.Depth = 20.0;
	TestTrue(TEXT("Fixture cuts a hole smaller than a coarse cell"), EraseShapeGeometry(Data, Layers[0].LayerId, Stamp, false));
	BuildAuthoringVisualization(Data, Layers, Visualization);
	BuildVisualizationMeshes(Visualization, Layers, Meshes);
	const auto* CutMesh = Meshes.FindByPredicate([](const auto& Mesh) { return Mesh.LayerIndex == 0; });
	if (TestNotNull(TEXT("Erased source remains visible"), CutMesh))
	{
		const double CutArea = 16.0 * FMath::Square(Stamp.Radius) * FMath::Sin(2.0 * UE_DOUBLE_PI / 32.0);
		TestTrue(TEXT("Sub-cell erase keeps exact source-minus-prism area"),
			FMath::IsNearlyEqual(VisualizationMeshArea(*CutMesh), ExpectedArea - CutArea, 1.e-3));
		TestFalse(TEXT("Sub-cell hole stays empty in preview"), VisualizationCovers(*CutMesh, Stamp.Center));
		TestTrue(TEXT("Coverage immediately outside the small hole remains visible"),
			VisualizationCovers(*CutMesh, Stamp.Center + FVector(3.0, 0.0, 0.0)));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraMeshVisualizationPartialOverlapTest,
	"ComposableCameraSystem.Editor.MeshCamera.VisualizationPartialOverlap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshVisualizationPartialOverlapTest::RunTest(const FString&)
{
	using namespace UE::ComposableCamera::MeshEditor;
	TArray<FComposableCameraMeshLayerDefinition> Layers;
	Layers.SetNum(2);
	Layers[0].LayerId = FGuid::NewGuid(); Layers[1].LayerId = FGuid::NewGuid();
	auto AddLower = [&](FComposableCameraMeshSurfaceAuthoringData& Data)
	{
		AddExactVisualizationTriangle(Data, Layers[1].LayerId, FVector(0, 0, 0), FVector(20, 0, 0), FVector(20, 20, 0));
		AddExactVisualizationTriangle(Data, Layers[1].LayerId, FVector(0, 0, 0), FVector(20, 20, 0), FVector(0, 20, 0));
	};
	auto AddUpper = [&](FComposableCameraMeshSurfaceAuthoringData& Data)
	{
		AddExactVisualizationTriangle(Data, Layers[0].LayerId, FVector(1, 1, 0), FVector(9, 1, 0), FVector(1, 9, 0));
	};
	FComposableCameraMeshSurfaceAuthoringData Data;
	AddLower(Data); AddUpper(Data); AddUpper(Data); // Repeated stamps must not increase opacity.
	auto CheckOverlap = [&](const TCHAR* Label)
	{
		FResolvedSurfaceVisualization Visualization;
		BuildAuthoringVisualization(Data, Layers, Visualization);
		TArray<FResolvedSurfaceLayerMesh> Meshes;
		BuildVisualizationMeshes(Visualization, Layers, Meshes);
		TestEqual(Label, Meshes.Num(), 2);
		const auto* Upper = Meshes.FindByPredicate([](const auto& Mesh) { return Mesh.LayerIndex == 0; });
		const auto* Lower = Meshes.FindByPredicate([](const auto& Mesh) { return Mesh.LayerIndex == 1; });
		if (!TestNotNull(TEXT("Upper partial coverage exists"), Upper)
			|| !TestNotNull(TEXT("Lower coverage in the same cell exists"), Lower)) { return; }
		TestTrue(TEXT("Repeated upper coverage has area 32, without alpha stacking"),
			FMath::IsNearlyEqual(VisualizationMeshArea(*Upper), 32.0, 1.e-5));
		TestTrue(TEXT("Lower Layer keeps only its area outside the upper footprint"),
			FMath::IsNearlyEqual(VisualizationMeshArea(*Lower), 368.0, 1.e-5));
		TestTrue(TEXT("Top row owns overlapping interior"), VisualizationCovers(*Upper, FVector(2, 2, 0)));
		TestFalse(TEXT("Lower row cannot accumulate color below upper coverage"), VisualizationCovers(*Lower, FVector(2, 2, 0)));
		TestFalse(TEXT("Upper row does not fill the entire boundary cell"), VisualizationCovers(*Upper, FVector(8, 8, 0)));
		TestTrue(TEXT("Uncovered corner of the same cell reveals lower row"), VisualizationCovers(*Lower, FVector(8, 8, 0)));
	};
	CheckOverlap(TEXT("Both Layers remain visible within one cell"));
	Data.Reset(); AddUpper(Data); AddLower(Data); AddUpper(Data);
	CheckOverlap(TEXT("Layer priority does not depend on source insertion order"));
	Layers[0].bEnabled = false;
	FResolvedSurfaceVisualization Disabled;
	BuildAuthoringVisualization(Data, Layers, Disabled);
	TArray<FResolvedSurfaceLayerMesh> DisabledMeshes;
	BuildVisualizationMeshes(Disabled, Layers, DisabledMeshes);
	TestTrue(TEXT("Disabling upper Layer restores the complete lower square"), DisabledMeshes.Num() == 1
		&& DisabledMeshes[0].LayerIndex == 1 && FMath::IsNearlyEqual(VisualizationMeshArea(DisabledMeshes[0]), 400.0, 1.e-5));
	Layers[0].bEnabled = true;
	Data.Reset(); AddLower(Data); AddUpper(Data);
	AddExactVisualizationTriangle(Data, Layers[0].LayerId, FVector(4, 1, 0), FVector(12, 1, 0), FVector(4, 9, 0));
	BuildAuthoringVisualization(Data, Layers, Disabled);
	BuildVisualizationMeshes(Disabled, Layers, DisabledMeshes);
	const auto* UnionMesh = DisabledMeshes.FindByPredicate([](const auto& Mesh) { return Mesh.LayerIndex == 0; });
	const auto* RemainderMesh = DisabledMeshes.FindByPredicate([](const auto& Mesh) { return Mesh.LayerIndex == 1; });
	if (TestNotNull(TEXT("Partly overlapping same-Layer stamps have a union mesh"), UnionMesh)
		&& TestNotNull(TEXT("Lower Layer surrounds the stamp union"), RemainderMesh))
	{
		TestTrue(TEXT("Partial same-Layer overlap emits the union area once"),
			FMath::IsNearlyEqual(VisualizationMeshArea(*UnionMesh), 51.5, 1.e-5));
		TestTrue(TEXT("Layer partition conserves source area across a partial stamp union"),
			FMath::IsNearlyEqual(VisualizationMeshArea(*RemainderMesh), 348.5, 1.e-5));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraMeshVisualizationUnevenSurfaceTest,
	"ComposableCameraSystem.Editor.MeshCamera.VisualizationUnevenSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshVisualizationUnevenSurfaceTest::RunTest(const FString&)
{
	using namespace UE::ComposableCamera::MeshEditor;
	TArray<FComposableCameraMeshLayerDefinition> Layers;
	Layers.SetNum(2);
	Layers[0].LayerId = FGuid::NewGuid(); Layers[1].LayerId = FGuid::NewGuid();
	FComposableCameraMeshSurfaceAuthoringData Data;
	auto AddFlat = [&](int32 Layer)
	{
		AddExactVisualizationTriangle(Data, Layers[Layer].LayerId, FVector(1, 1, 0), FVector(3, 1, 0), FVector(1, 3, 0));
	};
	auto AddSlope = [&](int32 Layer)
	{
		// Height difference is 0..4 inside the footprint, but 8 at grid center.
		AddExactVisualizationTriangle(Data, Layers[Layer].LayerId, FVector(1, 1, 0), FVector(3, 1, 4), FVector(1, 3, 0));
	};
	FResolvedSurfaceVisualization Visualization;
	TArray<FResolvedSurfaceLayerMesh> Meshes;
	auto Build = [&]() { BuildAuthoringVisualization(Data, Layers, Visualization); BuildVisualizationMeshes(Visualization, Layers, Meshes); };
	AddFlat(0); AddSlope(0); AddSlope(0);
	Build();
	TestTrue(TEXT("Uneven repeated stamps emit their footprint once despite extrapolated grid-center heights"),
		Meshes.Num() == 1 && FMath::IsNearlyEqual(VisualizationMeshArea(Meshes[0]), 2.0, 1.e-5));
	for (int32 Order = 0; Order < 2; ++Order)
	{
		Data.Reset();
		if (Order == 0) { AddSlope(1); AddFlat(0); } else { AddFlat(0); AddSlope(1); }
		Build();
		TestTrue(TEXT("Actual overlap heights preserve top-row ownership in either insertion order"),
			Meshes.Num() == 1 && Meshes[0].LayerIndex == 0 && FMath::IsNearlyEqual(VisualizationMeshArea(Meshes[0]), 2.0, 1.e-5));
	}
	FComposableCameraMeshSurfaceRuntimeData UnevenRuntime;
	UnevenRuntime.Vertices = Data.Vertices; UnevenRuntime.Indices = Data.Indices;
	for (const FGuid& Layer : Data.TriangleLayerIds) { UnevenRuntime.TriangleLayerIndices.Add(Layer == Layers[0].LayerId ? 0 : 1); }
	BuildRuntimeVisualization(UnevenRuntime, Layers, Visualization); BuildVisualizationMeshes(Visualization, Layers, Meshes);
	TestTrue(TEXT("Saved/PIE preview removes the same uneven-surface duplicate as authoring preview"),
		Meshes.Num() == 1 && Meshes[0].LayerIndex == 0 && FMath::IsNearlyEqual(VisualizationMeshArea(Meshes[0]), 2.0, 1.e-5));
	// Coarsening must not extrapolate a tiny patch's plane to decide ownership.
	AddMeshVisualizationTriangle(Data, Layers[1].LayerId, 100.0f, FVector2D(10000, 10000));
	Build();
	const auto* CoarseLower = Meshes.FindByPredicate([](const auto& Mesh) { return Mesh.LayerIndex == 1; });
	TestTrue(TEXT("Coarse index retains remote coverage without restoring local alpha overlap"),
		Visualization.CellSize > 10.0 && CoarseLower && !VisualizationCovers(*CoarseLower, FVector(1.5, 1.5, 0)));
	Data.Reset();
	AddExactVisualizationTriangle(Data, Layers[0].LayerId, FVector(0, 0, 0), FVector(10, 0, 0), FVector(0, 10, 0));
	AddExactVisualizationTriangle(Data, Layers[1].LayerId, FVector(0, 0, 0), FVector(10, 0, 10), FVector(0, 10, 0));
	Build();
	const auto* Lower = Meshes.FindByPredicate([](const auto& Mesh) { return Mesh.LayerIndex == 1; });
	TestTrue(TEXT("Height band crossing inside one cell retains separated coverage instead of discarding a whole patch"),
		Lower && FMath::IsNearlyEqual(VisualizationMeshArea(*Lower), 12.5, 1.e-5)
		&& !VisualizationCovers(*Lower, FVector(2, 2, 0)) && VisualizationCovers(*Lower, FVector(7, 1, 0)));
	Data.Reset();
	for (double Height : {0.0, 100.0})
	{
		AddExactVisualizationTriangle(Data, Layers[0].LayerId, FVector(0, 0, Height), FVector(10, 0, Height), FVector(0, 10, Height));
	}
	Build();
	TestTrue(TEXT("Distinct stacked floors retain both complete footprints"),
		Meshes.Num() == 1 && FMath::IsNearlyEqual(VisualizationMeshArea(Meshes[0]), 100.0, 1.e-5));
	FComposableCameraMeshSurfaceRuntimeData Runtime;
	Runtime.Vertices = Data.Vertices; Runtime.Indices = Data.Indices;
	Runtime.TriangleLayerIndices.Init(0, Data.TriangleLayerIds.Num());
	BuildRuntimeVisualization(Runtime, Layers, Visualization); BuildVisualizationMeshes(Visualization, Layers, Meshes);
	TestTrue(TEXT("PIE/runtime-data preview uses the same height-aware coverage resolver"),
		Meshes.Num() == 1 && FMath::IsNearlyEqual(VisualizationMeshArea(Meshes[0]), 100.0, 1.e-5));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraMeshLayerPIESurfaceProjectionTest,
	"ComposableCameraSystem.Editor.MeshCamera.PIEPreviewSurfaceProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshLayerPIESurfaceProjectionTest::RunTest(const FString&)
{
	using namespace UE::ComposableCamera::MeshEditor;
	TArray<FResolvedSurfaceLayerMesh> Source;
	Source.SetNum(2);
	Source[0].LayerIndex = 0; Source[0].Color = FColor::Red;
	Source[0].LocalVertices = {FVector(0, 0, VisualizationSurfaceOffset), FVector(10, 0, VisualizationSurfaceOffset),
		FVector(10, 10, VisualizationSurfaceOffset), FVector(0, 10, VisualizationSurfaceOffset), FVector(0, 0, VisualizationSurfaceOffset)};
	Source[0].Indices = {0, 1, 2, 0, 2, 3};
	Source[1].LayerIndex = 1; Source[1].Color = FColor::Green;
	Source[1].LocalVertices = {Source[0].LocalVertices[0], FVector(0, 0, 60 + VisualizationSurfaceOffset), Source[0].LocalVertices[2]};
	Source[1].Indices = {0, 1, 2};
	FPIEPreviewSurfaceProjection Projection;
	auto Input = Source;
	Projection.Begin(MoveTemp(Input));
	int32 Calls = 0;
	auto Project = [&Calls](const FVector& Vertex)
	{
		++Calls;
		return Vertex + FVector(5, 5, 8); // Only Z may change; XY owns color boundaries.
	};
	TestFalse(TEXT("Zero query budget defers fitting"), Projection.Advance(Project, 0));
	TestEqual(TEXT("Deferral performs no queries"), Calls, 0);
	for (int32 Step = 0; Step < 8 && !Projection.IsComplete(); ++Step)
	{
		int32 Queries = 0;
		Projection.Advance(Project, 1, 0.0, &Queries);
		TestTrue(TEXT("Each step respects its query budget"), Queries <= 1);
	}
	if (!TestTrue(TEXT("Projection eventually completes"), Projection.IsComplete())) { return false; }
	TestEqual(TEXT("Shared vertices reuse samples, while stacked heights remain distinct"), Calls, 5);
	const auto ReadyLayers = Projection.TakeReadyMeshes(2);
	if (!TestEqual(TEXT("Short completed Layers publish independent chunks"), ReadyLayers.Num(), Source.Num())) { return false; }
	TestEqual(TEXT("Fan triangles reuse remapped vertex IDs"), ReadyLayers[0].LocalVertices.Num(), 4);
	TestTrue(TEXT("Remap insertion and reuse preserve fan topology"), ReadyLayers[0].Indices == Source[0].Indices);
	for (int32 Layer = 0; Layer < ReadyLayers.Num(); ++Layer)
	{
		TestTrue(TEXT("Chunk publication retains each Layer's identity and color"),
			ReadyLayers[Layer].LayerIndex == Source[Layer].LayerIndex && ReadyLayers[Layer].Color == Source[Layer].Color);
	}
	auto Fitted = Projection.TakeMeshes();
	for (int32 Mesh = 0; Mesh < Source.Num(); ++Mesh)
	{
		TestTrue(TEXT("Fitting retains Layer identity, color and triangle topology"),
			Fitted[Mesh].LayerIndex == Source[Mesh].LayerIndex && Fitted[Mesh].Color == Source[Mesh].Color
			&& Fitted[Mesh].Indices == Source[Mesh].Indices);
		for (int32 Vertex = 0; Vertex < Source[Mesh].LocalVertices.Num(); ++Vertex)
		{
			TestTrue(TEXT("Fitting preserves XY edges and keeps the surface offset"),
				Fitted[Mesh].LocalVertices[Vertex].Equals(Source[Mesh].LocalVertices[Vertex] + FVector(0, 0, 8)));
		}
	}
	Input = Source;
	Projection.Begin(MoveTemp(Input));
	Projection.Advance(Project, 1);
	Projection.Reset();
	const int32 CallsBeforeCancel = Calls;
	TestTrue(TEXT("Cancelled fitting has no remaining work"), Projection.Advance(Project, 1));
	TestTrue(TEXT("Cancellation clears pending geometry and queries"), Projection.GetMeshes().IsEmpty() && Calls == CallsBeforeCancel);

	UWorld* World = UWorld::CreateWorld(EWorldType::PIE, false);
	GEngine->CreateNewWorldContext(EWorldType::PIE).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	const bool bResult = [this, World]()
	{
		auto AddFloorBox = [World](AActor* Actor, double Height,
			ECollisionChannel ObjectType = ECC_WorldStatic, double X = 0.0)
		{
			UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
			Box->InitBoxExtent(FVector(100, 100, 1));
			Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Box->SetCollisionObjectType(ObjectType);
			Box->SetCollisionResponseToAllChannels(ECR_Block);
			Actor->AddInstanceComponent(Box);
			Actor->SetRootComponent(Box);
			Actor->SetActorLocation(FVector(X, 0, Height - 1));
			Box->RegisterComponent();
			return Box;
		};
		AActor* LowerFloor = World->SpawnActor<AActor>();
		AActor* UpperFloor = World->SpawnActor<AActor>();
		APawn* Pawn = World->SpawnActor<APawn>();
		AActor* DynamicFloor = World->SpawnActor<AActor>();
		AActor* PhysicsFloor = World->SpawnActor<AActor>();
		AActor* IgnoredFloor = World->SpawnActor<AActor>();
		AActor* OverlapFloor = World->SpawnActor<AActor>();
		if (!TestNotNull(TEXT("Lower floor exists"), LowerFloor)
			|| !TestNotNull(TEXT("Upper floor exists"), UpperFloor)
			|| !TestNotNull(TEXT("Pawn exists"), Pawn)
			|| !TestNotNull(TEXT("WorldDynamic floor exists"), DynamicFloor)
			|| !TestNotNull(TEXT("PhysicsBody floor exists"), PhysicsFloor)
			|| !TestNotNull(TEXT("Visibility-ignoring surface exists"), IgnoredFloor)
			|| !TestNotNull(TEXT("Visibility-overlapping surface exists"), OverlapFloor)) { return false; }
		AddFloorBox(LowerFloor, 8);
		AddFloorBox(UpperFloor, 60);
		// Even an unusual WorldStatic Pawn must not become the projected floor.
		AddFloorBox(Pawn, 4);
		AddFloorBox(DynamicFloor, 12, ECC_WorldDynamic, 300);
		AddFloorBox(PhysicsFloor, 16, ECC_PhysicsBody, 600);
		// Closer query-only surfaces cannot replace the Visibility-blocking ground.
		AddFloorBox(IgnoredFloor, 2)->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
		AddFloorBox(OverlapFloor, 1, ECC_WorldStatic, 300)->SetCollisionResponseToChannel(ECC_Visibility, ECR_Overlap);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CCSMeshLayerPIEProjectionTest), false);
		TArray<FHitResult> Hits;
		FPIEPreviewProjectionStats Stats;
		const FVector Lower = ProjectPIEPreviewVertex(*World, FTransform::Identity,
			FVector(0, 0, VisualizationSurfaceOffset), Params, Hits, &Stats);
		TestTrue(TEXT("Nearest actual floor lifts buried source without snapping to a Pawn or higher floor"),
			Lower.Equals(FVector(0, 0, 8 + VisualizationSurfaceOffset), 1.e-4));
		TestTrue(TEXT("Visibility-blocking WorldDynamic floor is eligible, while closer overlaps are ignored"),
			ProjectPIEPreviewVertex(*World, FTransform::Identity, FVector(300, 0, VisualizationSurfaceOffset), Params, Hits, &Stats)
				.Equals(FVector(300, 0, 12 + VisualizationSurfaceOffset), 1.e-4));
		TestTrue(TEXT("Visibility-blocking PhysicsBody floor is eligible"),
			ProjectPIEPreviewVertex(*World, FTransform::Identity, FVector(600, 0, VisualizationSurfaceOffset), Params, Hits, &Stats)
				.Equals(FVector(600, 0, 16 + VisualizationSurfaceOffset), 1.e-4));
		const FVector Upper = ProjectPIEPreviewVertex(*World, FTransform::Identity, FVector(0, 0, 60 + VisualizationSurfaceOffset), Params, Hits);
		TestTrue(TEXT("Stacked floor retains its own height"), Upper.Equals(FVector(0, 0, 60 + VisualizationSurfaceOffset), 1.e-4));
		const FVector Missing(1500, 0, VisualizationSurfaceOffset);
		TestTrue(TEXT("Unsupported coverage retains original geometry"),
			ProjectPIEPreviewVertex(*World, FTransform::Identity, Missing, Params, Hits, &Stats).Equals(Missing));
		TestEqual(TEXT("Projection diagnostics count actual queries"), Stats.Queries, 4);
		TestEqual(TEXT("Projection diagnostics count only unsupported samples as misses"), Stats.Misses, 1);
		TestEqual(TEXT("Projection diagnostics identify accepted non-static floors"), Stats.NonStaticFloorHits, 2);
		TestTrue(TEXT("Floor correction range includes zero and the largest accepted world-space lift"),
			FMath::IsNearlyZero(Stats.MinFloorAdjustment) && FMath::IsNearlyEqual(Stats.MaxFloorAdjustment, 16.0, 1.e-4));
		// LevelBlock-like fixture: an ordinary Actor with a separate root and child StaticMesh.
		// No Construction Script or BeginPlay transform/asset changes.
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		AActor* MeshFloor = World->SpawnActor<AActor>();
		if (!TestNotNull(TEXT("Engine cube fixture loads"), Cube)
			|| !TestNotNull(TEXT("Ordinary child-mesh Actor exists"), MeshFloor)) { return false; }
		USceneComponent* Root = NewObject<USceneComponent>(MeshFloor);
		Root->SetMobility(EComponentMobility::Movable);
		MeshFloor->AddInstanceComponent(Root);
		MeshFloor->SetRootComponent(Root);
		MeshFloor->SetActorLocation(FVector(1900, 0, 0));
		Root->RegisterComponent();
		UStaticMeshComponent* Child = NewObject<UStaticMeshComponent>(MeshFloor);
		MeshFloor->AddInstanceComponent(Child);
		Child->SetupAttachment(Root);
		Child->SetMobility(EComponentMobility::Movable);
		Child->SetStaticMesh(Cube);
		Child->SetRelativeScale3D(FVector(4, 4, 0.1));
		Child->SetRelativeLocation(FVector(0, 0, 24.0 - Cube->GetBoundingBox().Max.Z * 0.1));
		Child->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Child->SetCollisionObjectType(ECC_WorldStatic);
		Child->SetCollisionResponseToAllChannels(ECR_Block);
		Child->RegisterComponent();
		FCollisionQueryParams ComplexParams(SCENE_QUERY_STAT(CCSMeshLayerPIEChildMeshTest), true);
		FPIEPreviewProjectionStats ChildStats;
		const FVector ChildFitted = ProjectPIEPreviewVertex(*World, FTransform::Identity,
			FVector(1900, 0, VisualizationSurfaceOffset), ComplexParams, Hits, &ChildStats);
		TestTrue(TEXT("Complex floor query fits a StaticMesh child of an ordinary Actor"),
			ChildFitted.Equals(FVector(1900, 0, 24 + VisualizationSurfaceOffset), 1.e-4));
		TestEqual(TEXT("Child StaticMesh floor is not a projection miss"), ChildStats.Misses, 0);
		TestTrue(TEXT("Child mesh collision participates without changing the actor transform"),
			Hits.ContainsByPredicate([Child](const FHitResult& Hit) { return Hit.GetComponent() == Child; })
			&& MeshFloor->GetActorLocation().Equals(FVector(1900, 0, 0)));
		const FTransform Scaled(FRotator(0, 37, 0), FVector::ZeroVector, FVector(2, 0.5, 2));
		TestTrue(TEXT("Projection respects anchor scale and applies the offset exactly once"),
			ProjectPIEPreviewVertex(*World, Scaled, FVector(0, 0, VisualizationSurfaceOffset), Params, Hits)
				.Equals(FVector(0, 0, 4 + VisualizationSurfaceOffset), 1.e-4));
		return true;
	}();
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return bResult;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraMeshLayerPIEOccludingFloorTest,
	"ComposableCameraSystem.Editor.MeshCamera.PIEPreviewOccludingFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshLayerPIEOccludingFloorTest::RunTest(const FString&)
{
	using namespace UE::ComposableCamera::MeshEditor;
	UWorld* World = UWorld::CreateWorld(EWorldType::PIE, false);
	GEngine->CreateNewWorldContext(EWorldType::PIE).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	const bool bResult = [this, World]()
	{
		constexpr double NativeHeight = -1.992;
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		AActor* Ground = World->SpawnActor<AActor>();
		AComposableCameraMeshSurfaceStorageActor* Storage = World->SpawnActor<AComposableCameraMeshSurfaceStorageActor>();
		if (!TestNotNull(TEXT("Occluding floor mesh loads"), Cube) || !TestNotNull(TEXT("Underlying ground exists"), Ground)
			|| !TestNotNull(TEXT("Occluding floor storage exists"), Storage)) { return false; }
		UBoxComponent* GroundBox = NewObject<UBoxComponent>(Ground);
		GroundBox->InitBoxExtent(FVector(256, 256, 1));
		GroundBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		GroundBox->SetCollisionObjectType(ECC_WorldStatic);
		GroundBox->SetCollisionResponseToAllChannels(ECR_Block);
		Ground->AddInstanceComponent(GroundBox);
		Ground->SetRootComponent(GroundBox);
		Ground->SetActorLocation(FVector(0, 0, NativeHeight - 1));
		GroundBox->RegisterComponent();
		auto AddMeshFloor = [&](double Height, ECollisionResponse Visibility)
		{
			AActor* Actor = World->SpawnActor<AActor>();
			if (!Actor) { return static_cast<UStaticMeshComponent*>(nullptr); }
			USceneComponent* Root = NewObject<USceneComponent>(Actor);
			Root->SetMobility(EComponentMobility::Movable);
			Actor->AddInstanceComponent(Root);
			Actor->SetRootComponent(Root);
			Root->RegisterComponent();
			UStaticMeshComponent* Child = NewObject<UStaticMeshComponent>(Actor);
			Actor->AddInstanceComponent(Child);
			Child->SetupAttachment(Root);
			Child->SetMobility(EComponentMobility::Movable);
			Child->SetStaticMesh(Cube);
			Child->SetMaterial(0, UMaterial::GetDefaultMaterial(MD_Surface));
			Child->SetRelativeScale3D(FVector(4, 4, 0.1));
			Child->SetRelativeLocation(FVector(0, 0, Height - Cube->GetBoundingBox().Max.Z * 0.1));
			Child->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Child->SetCollisionObjectType(ECC_WorldStatic);
			Child->SetCollisionResponseToAllChannels(ECR_Block);
			Child->SetCollisionResponseToChannel(ECC_Visibility, Visibility);
			Child->RegisterComponent();
			return Child;
		};
		UStaticMeshComponent* Slab = AddMeshFloor(0.0, ECR_Ignore);
		UStaticMeshComponent* UpperStorey = AddMeshFloor(60.0, ECR_Block);
		if (!TestNotNull(TEXT("Visibility-ignoring rendered slab exists"), Slab)
			|| !TestNotNull(TEXT("Distinct upper storey exists"), UpperStorey)) { return false; }
		if (!TestNotNull(TEXT("Opaque fixture material exists"), Slab->GetMaterial(0))) { return false; }
		TestTrue(TEXT("Slab fixture renders opaque despite ignoring Visibility"),
			Slab->ShouldRender() && Slab->GetMaterial(0)->GetBlendMode() == BLEND_Opaque
			&& Slab->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Ignore);

		TArray<FComposableCameraMeshLayerDefinition> Layers;
		Layers.AddDefaulted(); Layers[0].LayerId = FGuid::NewGuid();
		FComposableCameraMeshSurfaceAuthoringData Data;
		AddMeshVisualizationTriangle(Data, Layers[0].LayerId, static_cast<float>(NativeHeight));
		Storage->SetAuthoringData(Layers, Data);
		TArray<FResolvedSurfaceLayerMesh> Source;
		Source.AddDefaulted(); Source[0].LayerIndex = 0; Source[0].Color = FColor::Red;
		Source[0].LocalVertices = {FVector(0, 0, NativeHeight + VisualizationSurfaceOffset),
			FVector(100, 0, NativeHeight + VisualizationSurfaceOffset), FVector(0, 100, NativeHeight + VisualizationSurfaceOffset)};
		Source[0].Indices = {0, 1, 2};
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CCSMeshLayerPIEOccludingFloorTest), true);
		TArray<FHitResult> Hits;
		FPIEPreviewProjectionStats Stats;
		FPIEPreviewSurfaceProjection Projection;
		auto Input = Source;
		Projection.Begin(MoveTemp(Input));
		if (!TestTrue(TEXT("Occluding floor fixture completes fitting"), Projection.Advance([&](const FVector& Vertex)
		{
			return ProjectPIEPreviewVertex(*World, FTransform::Identity, Vertex, Params, Hits, &Stats);
		}, 256))) { return false; }
		auto Fitted = Projection.TakeMeshes();
		if (!TestEqual(TEXT("Fitting retains the fixture Layer"), Fitted.Num(), 1)) { return false; }
		for (int32 Index = 0; Index < Fitted[0].LocalVertices.Num(); ++Index)
		{
			TestTrue(TEXT("Fitting clears the nearby slab without jumping to the upper storey"),
				Fitted[0].LocalVertices[Index].Equals(FVector(Source[0].LocalVertices[Index].X,
					Source[0].LocalVertices[Index].Y, VisualizationSurfaceOffset), 1.e-4));
		}
		TestEqual(TEXT("Nearby visible slab does not produce projection misses"), Stats.Misses, 0);
		TestTrue(TEXT("Floor statistics identify the slab lift above authored ground"),
			FMath::IsNearlyEqual(Stats.MaxFloorAdjustment, -NativeHeight, 1.e-4));
		AActor* Preview = CreatePIEPreviewActor(*Storage, Fitted);
		if (!TestNotNull(TEXT("Occluding floor preview exists"), Preview)) { return false; }
		const FVector Foot(23, 21, 2.275);
		const FPIEPreviewPointSample Sample = SamplePIEPreviewActor(*Preview, Foot);
		TestTrue(TEXT("Submitted overlay covers the missing point above the visible slab"),
			Sample.Intersections > 0 && FMath::IsNearlyEqual(Sample.NearestPosition.Z, VisualizationSurfaceOffset, 1.e-4));
		FComposableCameraMeshLayerQueryResults NativeLayers;
		TestTrue(TEXT("Visualization fitting preserves native camera coverage"), Storage->QueryLayers(Foot, 100.0, 1.0, NativeLayers));
		if (TestEqual(TEXT("Native Layer remains queryable"), NativeLayers.Num(), 1))
		{
			TestTrue(TEXT("Native camera surface retains its original Landscape height"),
				FMath::IsNearlyEqual(NativeLayers[0].SurfacePosition.Z, NativeHeight, 1.e-4));
		}
		DestroyPIEPreviewActor(Preview);

		// Actual second report: the same slab is 7.070 cm above the native surface.
		// The former 5 cm promotion band publishes all triangles but leaves them buried.
		constexpr double DeeperNativeHeight = -7.070;
		Ground->SetActorLocation(FVector(0, 0, DeeperNativeHeight - 1));
		FComposableCameraMeshSurfaceAuthoringData DeeperData;
		AddMeshVisualizationTriangle(DeeperData, Layers[0].LayerId, static_cast<float>(DeeperNativeHeight));
		Storage->SetAuthoringData(Layers, DeeperData);
		auto DeeperSource = Source;
		for (FVector& Vertex : DeeperSource[0].LocalVertices) { Vertex.Z = DeeperNativeHeight + VisualizationSurfaceOffset; }
		Input = DeeperSource;
		Projection.Begin(MoveTemp(Input));
		Stats = {};
		if (!TestTrue(TEXT("Deeper occluding floor completes fitting"), Projection.Advance([&](const FVector& Vertex)
		{
			return ProjectPIEPreviewVertex(*World, FTransform::Identity, Vertex, Params, Hits, &Stats);
		}, 256))) { return false; }
		Fitted = Projection.TakeMeshes();
		Preview = CreatePIEPreviewActor(*Storage, Fitted);
		if (!TestNotNull(TEXT("Deeper floor publishes a preview"), Preview)) { return false; }
		const FPIEPreviewPointSample DeeperSample = SamplePIEPreviewActor(*Preview, Foot);
		TestTrue(TEXT("The reported 7.070 cm separation clears the slab in submitted geometry"),
			DeeperSample.Intersections > 0 && FMath::IsNearlyEqual(DeeperSample.NearestPosition.Z, VisualizationSurfaceOffset, 1.e-4));
		TestTrue(TEXT("Deeper floor statistics record the full correction without reaching the 60 cm storey"),
			Stats.Misses == 0 && FMath::IsNearlyEqual(Stats.MaxFloorAdjustment, -DeeperNativeHeight, 1.e-4));
		TestTrue(TEXT("Deeper native Layer remains queryable"), Storage->QueryLayers(Foot, 100.0, 1.0, NativeLayers));
		if (TestEqual(TEXT("Deeper native Layer is retained"), NativeLayers.Num(), 1))
		{
			TestTrue(TEXT("Fitting never lifts the actual camera query surface"),
				FMath::IsNearlyEqual(NativeLayers[0].SurfacePosition.Z, DeeperNativeHeight, 1.e-4));
		}
		DestroyPIEPreviewActor(Preview);
		Ground->SetActorLocation(FVector(0, 0, NativeHeight - 1));
		Storage->SetAuthoringData(Layers, Data);

		auto FitPoint = [&]() { return ProjectPIEPreviewVertex(*World, FTransform::Identity, Source[0].LocalVertices[0], Params, Hits); };
		Slab->SetHiddenInGame(true);
		TestTrue(TEXT("Hidden Visibility-ignoring slabs do not lift the overlay"), FitPoint().Equals(Source[0].LocalVertices[0], 1.e-4));
		Slab->SetHiddenInGame(false);
		Slab->GetOwner()->SetActorHiddenInGame(true);
		TestTrue(TEXT("Hidden owner excludes its slab from visible occluders"), FitPoint().Equals(Source[0].LocalVertices[0], 1.e-4));
		Slab->GetOwner()->SetActorHiddenInGame(false);
		UMaterial* Translucent = GEngine->GeomMaterial.Get();
		if (!TestNotNull(TEXT("Translucent fixture material exists"), Translucent)) { return false; }
		if (!TestTrue(TEXT("Fixture material does not write opaque floor depth"),
			Translucent->GetBlendMode() != BLEND_Opaque && Translucent->GetBlendMode() != BLEND_Masked)) { return false; }
		Slab->SetMaterial(0, Translucent);
		TestTrue(TEXT("Translucent Visibility-ignoring meshes do not lift the overlay"), FitPoint().Equals(Source[0].LocalVertices[0], 1.e-4));
		Slab->SetMaterial(0, UMaterial::GetDefaultMaterial(MD_Surface));
		Slab->SetRelativeLocation(FVector(0, 0, 11.0 - Cube->GetBoundingBox().Max.Z * 0.1));
		TestTrue(TEXT("Occluder promotion stays inside a fixed height band"), FitPoint().Equals(Source[0].LocalVertices[0], 1.e-4));
		GroundBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Params.AddIgnoredActor(Ground);
		TestTrue(TEXT("A standalone visible Visibility-ignoring floor is still eligible"),
			FitPoint().Equals(FVector(0, 0, 11.0 + VisualizationSurfaceOffset), 1.e-4));
		return true;
	}();
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return bResult;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraMeshLayerPIEProgressiveMeshTest,
	"ComposableCameraSystem.Editor.MeshCamera.PIEPreviewProgressiveMeshes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshLayerPIEProgressiveMeshTest::RunTest(const FString&)
{
	using namespace UE::ComposableCamera::MeshEditor;
	UWorld* World = UWorld::CreateWorld(EWorldType::PIE, false);
	GEngine->CreateNewWorldContext(EWorldType::PIE).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	const bool bResult = [this, World]()
	{
		AComposableCameraMeshSurfaceStorageActor* Storage = World->SpawnActor<AComposableCameraMeshSurfaceStorageActor>();
		if (!TestNotNull(TEXT("Progressive preview storage exists"), Storage)) { return false; }
		TArray<FResolvedSurfaceLayerMesh> Source;
		Source.SetNum(1);
		Source[0].LayerIndex = 7;
		Source[0].Color = FColor(80, 120, 180, 100);
		const int32 TriangleCount = PIEPreviewChunkTriangleCount * 3 + 17;
		for (int32 Triangle = 0; Triangle < TriangleCount; ++Triangle)
		{
			const int32 Base = Source[0].LocalVertices.Num();
			const FVector A(Triangle * 4.0, 0, VisualizationSurfaceOffset);
			Source[0].LocalVertices.Append({A, A + FVector(1, 0, 0), A + FVector(0, 1, 0)});
			Source[0].Indices.Append({Base, Base + 1, Base + 2});
		}
		FPIEPreviewSurfaceProjection Projection;
		auto Input = Source;
		Projection.Begin(MoveTemp(Input));
		TestTrue(TEXT("Unfitted document publishes no chunks"), Projection.TakeReadyMeshes(2).IsEmpty());
		TSet<int32> PublishedTriangles;
		PublishedTriangles.Reserve(TriangleCount);
		TWeakObjectPtr<AActor> Preview;
		TWeakObjectPtr<UDynamicMeshComponent> FirstComponent;
		UDynamicMesh* FirstMesh = nullptr;
		bool bVisibleBeforeComplete = false;
		int32 PublishedChunks = 0;
		auto Project = [](const FVector& Vertex) { return Vertex + FVector(0, 0, 4); };
		const int32 MaxSteps = Source[0].LocalVertices.Num() / 256 + 16;
		for (int32 Step = 0; Step < MaxSteps && PublishedTriangles.Num() < TriangleCount; ++Step)
		{
			int32 Queries = 0;
			Projection.Advance(Project, 256, 0.0, &Queries);
			TestTrue(TEXT("Progressive construction retains the shared query limit"), Queries <= 256);
			TestTrue(TEXT("Zero publication budget retains completed chunks for later"), Projection.TakeReadyMeshes(0).IsEmpty());
			auto Chunks = Projection.TakeReadyMeshes(1);
			TestTrue(TEXT("Publication respects its per-call component budget"), Chunks.Num() <= 1);
			if (Chunks.IsEmpty()) { continue; }
			if (PublishedChunks == 0)
			{
				TestEqual(TEXT("The first query batch already publishes geometry in this fixture"), Step, 0);
				TestEqual(TEXT("The first visible chunk stays small"), Chunks[0].Indices.Num(), PIEPreviewFirstChunkTriangleCount * 3);
			}
			const bool bStillFitting = !Projection.IsComplete();
			for (const auto& Chunk : Chunks)
			{
				TestTrue(TEXT("Chunk retains Layer identity/color and bounds its triangle count"),
					Chunk.LayerIndex == Source[0].LayerIndex && Chunk.Color == Source[0].Color
					&& Chunk.Indices.Num() <= PIEPreviewChunkTriangleCount * 3);
				for (int32 Index = 0; Index < Chunk.Indices.Num(); Index += 3)
				{
					const FVector A = Chunk.LocalVertices[Chunk.Indices[Index]];
					const int32 Triangle = FMath::RoundToInt(A.X / 4.0);
					if (!TestTrue(TEXT("Each fitted source triangle is published exactly once"),
						Triangle >= 0 && Triangle < TriangleCount && !PublishedTriangles.Contains(Triangle))) { return false; }
					PublishedTriangles.Add(Triangle);
					for (int32 Corner = 0; Corner < 3; ++Corner)
					{
						TestTrue(TEXT("Chunks expose only fitted positions, retaining original edges and winding"),
							Chunk.LocalVertices[Chunk.Indices[Index + Corner]]
								.Equals(Source[0].LocalVertices[Triangle * 3 + Corner] + FVector(0, 0, 4)));
					}
				}
			}
			AActor* ExistingPreview = Preview.Get();
			Preview = AppendPIEPreviewMeshes(*Storage, ExistingPreview, Chunks);
			if (!TestTrue(TEXT("A completed chunk creates a visible registered preview"), Preview.IsValid())) { return false; }
			if (ExistingPreview) { TestTrue(TEXT("Later chunks reuse the source-Level actor"), ExistingPreview == Preview.Get()); }
			++PublishedChunks;
			TArray<UDynamicMeshComponent*> Components;
			Preview->GetComponents(Components);
			if (!TestEqual(TEXT("Chunks append persistent components without replacing earlier ones"), Components.Num(), PublishedChunks)) { return false; }
			if (!FirstComponent.IsValid())
			{
				FirstComponent = Components[0];
				FirstMesh = Components[0]->GetDynamicMesh();
			}
			TestTrue(TEXT("Published mesh stays registered while later chunks are fitted"),
				FirstComponent->IsRegistered() && FirstComponent->GetDynamicMesh() == FirstMesh);
			bVisibleBeforeComplete |= bStillFitting && FirstComponent->IsRegistered();
		}
		if (!TestTrue(TEXT("PIE has visible geometry before fitting the entire document"), bVisibleBeforeComplete)
			|| !TestEqual(TEXT("Chunk publication preserves complete coverage including its short tail"), PublishedTriangles.Num(), TriangleCount)) { return false; }
		if (!TestTrue(TEXT("Fitting completes after bounded steps"), Projection.IsComplete())) { return false; }
		TestTrue(TEXT("Consumed chunks are not republished"), Projection.TakeReadyMeshes(2).IsEmpty());
		TestTrue(TEXT("Publication completion is separate from fitting completion"), Projection.IsPublicationComplete());
		auto Fitted = Projection.TakeMeshes();
		if (!TestEqual(TEXT("Final fitting retains its Layer count"), Fitted.Num(), Source.Num())) { return false; }
		TestTrue(TEXT("Progressive publication leaves the final Layer topology intact"), Fitted[0].Indices == Source[0].Indices);
		TestTrue(TEXT("Completed preview retains its first mesh without a consolidation upload"),
			FirstComponent->IsRegistered() && FirstComponent->GetDynamicMesh() == FirstMesh);
		TArray<UDynamicMeshComponent*> CompletedComponents;
		Preview->GetComponents(CompletedComponents);
		int32 SubmittedTriangles = 0;
		for (const auto* Component : CompletedComponents)
		{
			Component->ProcessMesh([&SubmittedTriangles](const UE::Geometry::FDynamicMesh3& Mesh)
			{
				SubmittedTriangles += Mesh.TriangleCount();
			});
		}
		TestEqual(TEXT("Persistent chunks retain all fitted triangles"), SubmittedTriangles, TriangleCount);
		Input = Source;
		Projection.Begin(MoveTemp(Input), {}, true);
		Projection.Advance(Project, MAX_int32);
		const auto LaterTileChunks = Projection.TakeReadyMeshes(1);
		if (!TestEqual(TEXT("Later geometry batches continue the regular chunk size"), LaterTileChunks.Num(), 1)) { return false; }
		TestEqual(TEXT("Only the document's first publication uses a small startup chunk"),
			LaterTileChunks[0].Indices.Num(), PIEPreviewChunkTriangleCount * 3);
		DestroyPIEPreviewActor(Preview.Get());
		TestFalse(TEXT("Completed preview cleanup unregisters persistent chunks"), FirstComponent->IsRegistered());

		Input = Source;
		Projection.Begin(MoveTemp(Input));
		Projection.Advance(Project, PIEPreviewChunkTriangleCount * 3);
		auto PendingChunks = Projection.TakeReadyMeshes(2);
		AActor* CancelledPreview = AppendPIEPreviewMeshes(*Storage, nullptr, PendingChunks);
		if (!TestNotNull(TEXT("Cancellation fixture has a published construction chunk"), CancelledPreview)) { return false; }
		TArray<UDynamicMeshComponent*> CancelledComponents;
		CancelledPreview->GetComponents(CancelledComponents);
		Projection.Reset();
		DestroyPIEPreviewActor(CancelledPreview);
		TestTrue(TEXT("Cancellation discards remaining fitting and unpublished chunks"),
			Projection.IsComplete() && Projection.TakeReadyMeshes(2).IsEmpty());
		for (const auto* Component : CancelledComponents)
		{
			TestFalse(TEXT("Cancellation unregisters construction primitives before scene release"), Component->IsRegistered());
		}
		return true;
	}();
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return bResult;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraMeshLayerPIEPersistentMeshTest,
	"ComposableCameraSystem.Editor.MeshCamera.PIEPreviewPersistentMeshes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshLayerPIEPersistentMeshTest::RunTest(const FString& /*Parameters*/)
{
	using namespace UE::ComposableCamera::MeshEditor;
	UWorld* World = UWorld::CreateWorld(EWorldType::PIE, false);
	GEngine->CreateNewWorldContext(EWorldType::PIE).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	const bool bResult = [this, World]()
	{
		AComposableCameraMeshSurfaceStorageActor* Storage = World->SpawnActor<AComposableCameraMeshSurfaceStorageActor>();
		if (!TestNotNull(TEXT("PIE storage exists"), Storage)) return false;
		TArray<FComposableCameraMeshLayerDefinition> Layers;
		Layers.SetNum(2);
		Layers[0].LayerId = FGuid::NewGuid(); Layers[1].LayerId = FGuid::NewGuid();
		Layers[0].DebugColor = FLinearColor(0.9f, 0.1f, 0.2f, 0.35f);
		Layers[1].DebugColor = FLinearColor(0.1f, 0.8f, 0.2f, 0.4f);
		FComposableCameraMeshSurfaceAuthoringData Data;
		AddMeshVisualizationTriangle(Data, Layers[0].LayerId);
		AddMeshVisualizationTriangle(Data, Layers[1].LayerId, 100.0f);
		Storage->SetAuthoringData(Layers, Data);
		const FTransform Transform(FRotator(0, 37, 0), FVector(100, 200, 300), FVector(2, 0.5, 1.5));
		Storage->SetActorTransform(Transform);
		FResolvedSurfaceVisualization Visualization;
		BuildRuntimeVisualization(Storage->GetRuntimeData(), Layers, Visualization);
		TArray<FResolvedSurfaceLayerMesh> Meshes;
		BuildVisualizationMeshes(Visualization, Layers, Meshes);
		if (!TestEqual(TEXT("Fixture exports both floor Layers"), Meshes.Num(), 2)) return false;
		ULineBatchComponent* LineBatcher = World->GetLineBatcher(UWorld::ELineBatcherType::WorldPersistent);
		LineBatcher->DrawLine(FVector::ZeroVector, FVector(10, 0, 0), FLinearColor::White, SDPG_World, 1.f, -1.f);
		const int32 ExternalLineCount = LineBatcher->BatchedLines.Num();
		const int32 ExternalMeshCount = LineBatcher->BatchedMeshes.Num();
		AActor* Preview = CreatePIEPreviewActor(*Storage, Meshes);
		if (!TestNotNull(TEXT("Persistent preview actor exists"), Preview)) return false;
		TestTrue(TEXT("Preview belongs to the source Level"), Preview->GetLevel() == Storage->GetLevel());
		TestTrue(TEXT("Preview is transient"), Preview->HasAnyFlags(RF_Transient));
		TestTrue(TEXT("Hidden storage does not hide the separate preview actor"), Storage->IsHidden() && !Preview->IsHidden());
		TestTrue(TEXT("Preview starts at the document transform"), Preview->GetActorTransform().Equals(Transform));
		TestEqual(TEXT("Preview does not submit per-frame LineBatcher meshes"), LineBatcher->BatchedMeshes.Num(), ExternalMeshCount);
		TArray<UDynamicMeshComponent*> Components;
		Preview->GetComponents(Components);
		if (!TestEqual(TEXT("One persistent mesh component per visible Layer"), Components.Num(), Meshes.Num())) return false;
		const FVector SamplePoint = Transform.TransformPosition(FVector(23, 21, 0));
		const FPIEPreviewPointSample Sample = SamplePIEPreviewActor(*Preview, SamplePoint);
		TestTrue(TEXT("Point diagnostic finds submitted coverage and the nearest scaled stacked surface"),
			Sample.Intersections > 0 && Sample.NearestPosition.Equals(
				Transform.TransformPosition(FVector(23, 21, VisualizationSurfaceOffset)), 1.e-4));
		TestEqual(TEXT("Point diagnostic reports zero intersections outside submitted coverage"),
			SamplePIEPreviewActor(*Preview, SamplePoint + FVector(10000, 10000, 0)).Intersections, 0);
		auto BuriedMeshes = Meshes;
		for (auto& Mesh : BuriedMeshes)
		{
			for (FVector& Vertex : Mesh.LocalVertices) { Vertex.Z -= 10.0; }
		}
		AActor* BuriedPreview = CreatePIEPreviewActor(*Storage, BuriedMeshes);
		if (!TestNotNull(TEXT("Buried point-diagnostic fixture exists"), BuriedPreview)) return false;
		const FPIEPreviewPointSample BuriedSample = SamplePIEPreviewActor(*BuriedPreview, SamplePoint);
		TestTrue(TEXT("Point diagnostic distinguishes submitted but buried geometry from missing coverage"),
			BuriedSample.Intersections > 0 && BuriedSample.NearestPosition.Z < SamplePoint.Z);
		DestroyPIEPreviewActor(BuriedPreview);
		for (int32 Index = 0; Index < Components.Num(); ++Index)
		{
			UDynamicMeshComponent* Component = Components[Index];
			TestTrue(TEXT("Registered mesh is owned by the PIE actor"), Component->IsRegistered() && Component->GetOwner() == Preview);
			TestTrue(TEXT("Preview has no collision or ticking"), Component->GetCollisionEnabled() == ECollisionEnabled::NoCollision
				&& !Component->PrimaryComponentTick.bCanEverTick);
			Component->GetDynamicMesh()->ProcessMesh([this, &Meshes, Index](const UE::Geometry::FDynamicMesh3& Mesh)
			{
				const int32 SourceVertices = Meshes[Index].LocalVertices.Num();
				const int32 SourceTriangles = Meshes[Index].Indices.Num() / 3;
				TestEqual(TEXT("Persistent mesh preserves fitted vertices without backface duplication"), Mesh.VertexCount(), SourceVertices);
				TestEqual(TEXT("Persistent mesh preserves fitted triangles"), Mesh.TriangleCount(), SourceTriangles);
				TestTrue(TEXT("Surface vertices remain in document-local space"), Mesh.GetVertex(0).Equals(Meshes[Index].LocalVertices[0]));
			});
			UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Component->GetMaterial(0));
			if (!TestNotNull(TEXT("Layer has a persistent color material"), Material)) return false;
			TestTrue(TEXT("PIE uses a depth-tested material"), Material->Parent == GEngine->DebugMeshMaterial.Get());
			TestTrue(TEXT("Characters occlude the floor overlay"), Material->GetMaterial()->bDisableDepthTest == 0);
			TestTrue(TEXT("Material renders both sides without duplicate translucent triangles"), Material->IsTwoSided());
			TestTrue(TEXT("Layer color and alpha match the previous mesh export"),
				Material->K2_GetVectorParameterValue(NAME_Color).Equals(FLinearColor(Meshes[Index].Color)));
		}
		TWeakObjectPtr<AActor> WeakPreview = Preview;
		TWeakObjectPtr<UDynamicMeshComponent> WeakComponent = Components[0];
		CollectGarbage(RF_NoFlags);
		if (!TestTrue(TEXT("Level ownership retains preview and meshes through GC"), WeakPreview.IsValid() && WeakComponent.IsValid())) return false;
		UDynamicMesh* MeshObject = Components[0]->GetDynamicMesh();
		int32 MeshChanges = 0;
		const FDelegateHandle ChangeHandle = MeshObject->OnMeshChanged().AddLambda(
			[&MeshChanges](UDynamicMesh*, FDynamicMeshChangeInfo) { ++MeshChanges; });
		TestFalse(TEXT("Stable transform skips updates"), UpdatePIEPreviewTransform(*Preview, Transform));
		for (int32 Frame = 1; Frame <= 120; ++Frame)
		{
			FTransform Moved = Transform;
			Moved.AddToTranslation(FVector(Frame * 10.0, 0, 0));
			TestTrue(TEXT("Document movement updates only the actor transform"), UpdatePIEPreviewTransform(*Preview, Moved));
			TestFalse(TEXT("Repeated transform does no work"), UpdatePIEPreviewTransform(*Preview, Moved));
		}
		MeshObject->OnMeshChanged().Remove(ChangeHandle);
		TestEqual(TEXT("Movement never rewrites persistent mesh geometry"), MeshChanges, 0);
		TestTrue(TEXT("Movement keeps the same mesh object"), Components[0]->GetDynamicMesh() == MeshObject);
		AActor* OtherPreview = CreatePIEPreviewActor(*Storage, Meshes);
		if (!TestNotNull(TEXT("Second independent preview exists"), OtherPreview)) return false;
		DestroyPIEPreviewActor(Preview);
		for (UDynamicMeshComponent* Component : Components)
		{
			TestFalse(TEXT("Cleanup unregisters primitives before scene teardown"), Component->IsRegistered());
		}
		TestFalse(TEXT("Cleanup leaves another preview alive"), OtherPreview->IsActorBeingDestroyed());
		TestEqual(TEXT("Cleanup preserves external debug lines"), LineBatcher->BatchedLines.Num(), ExternalLineCount);
		TestEqual(TEXT("Cleanup preserves external debug meshes"), LineBatcher->BatchedMeshes.Num(), ExternalMeshCount);
		DestroyPIEPreviewActor(OtherPreview);
		DestroyPIEPreviewActor(Preview);
		TestNull(TEXT("Empty documents allocate no preview actor"), CreatePIEPreviewActor(*Storage, TConstArrayView<FResolvedSurfaceLayerMesh>()));
		return true;
	}();
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return bResult;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraMeshLayerPIEPreviewWorldRoutingTest,
	"ComposableCameraSystem.Editor.MeshCamera.PIEPreviewWorldRouting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshLayerPIEPreviewWorldRoutingTest::RunTest(
	const FString& /*Parameters*/)
{
	using UE::ComposableCamera::MeshEditor::ShouldDrawPIEPreview;

	TestFalse(TEXT("Disabled preview rejects PIE"),
		ShouldDrawPIEPreview(false, false, EWorldType::PIE));
	TestTrue(TEXT("Enabled preview accepts PIE game worlds"),
		ShouldDrawPIEPreview(true, false, EWorldType::PIE));
	TestFalse(TEXT("PIE teardown rejects new preview work"),
		ShouldDrawPIEPreview(true, true, EWorldType::PIE));
	TestFalse(TEXT("PIE component path does not duplicate editor-mode drawing"),
		ShouldDrawPIEPreview(true, false, EWorldType::Editor));
	TestFalse(TEXT("Editor-only Show command does not target packaged Game worlds"),
		ShouldDrawPIEPreview(true, false, EWorldType::Game));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshLayerPIESchedulingTest,
	"ComposableCameraSystem.Editor.MeshCamera.PIEPreviewScheduling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshLayerPIESchedulingTest::RunTest(const FString&)
{
	using namespace UE::ComposableCamera::MeshEditor;
	FPIEPreviewSurfaceProjection Jobs[2];
	for (int32 JobIndex = 0; JobIndex < 2; ++JobIndex)
	{
		TArray<FResolvedSurfaceLayerMesh> Meshes;
		Meshes.SetNum(1);
		Meshes[0].LayerIndex = JobIndex;
		const int32 NumTriangles = JobIndex == 0 ? 5000 : 512;
		for (int32 Triangle = 0; Triangle < NumTriangles; ++Triangle)
		{
			const int32 Base = Meshes[0].LocalVertices.Num();
			const FVector A(Triangle * 4.0, JobIndex * 100.0, VisualizationSurfaceOffset);
			Meshes[0].LocalVertices.Append({A, A + FVector(1, 0, 0), A + FVector(0, 1, 0)});
			Meshes[0].Indices.Append({Base, Base + 1, Base + 2});
		}
		Jobs[JobIndex].Begin(MoveTemp(Meshes));
	}
	int32 NextProjectionJob = 0;
	int32 Calls[2] = {0, 0};
	for (int32 Tick = 0; Tick < 2; ++Tick)
	{
		int32 RemainingQueries = 256;
		RunPIEPreviewWorkRoundRobin(2, NextProjectionJob, [&](int32 JobIndex)
		{
			if (RemainingQueries <= 0) { return EPIEPreviewWorkResult::BudgetExhausted; }
			if (Jobs[JobIndex].IsComplete()) { return EPIEPreviewWorkResult::Skipped; }
			int32 Queries = 0;
			Jobs[JobIndex].Advance([&](const FVector& Vertex)
			{
				++Calls[JobIndex];
				return Vertex + FVector(0, 0, 4);
			}, FMath::Min(RemainingQueries, PIEPreviewProjectionQuerySlice), 0.0, &Queries);
			TestTrue(TEXT("Each document visit respects the query slice"), Queries <= PIEPreviewProjectionQuerySlice);
			RemainingQueries -= Queries;
			return EPIEPreviewWorkResult::Advanced;
		});
		TestEqual(TEXT("Both documents share the global query budget"), RemainingQueries, 0);
		TestEqual(TEXT("A later document starts before the large first document completes"), Calls[1], (Tick + 1) * 128);
		TestEqual(TEXT("The first document retains a fair query share"), Calls[0], Calls[1]);
	}
	TestFalse(TEXT("The large first document remains pending"), Jobs[0].IsComplete());
	TestFalse(TEXT("The second document already has fitted chunks while still pending"), Jobs[1].IsComplete());

	int32 NextPublicationJob = 0;
	int32 Published[2] = {0, 0};
	// A one-chunk budget forces the next tick to resume at the other document.
	for (int32 Tick = 0; Tick < 2; ++Tick)
	{
		int32 RemainingChunks = 1;
		RunPIEPreviewWorkRoundRobin(2, NextPublicationJob, [&](int32 JobIndex)
		{
			if (RemainingChunks <= 0) { return EPIEPreviewWorkResult::BudgetExhausted; }
			auto Ready = Jobs[JobIndex].TakeReadyMeshes(1);
			if (Ready.IsEmpty()) { return EPIEPreviewWorkResult::Skipped; }
			--RemainingChunks;
			Published[JobIndex] += Ready[0].Indices.Num() / 3;
			for (const FVector& Vertex : Ready[0].LocalVertices)
			{
				TestTrue(TEXT("Scheduled chunks contain fitted geometry"),
					FMath::IsNearlyEqual(Vertex.Z, VisualizationSurfaceOffset + 4));
			}
			return EPIEPreviewWorkResult::Advanced;
		});
	}
	TestEqual(TEXT("The first document publishes its initial chunk"), Published[0], PIEPreviewFirstChunkTriangleCount);
	TestEqual(TEXT("The later document publishes without waiting for the first to complete"), Published[1], PIEPreviewFirstChunkTriangleCount);
	int32 SoloNextJob = 0;
	int32 SoloRemainingQueries = 256;
	RunPIEPreviewWorkRoundRobin(1, SoloNextJob, [&](int32)
	{
		if (SoloRemainingQueries <= 0) { return EPIEPreviewWorkResult::BudgetExhausted; }
		int32 Queries = 0;
		Jobs[0].Advance([](const FVector& Vertex) { return Vertex + FVector(0, 0, 4); },
			FMath::Min(SoloRemainingQueries, PIEPreviewProjectionQuerySlice), 0.0, &Queries);
		SoloRemainingQueries -= Queries;
		return EPIEPreviewWorkResult::Advanced;
	});
	TestEqual(TEXT("A single active document can use the full budget through repeated visits"), SoloRemainingQueries, 0);

	int32 NextJob = 0;
	int32 FirstJob = INDEX_NONE;
	RunPIEPreviewWorkRoundRobin(3, NextJob, [&](int32 JobIndex)
	{
		if (FirstJob != INDEX_NONE) { return EPIEPreviewWorkResult::BudgetExhausted; }
		FirstJob = JobIndex;
		return EPIEPreviewWorkResult::Advanced;
	});
	TestEqual(TEXT("Time-budget exhaustion retains the next unserved job"), NextJob, 1);
	FirstJob = INDEX_NONE;
	RunPIEPreviewWorkRoundRobin(3, NextJob, [&](int32 JobIndex)
	{
		if (FirstJob != INDEX_NONE) { return EPIEPreviewWorkResult::BudgetExhausted; }
		FirstJob = JobIndex;
		return EPIEPreviewWorkResult::Advanced;
	});
	TestEqual(TEXT("Next tick starts with the previously unserved document"), FirstJob, 1);
	int32 SkippedJobs = 0;
	RunPIEPreviewWorkRoundRobin(2, NextJob, [&](int32)
	{
		++SkippedJobs;
		return EPIEPreviewWorkResult::Skipped;
	});
	TestEqual(TEXT("A finished or unavailable queue is visited once without spinning"), SkippedJobs, 2);
	NextJob = 9;
	int32 RemainingVisits = 1;
	RunPIEPreviewWorkRoundRobin(2, NextJob, [&](int32 JobIndex)
	{
		if (RemainingVisits <= 0) { return EPIEPreviewWorkResult::BudgetExhausted; }
		--RemainingVisits;
		TestTrue(TEXT("Streaming changes normalize the saved queue position"), JobIndex >= 0 && JobIndex < 2);
		return EPIEPreviewWorkResult::Advanced;
	});
	RunPIEPreviewWorkRoundRobin(0, NextJob, [](int32) { return EPIEPreviewWorkResult::BudgetExhausted; });
	TestEqual(TEXT("Cleared queues reset the saved position"), NextJob, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshBrushAppendCoverageTest,
	"ComposableCameraSystem.Editor.MeshCamera.BrushAppendCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshBrushAppendCoverageTest::RunTest(const FString&)
{
	using namespace UE::ComposableCamera::MeshEditor;
	TArray<FComposableCameraMeshLayerDefinition> Layers; Layers.SetNum(2);
	Layers[0].LayerId = FGuid::NewGuid(); Layers[1].LayerId = FGuid::NewGuid();
	FComposableCameraMeshSurfaceAuthoringData Data;
	AddMeshVisualizationTriangle(Data, Layers[1].LayerId);
	AddMeshVisualizationTriangle(Data, Layers[1].LayerId, 100.0f);
	for (int32 Triangle = 0; Triangle < 512; ++Triangle)
	{
		const double X = 1000.0 + Triangle * 10.0;
		AddExactVisualizationTriangle(Data, Layers[1].LayerId, FVector(X, 0, 0), FVector(X + 5, 0, 0), FVector(X, 5, 0));
	}
	FMeshLayerAuthoringIndex SourceIndex; SourceIndex.Build(Data);
	FResolvedSurfaceVisualization Cached; BuildAuthoringVisualization(Data, Layers, Cached);
	const FIntPoint RemoteGrid(100, 0);
	const int32 RemoteIndex = Cached.CellsByGrid.FindChecked(RemoteGrid)[0];
	const auto RemoteCell = Cached.Cells[RemoteIndex];
	for (int32 Stamp = 0; Stamp < 40; ++Stamp)
	{
		const int32 FirstTriangle = Data.TriangleLayerIds.Num();
		const double X = (Stamp % 4) * 7.0, Y = (Stamp % 3) * 6.0;
		const double Height = Stamp % 7 == 0 ? 100.0 : 0.0;
		const FVector A(X, Y, Height), B(X + 35.0, Y, Height + (Stamp % 5)), C(X, Y + 35.0, Height + (Stamp % 3));
		AddExactVisualizationTriangle(Data, Layers[Stamp % 2].LayerId, A, B, C);
		SourceIndex.Append(Data, FirstTriangle);
		FVisualizationUpdateStats Stats;
		AppendAuthoringVisualization(Data, Layers, FirstTriangle, FBox2D(FVector2D(X, Y), FVector2D(X + 35, Y + 35)), Cached, &Stats, &SourceIndex);
		TestFalse(TEXT("Normal stamps retain grid resolution"), Stats.bFullRebuild);
		TestEqual(TEXT("Brush tests only its newly appended triangle"), Stats.SourceTriangleTests, 1);
		TestTrue(TEXT("Brush refreshes only its tail block"), SourceIndex.GetLastRefreshedTriangleCount() <= FMeshLayerAuthoringIndex::TrianglesPerBlock);
	}
	TestTrue(TEXT("Untouched remote cells retain coverage and lookup identity"), Cached.CellsByGrid.FindChecked(RemoteGrid)[0] == RemoteIndex
		&& SameVisualizationCoverage(RemoteCell, Cached.Cells[RemoteIndex]));
	FResolvedSurfaceVisualization Full; BuildAuthoringVisualization(Data, Layers, Full);
	TestTrue(TEXT("Repeated sloped, overlapping and stacked stamps match full resolution"), SameResolvedVisualization(Cached, Full));
	const int32 BeforeGrowth = Data.TriangleLayerIds.Num();
	AddMeshVisualizationTriangle(Data, Layers[0].LayerId, 0.0f, FVector2D(100000, 100000));
	SourceIndex.Append(Data, BeforeGrowth);
	FVisualizationUpdateStats Growth;
	AppendAuthoringVisualization(Data, Layers, BeforeGrowth, FBox2D(FVector2D(100000, 100000), FVector2D(100100, 100100)), Cached, &Growth, &SourceIndex);
	BuildAuthoringVisualization(Data, Layers, Full);
	TestTrue(TEXT("Substantial growth retains the original full-regrid policy"), Growth.bFullRebuild && SameResolvedVisualization(Cached, Full));
	Layers[1].bEnabled = false; BuildAuthoringVisualization(Data, Layers, Cached);
	const int32 BeforeDisabled = Data.TriangleLayerIds.Num();
	AddMeshVisualizationTriangle(Data, Layers[1].LayerId, 0.0f, FVector2D(-1000000, -1000000));
	SourceIndex.Append(Data, BeforeDisabled);
	AppendAuthoringVisualization(Data, Layers, BeforeDisabled, FBox2D(FVector2D(-1000000, -1000000), FVector2D(-999900, -999900)), Cached, nullptr, &SourceIndex);
	BuildAuthoringVisualization(Data, Layers, Full);
	TestTrue(TEXT("Disabled appended geometry cannot expand the visible grid"), SameResolvedVisualization(Cached, Full));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshIndexedEraseCoverageTest,
	"ComposableCameraSystem.Editor.MeshCamera.IndexedEraseCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshIndexedEraseCoverageTest::RunTest(const FString&)
{
	using namespace UE::ComposableCamera::MeshEditor;
	TArray<FComposableCameraMeshLayerDefinition> Layers; Layers.SetNum(2);
	Layers[0].LayerId = FGuid::NewGuid(); Layers[1].LayerId = FGuid::NewGuid();
	FComposableCameraMeshSurfaceAuthoringData Data;
	AddMeshVisualizationTriangle(Data, Layers[0].LayerId); AddMeshVisualizationTriangle(Data, Layers[1].LayerId);
	AddMeshVisualizationTriangle(Data, Layers[1].LayerId, 100.0f);
	for (int32 Triangle = 0; Triangle < 4096; ++Triangle)
	{
		const double X = 1000.0 + Triangle * 6.0;
		AddExactVisualizationTriangle(Data, Layers[0].LayerId, FVector(X, 0, 0), FVector(X + 5, 0, 0), FVector(X, 5, 0));
	}
	FMeshLayerAuthoringIndex SourceIndex; SourceIndex.Build(Data);
	FResolvedSurfaceVisualization Cached; BuildAuthoringVisualization(Data, Layers, Cached);
	FComposableCameraMeshEraseStamp Stamp; Stamp.Center = FVector(20, 20, 0); Stamp.Radius = 8.0; Stamp.Depth = 10.0;
	FEraseGeometryStats EraseStats; FBox2D Dirty(ForceInit);
	TestTrue(TEXT("Indexed eraser changes near coverage"), EraseShapeGeometry(Data, Layers[0].LayerId, Stamp, false, &EraseStats, &Dirty, &SourceIndex));
	TestTrue(TEXT("Distant source triangles never enter per-triangle eraser tests"), EraseStats.ConsideredTriangles <= 128);
	TestTrue(TEXT("Swap-removal and fragment appends refresh only touched blocks"), SourceIndex.IsCurrent(Data)
		&& SourceIndex.GetLastRefreshedTriangleCount() <= 256);
	FVisualizationUpdateStats Stats; UpdateAuthoringVisualization(Data, Layers, Dirty, Cached, &Stats, &SourceIndex);
	TestFalse(TEXT("Local erasure retains the grid"), Stats.bFullRebuild);
	TestTrue(TEXT("Coverage bounds and rasterization avoid a full source scan"), Stats.SourceTriangleTests < 384);
	FResolvedSurfaceVisualization Full; BuildAuthoringVisualization(Data, Layers, Full);
	TestTrue(TEXT("Indexed erasure retains remote, lower-Layer and stacked coverage"), SameResolvedVisualization(Cached, Full));
	Stamp.Center = FVector(-10000, -10000, 0);
	TestFalse(TEXT("An empty region is a no-op"), EraseShapeGeometry(Data, Layers[0].LayerId, Stamp, false, &EraseStats, &Dirty, &SourceIndex));
	TestEqual(TEXT("Empty region considers no source triangles"), EraseStats.ConsideredTriangles, 0);
	// Undo/Discard can restore different geometry with identical counts: explicit invalidation is required.
	for (auto& Vertex : Data.Vertices) { Vertex.Y += 1000.0f; }
	SourceIndex.Reset(); SourceIndex.Build(Data);
	BuildAuthoringVisualization(Data, Layers, Cached);
	Stamp.Center = FVector(40, 1040, 0);
	TestTrue(TEXT("Rebuilt index follows a same-count restored document"), EraseShapeGeometry(Data, Layers[0].LayerId, Stamp, false, nullptr, &Dirty, &SourceIndex));
	UpdateAuthoringVisualization(Data, Layers, Dirty, Cached, nullptr, &SourceIndex); BuildAuthoringVisualization(Data, Layers, Full);
	TestTrue(TEXT("Restored geometry has equivalent indexed coverage"), SameResolvedVisualization(Cached, Full));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshBudgetedCoverageTest,
	"ComposableCameraSystem.Editor.MeshCamera.BudgetedCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshBudgetedCoverageTest::RunTest(const FString&)
{
	using namespace UE::ComposableCamera::MeshEditor;
	TArray<FComposableCameraMeshLayerDefinition> Layers; Layers.SetNum(2);
	Layers[0].LayerId = FGuid::NewGuid(); Layers[1].LayerId = FGuid::NewGuid();
	FComposableCameraMeshSurfaceAuthoringData Data;
	AddMeshVisualizationTriangle(Data, Layers[1].LayerId); AddMeshVisualizationTriangle(Data, Layers[1].LayerId, 100.0f);
	FMeshLayerAuthoringIndex Index; Index.Build(Data);
	FResolvedSurfaceVisualization Cached, Full;
	FAuthoringVisualizationUpdate Update;
	auto Complete = [&]()
	{
		int32 Steps = 0;
		while (!Update.Advance(Data, Layers, Cached, 1, 0.0) && ++Steps < 100000) {}
		return Steps < 100000;
	};
	Update.Begin(Data, Layers, Cached, nullptr, &Index);
	TestFalse(TEXT("One operation cannot rasterize a full document"), Update.Advance(Data, Layers, Cached, 1, 0.0));
	TestTrue(TEXT("Full coverage resumes"), Complete());
	BuildAuthoringVisualization(Data, Layers, Full);
	TestTrue(TEXT("Full resumable result matches the independent complete resolver"), SameResolvedVisualization(Cached, Full));
	for (int32 Stamp = 0; Stamp < 12; ++Stamp)
	{
		const int32 First = Data.TriangleLayerIds.Num();
		const double X = (Stamp % 4) * 7.0, Y = (Stamp % 3) * 6.0, Z = Stamp % 4 == 0 ? 100.0 : 0.0;
		AddExactVisualizationTriangle(Data, Layers[Stamp % 2].LayerId, FVector(X, Y, Z), FVector(X + 35, Y, Z + 3), FVector(X, Y + 35, Z + 1));
		Index.Append(Data, First);
		const FBox2D Dirty(FVector2D(X, Y), FVector2D(X + 35, Y + 35));
		Update.Begin(Data, Layers, Cached, &Dirty, &Index, First);
		TestTrue(TEXT("Append resumes without dropping cells or incoming triangles"), Complete());
		TestEqual(TEXT("Normal append still visits only its source tail"), Update.GetStats().SourceTriangleTests, 1);
	}
	BuildAuthoringVisualization(Data, Layers, Full);
	TestTrue(TEXT("Slopes, repeated overlap, priority and stacked surfaces remain equivalent"), SameResolvedVisualization(Cached, Full));
	const FGuid ShapeId = FGuid::NewGuid();
	Data.TriangleShapeIds.SetNum(Data.TriangleLayerIds.Num());
	for (int32 Triangle = 0; Triangle < Data.TriangleLayerIds.Num(); ++Triangle)
	{
		if (Data.TriangleLayerIds[Triangle] == Layers[0].LayerId) { Data.TriangleShapeIds[Triangle] = ShapeId; }
	}
	auto& Shape = Data.Shapes.AddDefaulted_GetRef(); Shape.ShapeId = ShapeId; Shape.LayerId = Layers[0].LayerId;
	auto Reference = Data;
	FComposableCameraMeshEraseStamp Stamp; Stamp.Center = FVector(20, 20, 0); Stamp.Radius = 8.0; Stamp.Depth = 10.0;
	FEraseGeometryBuild Erase;
	Erase.Begin(Data, Layers[0].LayerId, Stamp, true, &Index);
	int32 EraseSteps = 0;
	while (!Erase.Advance(Data, &Index, 1, 0.0) && ++EraseSteps < 100000) {}
	TestTrue(TEXT("Exact clipping yields and eventually completes"), EraseSteps > 0 && EraseSteps < 100000 && Erase.HasChanged());
	EraseShapeGeometry(Reference, Layers[0].LayerId, Stamp, true);
	TestTrue(TEXT("Yielding preserves exact erase vertex/index/ownership order"), Data.Vertices == Reference.Vertices && Data.Indices == Reference.Indices
		&& Data.TriangleLayerIds == Reference.TriangleLayerIds && Data.TriangleShapeIds == Reference.TriangleShapeIds && Index.IsCurrent(Data));
	TestTrue(TEXT("Shape masks finalize once after all partial cuts"), Data.Shapes[0].Erasures.Num() == 1 && Reference.Shapes[0].Erasures.Num() == 1);
	Update.Begin(Data, Layers, Cached, &Erase.GetChangedBounds(), &Index);
	TestTrue(TEXT("Regional removal and lower-Layer replay resume"), Complete());
	BuildAuthoringVisualization(Data, Layers, Full);
	TestTrue(TEXT("Erase holes and exposed lower Layers match full coverage"), SameResolvedVisualization(Cached, Full));
	const int32 FirstGrowth = Data.TriangleLayerIds.Num();
	AddMeshVisualizationTriangle(Data, Layers[0].LayerId, 0.0f, FVector2D(100000, 100000)); Index.Append(Data, FirstGrowth);
	const FBox2D Growth(FVector2D(100000, 100000), FVector2D(100100, 100100));
	Update.Begin(Data, Layers, Cached, &Growth, &Index, FirstGrowth); Complete(); BuildAuthoringVisualization(Data, Layers, Full);
	TestTrue(TEXT("Large growth keeps the exact full-regrid fallback"), Update.GetStats().bFullRebuild && SameResolvedVisualization(Cached, Full));
	Layers[0].bEnabled = false;
	Update.Begin(Data, Layers, Cached, nullptr, &Index); Complete(); BuildAuthoringVisualization(Data, Layers, Full);
	TestTrue(TEXT("Disabled Layer bounds and coverage remain equivalent"), SameResolvedVisualization(Cached, Full));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshAsyncStrokeCoverageTest,
	"ComposableCameraSystem.Editor.MeshCamera.AsyncStrokeCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshAsyncStrokeCoverageTest::RunTest(const FString&)
{
	using namespace UE::ComposableCamera::MeshEditor;
	TArray<FComposableCameraMeshLayerDefinition> Layers; Layers.SetNum(3);
	for (auto& Layer : Layers) { Layer.LayerId = FGuid::NewGuid(); }
	Layers[2].bEnabled = false;
	FComposableCameraMeshSurfaceAuthoringData Data;
	AddExactVisualizationTriangle(Data, Layers[1].LayerId, FVector(-80, -80, 0), FVector(80, -80, 12), FVector(-80, 80, 8));
	AddExactVisualizationTriangle(Data, Layers[1].LayerId, FVector(-80, -80, 60), FVector(80, -80, 72), FVector(-80, 80, 68));
	for (int32 Triangle = 0; Triangle < 640; ++Triangle)
	{
		AddMeshVisualizationTriangle(Data, Layers[1].LayerId, 0, FVector2D(1000 + Triangle % 20 * 120, 1000 + Triangle / 20 * 120));
	}
	AddMeshVisualizationTriangle(Data, Layers[2].LayerId, 0, FVector2D(100000, 100000));
	FMeshLayerAuthoringIndex Index; Index.Build(Data);
	FResolvedSurfaceVisualization Cache, Reference;
	BuildAuthoringVisualization(Data, Layers, Cache); Reference = Cache;
	FMeshLayerStrokeCoverage Pipeline;
	const FBox2D Dirty(FVector2D(-90, -90), FVector2D(90, 90));
	const int32 First = Data.TriangleLayerIds.Num();
	AddExactVisualizationTriangle(Data, Layers[0].LayerId, FVector(-60, -60, 2), FVector(40, -60, 10), FVector(-60, 40, 7));
	Index.Append(Data, First);
	AppendAuthoringVisualization(Data, Layers, First, Dirty, Reference, nullptr, &Index);
	const auto FirstExpected = Reference;
	Pipeline.Queue(Data, Layers, Index, Dirty, First, Cache.CellSize, false);
	FBox2D PublishedBounds(ForceInit); bool bFull = false;
	TestFalse(TEXT("Partial source ownership prevents snapshot launch"), Pipeline.Advance(Cache, PublishedBounds, bFull, false, false));
	TestTrue(TEXT("Deferral keeps the exact live cache"), Pipeline.HasPending() && !Cache.Cells.IsEmpty());
	TestFalse(TEXT("Finite advance dispatches without waiting for coverage"), Pipeline.Advance(Cache, PublishedBounds, bFull));
	TestEqual(TEXT("Append snapshot copies only its new source tail"), Pipeline.GetLastSnapshotTriangleCount(), 1);
	const int32 Second = Data.TriangleLayerIds.Num();
	AddExactVisualizationTriangle(Data, Layers[0].LayerId, FVector(-30, -60, 2), FVector(70, -60, 10), FVector(-30, 40, 7));
	Index.Append(Data, Second);
	AppendAuthoringVisualization(Data, Layers, Second, Dirty, Reference, nullptr, &Index);
	Pipeline.Queue(Data, Layers, Index, Dirty, Second, Cache.CellSize, false);
	FComposableCameraMeshEraseStamp Cut; Cut.Center = FVector(-20, -20, 5); Cut.Radius = 8; Cut.Depth = 20;
	FBox2D CutBounds(ForceInit);
	TestTrue(TEXT("Mixed fixture erases original exact geometry"), EraseShapeGeometry(Data, Layers[0].LayerId, Cut, false, nullptr, &CutBounds, &Index));
	UpdateAuthoringVisualization(Data, Layers, CutBounds, Reference, nullptr, &Index);
	Pipeline.Queue(Data, Layers, Index, CutBounds, INDEX_NONE, Cache.CellSize, false);
	TestTrue(TEXT("First completed immutable state can publish despite later source mutations"), Pipeline.Advance(Cache, PublishedBounds, bFull, true));
	TestTrue(TEXT("First snapshot retains exact slopes, priorities and stacked floors"), SameResolvedVisualization(Cache, FirstExpected));
	TestTrue(TEXT("Later source edits stay pending independently"), Pipeline.HasPending());
	TestFalse(TEXT("Next regional state launches without waiting"), Pipeline.Advance(Cache, PublishedBounds, bFull));
	TestTrue(TEXT("Regional snapshot avoids copying distant source blocks"), Pipeline.GetLastSnapshotTriangleCount() < Data.TriangleLayerIds.Num() / 2);
	const auto SavedSource = Data; Data.Reset(); Index.Reset();
	TestTrue(TEXT("Worker result owns geometry after live source/index reset"), Pipeline.Advance(Cache, PublishedBounds, bFull, true));
	TestTrue(TEXT("Batched mixed edits retain complete exact coverage in original order"), SameResolvedVisualization(Cache, Reference));
	TestFalse(TEXT("Mixed queue drains completely"), Pipeline.HasPending());
	Data = SavedSource; Index.Build(Data);
	for (int32 Stamp = 0; Stamp < 2; ++Stamp)
	{
		const int32 Added = Data.TriangleLayerIds.Num();
		AddExactVisualizationTriangle(Data, Layers[0].LayerId, FVector(-60 + Stamp * 15, -40, 4), FVector(30 + Stamp * 15, -40, 9), FVector(-60 + Stamp * 15, 50, 6));
		Index.Append(Data, Added); AppendAuthoringVisualization(Data, Layers, Added, Dirty, Reference, nullptr, &Index);
		Pipeline.Queue(Data, Layers, Index, Dirty, Added, Cache.CellSize, false);
	}
	TestTrue(TEXT("Coalesced append work publishes"), Pipeline.Advance(Cache, PublishedBounds, bFull, true));
	TestEqual(TEXT("Two waiting append stamps copy two source triangles"), Pipeline.GetLastSnapshotTriangleCount(), 2);
	TestTrue(TEXT("Coalesced append preserves original rasterization order"), SameResolvedVisualization(Cache, Reference));
	Pipeline.Queue(Data, Layers, Index, Dirty, INDEX_NONE, Cache.CellSize, false);
	Pipeline.Advance(Cache, PublishedBounds, bFull);
	Pipeline.Cancel(); Cache = Reference;
	TestFalse(TEXT("Cancelled result cannot replace restored coverage"), Pipeline.Advance(Cache, PublishedBounds, bFull, true));
	TestTrue(TEXT("Cancelled jobs have no callback into the live cache"), SameResolvedVisualization(Cache, Reference));

	// An empty compact regional snapshot still clears old local coverage while
	// preserving a distant floor. Empty snapshot does not mean empty document.
	FComposableCameraMeshSurfaceAuthoringData FarOnly;
	AddMeshVisualizationTriangle(FarOnly, Layers[1].LayerId, 0, FVector2D(3000, 3000));
	Index.Build(FarOnly);
	const FBox2D Removed(FVector2D(-100, -100), FVector2D(6000, 6000));
	UpdateAuthoringVisualization(FarOnly, Layers, Removed, Reference, nullptr, &Index);
	Pipeline.Queue(FarOnly, Layers, Index, Removed, INDEX_NONE, Cache.CellSize, false);
	Pipeline.Advance(Cache, PublishedBounds, bFull, true);
	TestTrue(TEXT("Regional rebuild retains only actual surviving coverage"), SameResolvedVisualization(Cache, Reference));
	const FBox2D EmptyRegion(FVector2D(-90, -90), FVector2D(90, 90));
	Pipeline.Queue(FarOnly, Layers, Index, EmptyRegion, INDEX_NONE, Cache.CellSize, false);
	Pipeline.Advance(Cache, PublishedBounds, bFull, true);
	TestEqual(TEXT("Empty region snapshot contains no source triangles"), Pipeline.GetLastSnapshotTriangleCount(), 0);
	TestTrue(TEXT("Empty regional source does not clear distant cells"), SameResolvedVisualization(Cache, Reference));

	// Growth can regrid even when the growing source is erased before launch.
	Pipeline.Cancel(); Data = FarOnly; Index.Build(Data); BuildAuthoringVisualization(Data, Layers, Cache); Reference = Cache;
	const int32 GrowthFrom = Data.TriangleLayerIds.Num();
	AddMeshVisualizationTriangle(Data, Layers[0].LayerId, 0, FVector2D(100000, 100000)); Index.Append(Data, GrowthFrom);
	const FBox2D Growth(FVector2D(3000, 3000), FVector2D(100100, 100100));
	AppendAuthoringVisualization(Data, Layers, GrowthFrom, Growth, Reference, nullptr, &Index);
	Pipeline.Queue(Data, Layers, Index, Growth, GrowthFrom, Cache.CellSize, false);
	Data = FarOnly; Index.Build(Data); UpdateAuthoringVisualization(Data, Layers, Growth, Reference, nullptr, &Index);
	Pipeline.Queue(Data, Layers, Index, Growth, INDEX_NONE, Cache.CellSize, false);
	Pipeline.Advance(Cache, PublishedBounds, bFull, true);
	TestTrue(TEXT("Coalescing retains transient growth and release grid policy"), bFull && SameResolvedVisualization(Cache, Reference));
	Data.Reset(); Index.Build(Data);
	Pipeline.Queue(Data, Layers, Index, Growth, INDEX_NONE, Cache.CellSize, false);
	Pipeline.Advance(Cache, PublishedBounds, bFull, true);
	TestTrue(TEXT("Empty whole document clears all coverage"), bFull && Cache.Cells.IsEmpty() && !Cache.LocalBounds.bIsValid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshErasePreviewBatchTest,
	"ComposableCameraSystem.Editor.MeshCamera.ErasePreviewBatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FComposableCameraMeshErasePreviewBatchTest::RunTest(const FString&)
{
	using namespace UE::ComposableCamera::MeshEditor;
	TArray<FComposableCameraMeshLayerDefinition> Layers; Layers.SetNum(2);
	for (auto& Layer : Layers) { Layer.LayerId = FGuid::NewGuid(); }
	FComposableCameraMeshSurfaceAuthoringData Data;
	for (int32 Layer = 0; Layer < 2; ++Layer)
	{
		AddMeshVisualizationTriangle(Data, Layers[Layer].LayerId, Layer == 0 ? 0 : -4);
		AddMeshVisualizationTriangle(Data, Layers[Layer].LayerId, Layer == 0 ? 0 : -4, FVector2D(10000, 0));
	}
	FMeshLayerAuthoringIndex Index; Index.Build(Data);
	FResolvedSurfaceVisualization Cache; BuildAuthoringVisualization(Data, Layers, Cache); auto Reference = Cache;
	FMeshLayerStrokeCoverage Pipeline; Pipeline.EnablePreparedPreview();
	int32 ExpectedUpdates = 0;
	auto QueueCut = [&](const FVector& Center)
	{
		FComposableCameraMeshEraseStamp Stamp; Stamp.Center = Center; Stamp.Radius = 3; Stamp.Depth = 10;
		FBox2D Dirty(ForceInit);
		if (!EraseShapeGeometry(Data, Layers[0].LayerId, Stamp, false, nullptr, &Dirty, &Index)) { return false; }
		UpdateAuthoringVisualization(Data, Layers, Dirty, Reference, nullptr, &Index);
		Pipeline.Queue(Data, Layers, Index, Dirty, INDEX_NONE, Cache.CellSize, false); ++ExpectedUpdates; return true;
	};
	for (int32 Cut = 0; Cut < 8; ++Cut)
	{
		if (!TestTrue(TEXT("Each accepted Erase changes its exact source"), QueueCut(FVector(15 + Cut * 6, 20, 0)))) { return false; }
		if (Cut == 3)
		{
			const int32 First = Data.TriangleLayerIds.Num();
			AddExactVisualizationTriangle(Data, Layers[0].LayerId, FVector(10, 10, 2), FVector(60, 10, 3), FVector(10, 60, 2));
			Index.Append(Data, First); const FBox2D Dirty(FVector2D(10, 10), FVector2D(60, 60));
			AppendAuthoringVisualization(Data, Layers, First, Dirty, Reference, nullptr, &Index);
			Pipeline.Queue(Data, Layers, Index, Dirty, First, Cache.CellSize, false); ++ExpectedUpdates;
		}
	}
	if (!TestTrue(TEXT("Separate distant region changes independently"), QueueCut(FVector(10020, 20, 0)))) { return false; }
	const auto CompletedSource = Data;
	Data.Reset(); Index.Reset(); // Workers must own every completed input before launch.
	FBox2D Changed(ForceInit); bool bFull = false;
	TMap<FIntPoint, FPreparedEditPreviewTile> Published;
	int32 PublishedTiles = 0;
	int32 CompletedUpdates = 0, PreparedTileCount = 0;
	bool bExpectFull = false;
	auto Drain = [&]()
	{
		for (int32 Attempt = 0; Attempt < 256 && Pipeline.HasPending(); ++Attempt)
		{
			FPreparedEditPreview Prepared; bool bPreparedFull = false;
			while (Pipeline.TakePrepared(Prepared, bPreparedFull))
			{
				TestEqual(TEXT("Publication follows the local/full replacement policy"), bPreparedFull, bExpectFull);
				for (auto& Tile : Prepared.Tiles)
				{
					++PublishedTiles;
					TestTrue(TEXT("Coverage worker supplies immutable geometry and precomputed bounds"), Tile.Geometry.IsEmpty());
					for (const auto& Pair : Tile.SharedGeometry)
					{
						FBox Bounds(ForceInit); for (const auto& Vertex : Pair.Value.Geometry->Vertices) { Bounds += FVector(Vertex.Position); }
						TestTrue(TEXT("Shared geometry has exact native vertex bounds and current-Layer color policy"), Bounds.IsValid
							&& Pair.Value.Geometry->LocalBounds.Origin.Equals(Bounds.GetCenter())
							&& Pair.Value.Geometry->LocalBounds.BoxExtent.Equals(Bounds.GetExtent()) && Pair.Value.bUseLayerColor);
					}
					Published.Add(Tile.Tile, MoveTemp(Tile));
				}
			}
			if (Pipeline.Advance(Cache, Changed, bFull, true))
			{ CompletedUpdates += Pipeline.GetLastCoverageUpdateCount(); PreparedTileCount += Pipeline.GetLastPreparedTileCount(); }
		}
		return !Pipeline.HasPending();
	};
	TestTrue(TEXT("Owned batch drains with every coverage input intact"), Drain());
	TestEqual(TEXT("Mixed Erase/Brush coverage still executes every operation"), CompletedUpdates, ExpectedUpdates);
	TestEqual(TEXT("Bounded batches assemble repeated regions once per batch, without filling gaps"), PreparedTileCount, 3);
	TestEqual(TEXT("Long backlogs publish a completed prefix and the final near/far states"), PublishedTiles, 3);
	TestTrue(TEXT("Native ordered result equals independent incremental resolution"), SameResolvedVisualization(Cache, Reference));
	const FIntPoint FarTile(FMath::FloorToInt(10020 / (Reference.CellSize * EditPreviewTileCells)), 0), EmptyTile(-40, 0);
	const TArray<FIntPoint> RequestedTiles = {FIntPoint(0, 0), FarTile, FIntPoint(0, 0), EmptyTile};
	const auto Expected = PrepareEditPreviewTiles(Reference, Layers, RequestedTiles, FVector2D::ZeroVector, nullptr, false);
	TestEqual(TEXT("Explicit regions deduplicate keys and retain empty removals without filling gaps"), Expected.Tiles.Num(), 3);
	for (const auto& Tile : Expected.Tiles)
	{
		if (Tile.Tile == EmptyTile) { TestTrue(TEXT("Explicit empty region has no invented fill"), Tile.Geometry.IsEmpty()); continue; }
		const auto* Actual = Published.Find(Tile.Tile);
		if (!TestNotNull(TEXT("Every independent occupied region was published"), Actual)) { continue; }
		for (const auto& Pair : Tile.Geometry)
		{
			const auto* Shared = Actual->SharedGeometry.Find(Pair.Key);
			if (!TestNotNull(TEXT("Every exact Layer buffer survives display coalescing"), Shared)) { continue; }
			TestTrue(TEXT("Native fan indices and vertex counts equal independent output"), Shared->Geometry->Indices == Pair.Value.Indices
				&& Shared->Geometry->Vertices.Num() == Pair.Value.Vertices.Num());
			if (Shared->Geometry->Vertices.Num() != Pair.Value.Vertices.Num()) { continue; }
			for (int32 Vertex = 0; Vertex < Pair.Value.Vertices.Num(); ++Vertex)
			{
				const auto& A = Shared->Geometry->Vertices[Vertex]; const auto& B = Pair.Value.Vertices[Vertex];
				TestTrue(TEXT("Final position, normal and tangent stay exact"), A.Position == B.Position && A.TangentX == B.TangentX && A.TangentZ == B.TangentZ);
			}
		}
	}
	// Empty local input must still publish its removal key, without a full replacement.
	auto FarOnly = CompletedSource; Index.Build(FarOnly);
	FComposableCameraMeshEraseStamp RemoveNear; RemoveNear.Center = FVector(50, 50, 0); RemoveNear.Radius = 500; RemoveNear.Depth = 20;
	for (const auto& Layer : Layers)
	{
		if (!TestTrue(TEXT("Remove only near source while retaining the exact distant cuts"),
			EraseShapeGeometry(FarOnly, Layer.LayerId, RemoveNear, false, nullptr, nullptr, &Index))) { return false; }
	}
	const FBox2D Removed(FVector2D(0, 0), FVector2D(100, 100));
	UpdateAuthoringVisualization(FarOnly, Layers, Removed, Reference, nullptr, &Index);
	Pipeline.Queue(FarOnly, Layers, Index, Removed, INDEX_NONE, Cache.CellSize, false);
	Published.Reset(); PublishedTiles = 0; TestTrue(TEXT("Empty local removal drains"), Drain());
	TestTrue(TEXT("An empty prepared key clears near fill without touching distant fill"), PublishedTiles == 1
		&& Published.Contains(FIntPoint(0, 0)) && Published.FindChecked(FIntPoint(0, 0)).SharedGeometry.IsEmpty()
		&& SameResolvedVisualization(Cache, Reference));
	Pipeline.Queue(FarOnly, Layers, Index, Removed, INDEX_NONE, Cache.CellSize, false); Pipeline.Cancel();
	TestFalse(TEXT("Cancellation drops coalesced display and source-derived work"), Pipeline.HasPending());
	// A full rebuild followed by local changes emits only the latest complete document.
	BuildAuthoringVisualization(FarOnly, Layers, Reference);
	const FBox2D FarBounds(FVector2D(10000, 0), FVector2D(10100, 100));
	Pipeline.Queue(FarOnly, Layers, Index, FarBounds, INDEX_NONE, Cache.CellSize, true);
	FComposableCameraMeshEraseStamp FinalCut; FinalCut.Center = FVector(10040, 20, 0); FinalCut.Radius = 3; FinalCut.Depth = 10;
	FBox2D FinalDirty(ForceInit);
	if (!TestTrue(TEXT("Local source changes after the queued full snapshot"), EraseShapeGeometry(FarOnly, Layers[0].LayerId,
		FinalCut, false, nullptr, &FinalDirty, &Index))) { return false; }
	UpdateAuthoringVisualization(FarOnly, Layers, FinalDirty, Reference, nullptr, &Index);
	Pipeline.Queue(FarOnly, Layers, Index, FinalDirty, INDEX_NONE, Cache.CellSize, false);
	Published.Reset(); PublishedTiles = 0; bExpectFull = true;
	TestTrue(TEXT("Full and subsequent local inputs drain together"), Drain());
	TestTrue(TEXT("Only the latest whole state publishes after every exact input"), bFull && PublishedTiles == 1
		&& Pipeline.GetLastPreparedTileCount() == 1 && Pipeline.GetLastCoverageUpdateCount() == 2
		&& SameResolvedVisualization(Cache, Reference));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshRegionalSnapshotFilterTest,
	"ComposableCameraSystem.Editor.MeshCamera.RegionalSnapshotFiltering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FComposableCameraMeshRegionalSnapshotFilterTest::RunTest(const FString&)
{
	using namespace UE::ComposableCamera::MeshEditor;
	TArray<FComposableCameraMeshLayerDefinition> Layers; Layers.SetNum(3);
	for (auto& Layer : Layers) { Layer.LayerId = FGuid::NewGuid(); } Layers[2].bEnabled = false;
	FComposableCameraMeshSurfaceAuthoringData Data;
	AddMeshVisualizationTriangle(Data, Layers[0].LayerId);
	AddMeshVisualizationTriangle(Data, Layers[1].LayerId, -4);
	AddMeshVisualizationTriangle(Data, Layers[2].LayerId);
	AddMeshVisualizationTriangle(Data, Layers[0].LayerId, 0, FVector2D(1000, 0));
	FMeshLayerAuthoringIndex Index; Index.Build(Data);
	FResolvedSurfaceVisualization Cache; BuildAuthoringVisualization(Data, Layers, Cache); const auto Reference = Cache;
	const FBox2D Dirty(FVector2D(12, 12), FVector2D(18, 18));
	FMeshLayerStrokeCoverage Pipeline; Pipeline.Queue(Data, Layers, Index, Dirty, INDEX_NONE, Cache.CellSize, false);
	Data.Reset(); Index.Reset(); FBox2D Changed(ForceInit); bool bFull = false;
	TestTrue(TEXT("Regional snapshot owns exact local input after live source disappears"), Pipeline.Advance(Cache, Changed, bFull, true));
	TestEqual(TEXT("Mixed 128-triangle leaf copies only the two enabled local floors"), Pipeline.GetLastSnapshotTriangleCount(), 2);
	TestTrue(TEXT("Fine filtering retains complete edge cells, lower floors and distant coverage"), !bFull && SameResolvedVisualization(Cache, Reference));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshAsyncDocumentBuildTest,
	"ComposableCameraSystem.Editor.MeshCamera.AsyncDocumentPreviewBuild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshAsyncDocumentBuildTest::RunTest(const FString&)
{
	using namespace UE::ComposableCamera::MeshEditor;
	TArray<FComposableCameraMeshLayerDefinition> Layers; Layers.SetNum(3);
	for (auto& Layer : Layers) { Layer.LayerId = FGuid::NewGuid(); }
	Layers[2].bEnabled = false;
	FComposableCameraMeshSurfaceAuthoringData Data;
	AddExactVisualizationTriangle(Data, Layers[0].LayerId, FVector(-400, 0, 0), FVector(-200, 0, 15), FVector(-400, 200, 30));
	AddExactVisualizationTriangle(Data, Layers[0].LayerId, FVector(-400, 0, 2), FVector(-200, 0, 17), FVector(-400, 200, 32));
	AddExactVisualizationTriangle(Data, Layers[1].LayerId, FVector(-380, 20, 10), FVector(-300, 20, 16), FVector(-380, 100, 22));
	AddExactVisualizationTriangle(Data, Layers[1].LayerId, FVector(-400, 0, 400), FVector(-200, 0, 415), FVector(-400, 200, 430));
	AddMeshVisualizationTriangle(Data, Layers[0].LayerId, 20, FVector2D(1200, 0));
	AddMeshVisualizationTriangle(Data, Layers[2].LayerId, 50, FVector2D(100000, 0));
	FResolvedSurfaceVisualization Expected; BuildAuthoringVisualization(Data, Layers, Expected);
	FMeshLayerDocumentBuild Build; const FGuid Revision = FGuid::NewGuid();
	FResolvedSurfaceVisualization Retired = Expected;
	Build.Start(Data, Layers, Revision, &Retired);
	TestTrue(TEXT("Start schedules one owned job and retires old coverage without resolving or publishing"), Build.HasPending() && Retired.Cells.IsEmpty());
	const auto Original = Data;
	Data.Reset(); Layers.Reset(); // The worker must have no live source/Layer references.
	FDocumentPreviewResult Result;
	TestFalse(TEXT("Terminal result waits for already published final regions to be consumed"), Build.Take(Revision, Result, true));
	FPreparedEditPreview Tile; int32 Regions = 0;
	while (Build.TakeTile(Revision, Tile))
	{
		++Regions;
		TestTrue(TEXT("Each final region is available while the document job remains attached"), Build.HasPending()
			&& Tile.CellSize == Expected.CellSize && !Tile.Tiles.IsEmpty());
	}
	if (!TestTrue(TEXT("Explicit automation wait consumes the original snapshot after all ready regions"), Build.Take(Revision, Result, true))) { return false; }
	TestTrue(TEXT("Background full build preserves slopes, same-Layer winners, priority, stacked floors and disabled bounds"),
		SameResolvedVisualization(Result.Visualization, Expected));
	TestTrue(TEXT("Returned broad phase retains original counts and bounds"), Result.Index.IsCurrent(Original)
		&& Regions > 1 && !Build.HasPending());
	// A remembered exact display needs only coverage/index rebuilding. No partial
	// mesh may replace it, and no second all-document upload is needed afterward.
	TArray<FComposableCameraMeshLayerDefinition> CacheLayers; CacheLayers.SetNum(3);
	CacheLayers[0].LayerId = Original.TriangleLayerIds[0]; CacheLayers[1].LayerId = Original.TriangleLayerIds[2];
	CacheLayers[2].LayerId = Original.TriangleLayerIds.Last(); CacheLayers[2].bEnabled = false;
	Build.Start(Original, CacheLayers, Revision, nullptr, FVector2D::ZeroVector, false);
	TestTrue(TEXT("Cache-only rebuild completes without consuming preview tiles"), Build.Take(Revision, Result, true)
		&& !Build.TakeTile(Revision, Tile) && Result.Index.IsCurrent(Original) && SameResolvedVisualization(Result.Visualization, Expected));
	Layers.AddDefaulted(); Layers[0].LayerId = FGuid::NewGuid();
	Build.Start(Data, Layers, Revision);
	TestFalse(TEXT("A different restored revision rejects even a ready result"), Build.Take(FGuid::NewGuid(), Result, true));
	TestFalse(TEXT("Rejected revision leaves no pending generation"), Build.HasPending());
	Build.Start(Original, Layers, Revision);
	TestFalse(TEXT("A mismatched revision rejects early regions too"), Build.TakeTile(FGuid::NewGuid(), Tile));
	TestFalse(TEXT("Rejected early regions detach the whole generation"), Build.HasPending());
	Build.Start(Original, Layers, Revision); Build.Cancel();
	TestFalse(TEXT("Cancellation cannot publish old coverage"), Build.Take(Revision, Result, true));
	Build.Start(Data, Layers, Revision);
	TestTrue(TEXT("Empty document resolves and completes normally"), Build.Take(Revision, Result, true)
		&& Result.Visualization.Cells.IsEmpty() && !Build.TakeTile(Revision, Tile) && Result.Index.IsCurrent(Data));
	return true;
}

#endif
