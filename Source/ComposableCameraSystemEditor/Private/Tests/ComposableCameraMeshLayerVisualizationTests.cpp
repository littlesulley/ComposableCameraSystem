// Copyright 2026 Sulley. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "MeshCamera/ComposableCameraMeshLayerRendering.h"
#include "MeshCamera/ComposableCameraMeshSurfaceTypes.h"
#include "Misc/AutomationTest.h"

namespace
{
	void AddMeshVisualizationTriangle(
		FComposableCameraMeshSurfaceAuthoringData& Data,
		const FGuid& LayerId,
		float Height = 0.0f)
	{
		const int32 FirstVertex = Data.Vertices.Add(FVector3f(0.0f, 0.0f, Height));
		Data.Vertices.Add(FVector3f(100.0f, 0.0f, Height));
		Data.Vertices.Add(FVector3f(0.0f, 100.0f, Height));
		Data.Indices.Add(FirstVertex);
		Data.Indices.Add(FirstVertex + 1);
		Data.Indices.Add(FirstVertex + 2);
		Data.TriangleLayerIds.Add(LayerId);
	}
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
		TestEqual(TEXT("Each resolved cell becomes one non-overlapping quad"),
			OrderedMeshes[0].LocalVertices.Num(),
			OrderedVisualization.Cells.Num() * 4);
		TestEqual(TEXT("Each resolved cell becomes two mesh triangles"),
			OrderedMeshes[0].Indices.Num(),
			OrderedVisualization.Cells.Num() * 6);
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
