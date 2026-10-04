// Copyright 2026 Sulley. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "MeshCamera/ComposableCameraMeshLayerRendering.h"
#include "MeshCamera/ComposableCameraMeshLayerShapes.h"
#include "MeshCamera/ComposableCameraMeshSurfaceTypes.h"
#include "Misc/AutomationTest.h"

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
						return FMath::Abs(Cell.LocalPosition.Z - Other.LocalPosition.Z) <= 1.e-3
							&& SameVisualizationCoverage(Cell, Other);
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

#endif
