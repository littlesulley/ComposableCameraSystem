// Copyright 2026 Sulley. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "MeshCamera/ComposableCameraMeshLayerEdMode.h"
#include "MeshCamera/ComposableCameraMeshLayerShapes.h"
#include "MeshCamera/ComposableCameraMeshLayerToolSettings.h"
#include "MeshCamera/ComposableCameraMeshLayerEditPreview.h"
#include "MeshCamera/ComposableCameraMeshLayerAuthoringIndex.h"
#include "MeshCamera/ComposableCameraMeshLayerStrokeCoverage.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

namespace
{
	class FStrokeFixtureModeTools final : public FEditorModeTools
	{
	public:
		explicit FStrokeFixtureModeTools(UWorld& World) : FixtureWorld(&World) {}
		virtual UWorld* GetWorld() const override { return FixtureWorld.Get(); }
	private:
		TWeakObjectPtr<UWorld> FixtureWorld;
	};
	double StrokePreviewArea(const UE::ComposableCamera::MeshEditor::FMeshLayerEditPreview& Preview)
	{
		double Area = 0; TArray<UComposableCameraMeshLayerEditPreviewComponent*> Components;
		if (Preview.GetActor()) { Preview.GetActor()->GetComponents(Components); }
		for (const auto* Component : Components)
		{
			const auto Vertices = Component->GetVertices(); const auto Indices = Component->GetIndices();
			for (int32 Index = 0; Index < Indices.Num(); Index += 3)
			{
				Area += FMath::Abs(FVector3f::CrossProduct(Vertices[Indices[Index + 1]].Position - Vertices[Indices[Index]].Position,
					Vertices[Indices[Index + 2]].Position - Vertices[Indices[Index]].Position).Z) * 0.5;
			}
		}
		return Area;
	}
	bool StrokePreviewHeight(const UE::ComposableCamera::MeshEditor::FMeshLayerEditPreview& Preview, const FVector2D& Point, double& OutHeight)
	{
		bool bFound = false; OutHeight = -TNumericLimits<double>::Max();
		TArray<UComposableCameraMeshLayerEditPreviewComponent*> Components;
		if (Preview.GetActor()) { Preview.GetActor()->GetComponents(Components); }
		for (const auto* Component : Components)
		{
			const auto Vertices = Component->GetVertices(); const auto Indices = Component->GetIndices();
			for (int32 Index = 0; Index + 2 < Indices.Num(); Index += 3)
			{
				const FVector A(Vertices[Indices[Index]].Position), B(Vertices[Indices[Index + 1]].Position), C(Vertices[Indices[Index + 2]].Position);
				const double Denominator = (B.Y - C.Y) * (A.X - C.X) + (C.X - B.X) * (A.Y - C.Y);
				if (FMath::Abs(Denominator) < 1.e-10) { continue; }
				const double WA = ((B.Y - C.Y) * (Point.X - C.X) + (C.X - B.X) * (Point.Y - C.Y)) / Denominator;
				const double WB = ((C.Y - A.Y) * (Point.X - C.X) + (A.X - C.X) * (Point.Y - C.Y)) / Denominator;
				const double WC = 1 - WA - WB;
				if (WA >= -1.e-6 && WB >= -1.e-6 && WC >= -1.e-6)
				{
					OutHeight = FMath::Max(OutHeight, WA * A.Z + WB * B.Z + WC * C.Z); bFound = true;
				}
			}
		}
		return bFound;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshContinuousEraseTest,
	"ComposableCameraSystem.Editor.MeshCamera.ContinuousErasePreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FComposableCameraMeshContinuousEraseTest::RunTest(const FString&)
{
	if (!TestNotNull(TEXT("Editor exists"), GEditor) || GEditor->IsTransactionActive()) { return false; }
	using namespace UE::ComposableCamera::MeshEditor;
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	if (!TestNotNull(TEXT("Erase fixture exists"), World)) { return false; }
	const bool bResult = [this, World]()
	{
		FStrokeFixtureModeTools Owner(*World); FComposableCameraMeshLayerEdMode Mode; Mode.Owner = &Owner;
		Mode.TargetLevel = World->PersistentLevel;
		Mode.Settings = NewObject<UComposableCameraMeshLayerToolSettings>(GetTransientPackage(), NAME_None, RF_Transactional);
		Mode.Settings->Layers.SetNum(2); Mode.Settings->NormalizeLayers();
		Mode.Settings->Tool = EComposableCameraMeshDrawTool::Erase; Mode.Settings->BrushRadius = 18; Mode.Settings->ProjectionDistance = 10;
		auto& Data = Mode.Settings->WorkingData;
		for (int32 Layer = 0; Layer < 2; ++Layer) { for (int32 Y = 0; Y < 8; ++Y) { for (int32 X = 0; X < 8; ++X)
		{
			const int32 First = Data.Vertices.Num(); const float Z = Layer == 0 ? 0 : -4;
			Data.Vertices.Append({FVector3f(X * 20, Y * 20, Z), FVector3f((X + 1) * 20, Y * 20, Z),
				FVector3f((X + 1) * 20, (Y + 1) * 20, Z), FVector3f(X * 20, (Y + 1) * 20, Z)});
			Data.Indices.Append({First, First + 1, First + 2, First, First + 2, First + 3});
			Data.TriangleLayerIds.Append({Mode.Settings->Layers[Layer].LayerId, Mode.Settings->Layers[Layer].LayerId});
		} } }
		const auto Original = Data;
		Mode.Settings->TouchDocument(); Mode.SavedRevision = Mode.Settings->DocumentRevision;
		Mode.AuthoringIndex->Build(Data); BuildAuthoringVisualization(Data, Mode.Settings->Layers, Mode.Visualization);
		Mode.bVisualizationDirty = false;
		Mode.bEditPreviewDirty = !Mode.EditPreview->Update(*World->PersistentLevel, FTransform::Identity, Mode.Visualization, Mode.Settings->Layers);
		if (!TestFalse(TEXT("Initial exact display is ready"), Mode.bEditPreviewDirty)) { return false; }
		Mode.PreviewRevision = Mode.Settings->DocumentRevision;
		Mode.bHasHoverHit = true; Mode.HoverHit.ImpactNormal = FVector::UpVector;
		Mode.BeginStroke(); Mode.HoverHit.ImpactPoint = FVector(-1000, -1000, 0); Mode.QueuePaintAtHover(nullptr);
		Mode.AdvancePainting(MAX_int32, 0.0);
		TestTrue(TEXT("Empty indexed erase needs no extra whole-source rollback snapshot"), !Mode.bStrokeSourceSnapshot
			&& Data.Vertices == Original.Vertices && Data.Indices == Original.Indices && !Mode.StrokeCoverage->HasPending());
		Mode.FinishStroke(false, true);

		Mode.BeginStroke(); Mode.CaptureStrokeSourceBeforeErase();
		FComposableCameraMeshEraseStamp FirstCut; FirstCut.Center = FVector(25, 25, 0); FirstCut.Radius = 18; FirstCut.Depth = 2;
		FBox2D Dirty(ForceInit);
		if (!TestTrue(TEXT("First complete source cut changes the upper floor"), EraseShapeGeometry(Data, Mode.Settings->Layers[0].LayerId,
			FirstCut, false, nullptr, &Dirty, Mode.AuthoringIndex.Get()))) { Mode.FinishStroke(true); return false; }
		Mode.Settings->TouchDocument(); const FGuid CompletedRevision = Mode.Settings->DocumentRevision;
		const auto Completed = Data; const auto CompletedIndex = *Mode.AuthoringIndex;
		Mode.Settings->ProjectionDistance = 2; Mode.HoverHit.ImpactPoint = FVector(145, 145, 0); Mode.QueuePaintAtHover(nullptr);
		// Withhold derived advancement while producing the next partial mutation.
		Mode.AdvancePainting(1, 0.0, false);
		if (!TestTrue(TEXT("Mouse remains held with another Erase unfinished"), Mode.bPainting && !!Mode.StrokeTask
			&& Data.Indices != Completed.Indices && Mode.Settings->DocumentRevision == CompletedRevision)) { Mode.FinishStroke(true); return false; }
		const auto Partial = Data;
		Mode.StrokeCoverage->Queue(Completed, Mode.Settings->Layers, CompletedIndex, Dirty, INDEX_NONE, Mode.GetVisualization().CellSize, false);
		Mode.AdvanceStrokeCoverage(false);
		TestTrue(TEXT("Completed coverage starts despite the active partial Erase"), Mode.StrokeCoverage->GetLastSnapshotTriangleCount() > 0);
		Mode.AdvanceStrokeCoverage(true);
		Mode.EditPreview->AdvanceQueuedUpdates(*World->PersistentLevel, FTransform::Identity, Mode.Settings->Layers, 0);
		FResolvedSurfaceVisualization Expected; BuildAuthoringVisualization(Completed, Mode.Settings->Layers, Expected);
		FMeshLayerEditPreview Reference;
		if (!TestTrue(TEXT("Independent completed-source preview exists"), Reference.Update(*World->PersistentLevel, FTransform::Identity, Expected, Mode.Settings->Layers)))
		{ Mode.FinishStroke(true); return false; }
		TestTrue(TEXT("The completed cut is displayed before finishing the next source task"), !!Mode.StrokeTask && Mode.bPainting
			&& !Mode.StrokeCoverage->HasPending() && FMath::IsNearlyEqual(StrokePreviewArea(*Mode.EditPreview), StrokePreviewArea(Reference), 0.1));
		double FirstHeight = 0, NextHeight = 0;
		TestTrue(TEXT("Completed erasure exposes the lower floor while the next partial cut stays invisible"),
			StrokePreviewHeight(*Mode.EditPreview, FVector2D(25, 25), FirstHeight)
			&& StrokePreviewHeight(*Mode.EditPreview, FVector2D(145, 145), NextHeight)
			&& FMath::IsNearlyEqual(FirstHeight, -4 + VisualizationSurfaceOffset, 1.e-3)
			&& FMath::IsNearlyEqual(NextHeight, VisualizationSurfaceOffset, 1.e-3));
		TestTrue(TEXT("Coverage reads immutable completed input without touching partial source or its revision"), Data.Vertices == Partial.Vertices
			&& Data.Indices == Partial.Indices && Data.TriangleLayerIds == Partial.TriangleLayerIds
			&& Data.TriangleShapeIds == Partial.TriangleShapeIds && Mode.Settings->DocumentRevision == CompletedRevision);
		int32 LowerTriangles = 0;
		for (const FGuid& LayerId : Data.TriangleLayerIds) { LowerTriangles += LayerId == Mode.Settings->Layers[1].LayerId ? 1 : 0; }
		TestEqual(TEXT("Lower floor remains intact in the source"), LowerTriangles, 128);
		Mode.FinishStroke(true);
		TestTrue(TEXT("Cancel restores the entire held stroke and revokes its derived work"), Data.Vertices == Original.Vertices && Data.Indices == Original.Indices
			&& Mode.Settings->DocumentRevision == Mode.SavedRevision && !Mode.StrokeCoverage->HasPending()
			&& !Mode.EditPreview->HasQueuedUpdates() && !GEditor->IsTransactionActive());
		Mode.Owner = nullptr; return true;
	}();
	World->DestroyWorld(false); return bResult;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshBudgetedStrokeTest,
	"ComposableCameraSystem.Editor.MeshCamera.BudgetedStroke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshBudgetedStrokeTest::RunTest(const FString&)
{
	if (!TestNotNull(TEXT("Editor transactions available"), GEditor)
		|| !TestFalse(TEXT("No unrelated transaction is active"), GEditor->IsTransactionActive())) { return false; }
	using namespace UE::ComposableCamera::MeshEditor;
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	if (!TestNotNull(TEXT("Fixture World exists"), World)) { return false; }
	const bool bResult = [this, World]()
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		AActor* Floor = World->SpawnActor<AActor>();
		if (!TestNotNull(TEXT("Floor mesh loads"), Cube) || !TestNotNull(TEXT("Floor exists"), Floor)) { return false; }
		auto* Mesh = NewObject<UStaticMeshComponent>(Floor);
		Floor->AddInstanceComponent(Mesh); Floor->SetRootComponent(Mesh);
		Mesh->SetMobility(EComponentMobility::Movable); Mesh->SetStaticMesh(Cube);
		Mesh->SetRelativeScale3D(FVector(10, 10, 0.1)); Floor->SetActorLocation(FVector(0, 0, -Cube->GetBoundingBox().Max.Z * 0.1));
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
		Mesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Block); Mesh->RegisterComponent();
		FStrokeFixtureModeTools Owner(*World);
		FComposableCameraMeshLayerEdMode Mode, Reference;
		Mode.Owner = Reference.Owner = &Owner;
		Mode.TargetLevel = Reference.TargetLevel = World->PersistentLevel;
		Mode.Settings = NewObject<UComposableCameraMeshLayerToolSettings>(GetTransientPackage(), NAME_None, RF_Transactional);
		Reference.Settings = NewObject<UComposableCameraMeshLayerToolSettings>();
		Mode.Settings->Layers.AddDefaulted(); Mode.Settings->NormalizeLayers(); Mode.Settings->Layers[0].TraceChannel = ECC_Camera;
		Reference.Settings->Layers = Mode.Settings->Layers;
		Mode.Settings->BrushRadius = Reference.Settings->BrushRadius = 45.0;
		Mode.Settings->ProjectionDistance = Reference.Settings->ProjectionDistance = 100.0;
		Mode.Settings->TouchDocument(); Mode.SavedRevision = Mode.Settings->DocumentRevision;
		Mode.bHasHoverHit = true; Mode.HoverHit.ImpactNormal = FVector::UpVector;
		Mode.BeginStroke();
		const FGuid Layer = Mode.Settings->GetActiveLayerId();
		for (int32 Point = 0; Point < 3; ++Point)
		{
			Mode.Settings->Tool = Point == 1 ? EComposableCameraMeshDrawTool::Erase : EComposableCameraMeshDrawTool::Brush;
			Mode.HoverHit.ImpactPoint = FVector(-60.0 + Point * 60.0, 0, 0);
			Mode.QueuePaintAtHover(nullptr);
			if (Point == 1) { Reference.EraseBrushStamp(Mode.HoverHit, Layer); }
			else { TestTrue(TEXT("Reference Brush hits a custom-channel StaticMesh"), Reference.AddProjectedBrushStamp(Mode.HoverHit, Layer)); }
		}
		Mode.HoverHit.ImpactPoint.X += 1.0; Mode.QueuePaintAtHover(nullptr);
		TestEqual(TEXT("Existing spacing filters nearby input without losing accepted points"), Mode.QueuedStamps.Num(), 3);
		TestTrue(TEXT("Mouse callbacks queue work instead of mutating geometry"), Mode.Settings->WorkingData.Indices.IsEmpty());
		Mode.Settings->BrushRadius = 5.0; // Every queued sample must retain its original options.
		Mode.InputKey(nullptr, nullptr, EKeys::LeftMouseButton, IE_Released);
		TestTrue(TEXT("Release retains its unfinished stroke and transaction"), Mode.bStrokeReleased && Mode.bPainting && GEditor->IsTransactionActive());
		for (int32 Step = 0; Step < 30000 && Mode.bPainting; ++Step) { Mode.AdvancePainting(1, 0.0); }
		TestFalse(TEXT("All small-budget steps eventually finish"), Mode.bPainting);
		TestFalse(TEXT("Released work closes its one transaction"), GEditor->IsTransactionActive());
		TestTrue(TEXT("Queued Brush/Erase source equals the original synchronous source exactly"),
			Mode.Settings->WorkingData.Vertices == Reference.Settings->WorkingData.Vertices
			&& Mode.Settings->WorkingData.Indices == Reference.Settings->WorkingData.Indices
			&& Mode.Settings->WorkingData.TriangleLayerIds == Reference.Settings->WorkingData.TriangleLayerIds
			&& Mode.Settings->WorkingData.TriangleShapeIds == Reference.Settings->WorkingData.TriangleShapeIds);
		TestTrue(TEXT("Committed stroke is dirty with current caches"), Mode.IsDirty() && !Mode.bVisualizationDirty
			&& Mode.AuthoringIndex->IsCurrent(Mode.Settings->WorkingData) && Mode.EditPreview->IsReadyFor(World->PersistentLevel));
		GEditor->RegisterForUndo(&Mode);
		GEditor->UndoTransaction();
		TestTrue(TEXT("One Undo removes the entire queued mixed stroke"), Mode.Settings->WorkingData.Indices.IsEmpty() && !Mode.IsDirty());
		GEditor->RedoTransaction();
		TestTrue(TEXT("Redo restores every queued stamp exactly"), Mode.Settings->WorkingData.Vertices == Reference.Settings->WorkingData.Vertices
			&& Mode.Settings->WorkingData.Indices == Reference.Settings->WorkingData.Indices && Mode.IsDirty());
		GEditor->UnregisterForUndo(&Mode);
		const auto BeforeCancel = Mode.Settings->WorkingData; const FGuid Revision = Mode.Settings->DocumentRevision;
		Mode.Settings->Tool = EComposableCameraMeshDrawTool::Erase; Mode.Settings->BrushRadius = 25.0;
		Mode.BeginStroke(); Mode.HoverHit.ImpactPoint = FVector(-60, 0, 0); Mode.QueuePaintAtHover(nullptr);
		for (int32 Step = 0; Step < 2000 && Mode.Settings->WorkingData.Indices == BeforeCancel.Indices; ++Step) { Mode.AdvancePainting(1, 0.0); }
		TestTrue(TEXT("Cancellation fixture reached a partial source mutation"), Mode.Settings->WorkingData.Indices != BeforeCancel.Indices);
		Mode.FinishStroke(true);
		TestTrue(TEXT("Cancellation restores exact pre-stroke source and revision"), Mode.Settings->WorkingData.Vertices == BeforeCancel.Vertices
			&& Mode.Settings->WorkingData.Indices == BeforeCancel.Indices && Mode.Settings->DocumentRevision == Revision);
		TestFalse(TEXT("Cancellation leaves no transaction"), GEditor->IsTransactionActive());
		Mode.BeginStroke(); Mode.QueuePaintAtHover(nullptr);
		Mode.FinishStroke();
		TestTrue(TEXT("Explicit boundaries flush pending stamps before returning"), !Mode.bPainting && !Mode.StrokeTask
			&& Mode.QueuedStamps.IsEmpty() && Mode.Settings->DocumentRevision != Revision && !GEditor->IsTransactionActive());
		Mode.Settings->OnBeforeEdit.BindRaw(&Mode, &FComposableCameraMeshLayerEdMode::ResetInteraction);
		Mode.Settings->OnLayerDataChanged.BindRaw(&Mode, &FComposableCameraMeshLayerEdMode::MarkLayerDataDirty);
		Mode.Settings->AddLayer(); Mode.Settings->SelectLayer(Layer);
		const auto BeforeReorder = Mode.Settings->WorkingData;
		Reference.Settings->WorkingData = BeforeReorder; Reference.Settings->BrushRadius = 25.0;
		Reference.AuthoringIndex->Reset();
		Mode.Settings->Tool = EComposableCameraMeshDrawTool::Brush;
		Mode.BeginStroke(); Mode.HoverHit.ImpactPoint = FVector(120, 80, 0); Mode.QueuePaintAtHover(nullptr);
		TestTrue(TEXT("Reorder reference stamp projects"), Reference.AddProjectedBrushStamp(Mode.HoverHit, Layer));
		Mode.InputKey(nullptr, nullptr, EKeys::LeftMouseButton, IE_Released);
		Mode.AdvancePainting(1, 0.0);
		TestTrue(TEXT("Reorder begins while native stroke stages remain pending"), Mode.bStrokeReleased && !!Mode.StrokeTask);
		TestTrue(TEXT("Layer reorder succeeds after flushing its queued stroke"), Mode.Settings->MoveActiveLayer(1));
		TestTrue(TEXT("Reorder keeps every queued source vertex before changing Layer indices"), !Mode.bPainting && !Mode.StrokeTask
			&& Mode.Settings->WorkingData.Vertices == Reference.Settings->WorkingData.Vertices
			&& Mode.Settings->WorkingData.Indices == Reference.Settings->WorkingData.Indices && !GEditor->IsTransactionActive());
		GEditor->RegisterForUndo(&Mode);
		GEditor->UndoTransaction();
		TestTrue(TEXT("Reorder Undo retains the completed stroke as a separate transaction"), Mode.Settings->Layers[0].LayerId == Layer
			&& Mode.Settings->WorkingData.Indices == Reference.Settings->WorkingData.Indices);
		GEditor->UndoTransaction();
		TestTrue(TEXT("Next Undo removes only that completed stroke"), Mode.Settings->WorkingData.Vertices == BeforeReorder.Vertices
			&& Mode.Settings->WorkingData.Indices == BeforeReorder.Indices && Mode.Settings->Layers.Num() == 2);
		GEditor->UnregisterForUndo(&Mode);
		Mode.Settings->OnBeforeEdit.Unbind(); Mode.Settings->OnLayerDataChanged.Unbind();
		Mode.Settings->BrushRadius = 150.0;
		Mode.BeginStroke(); Mode.HoverHit.ImpactPoint = FVector(200, 80, 0); Mode.QueuePaintAtHover(nullptr);
		Mode.AdvancePainting(MAX_int32, 1.0);
		TestTrue(TEXT("Finite source processing completes without waiting for background coverage"), !Mode.StrokeTask
			&& Mode.QueuedStamps.IsEmpty() && Mode.StrokeCoverage->HasPending());
		Mode.InputKey(nullptr, nullptr, EKeys::LeftMouseButton, IE_Released);
		TestTrue(TEXT("Coverage does not keep the completed source transaction open"), !Mode.bPainting
			&& Mode.StrokeCoverage->HasPending() && !GEditor->IsTransactionActive());
		Mode.BeginStroke();
		TestTrue(TEXT("Next press does not drain pending coverage"), Mode.bPainting && Mode.StrokeCoverage->HasPending());
		Mode.FinishStroke();
		TestTrue(TEXT("Explicit boundaries drain both coverage and fill"), !Mode.StrokeCoverage->HasPending()
			&& !Mode.EditPreview->HasQueuedUpdates() && !GEditor->IsTransactionActive());
		const auto BeforeAsyncErase = Mode.Settings->WorkingData;
		Mode.Settings->Tool = EComposableCameraMeshDrawTool::Erase;
		Mode.BeginStroke(); Mode.QueuePaintAtHover(nullptr); Mode.AdvancePainting(MAX_int32, 1.0);
		TestTrue(TEXT("Finite Erase changes source without a coverage barrier"), !Mode.StrokeTask
			&& Mode.Settings->WorkingData.Indices != BeforeAsyncErase.Indices && Mode.StrokeCoverage->HasPending());
		Mode.InputKey(nullptr, nullptr, EKeys::LeftMouseButton, IE_Released);
		TestTrue(TEXT("Erase release also closes source before background coverage"), !Mode.bPainting
			&& Mode.StrokeCoverage->HasPending() && !GEditor->IsTransactionActive());
		Mode.FinishStroke();
		BuildAuthoringVisualization(Mode.Settings->WorkingData, Mode.Settings->Layers, Mode.Visualization);
		Mode.bVisualizationDirty = false;
		Mode.EditPreview->QueueUpdate(*World->PersistentLevel, Mode.Visualization);
		Mode.bEditPreviewDirty = false;
		Mode.BeginStroke(); Mode.InputKey(nullptr, nullptr, EKeys::LeftMouseButton, IE_Released);
		TestTrue(TEXT("Source-complete release closes its transaction without waiting for derived fill"), !Mode.bPainting
			&& Mode.EditPreview->HasQueuedUpdates() && !GEditor->IsTransactionActive());
		Mode.BeginStroke();
		TestTrue(TEXT("A new stroke retains pending display work instead of flushing it synchronously"), Mode.bPainting && Mode.EditPreview->HasQueuedUpdates());
		Mode.FinishStroke();
		TestTrue(TEXT("Explicit boundaries still drain retained display work"), !Mode.bPainting && !Mode.EditPreview->HasQueuedUpdates()
			&& Mode.EditPreview->IsReadyFor(World->PersistentLevel) && !GEditor->IsTransactionActive());
		const auto BeforeUndo = Mode.Settings->WorkingData;
		Reference.Settings->WorkingData = BeforeUndo; Reference.Settings->Layers = Mode.Settings->Layers;
		Reference.Settings->BrushRadius = Mode.Settings->BrushRadius; Reference.AuthoringIndex->Reset();
		Mode.Settings->Tool = EComposableCameraMeshDrawTool::Brush;
		Mode.BeginStroke(); Mode.HoverHit.ImpactPoint = FVector(300, 0, 0); Mode.QueuePaintAtHover(nullptr);
		TestTrue(TEXT("Undo source reference projects the same accepted stamp"),
			Reference.AddProjectedBrushStamp(Mode.HoverHit, Mode.Settings->GetActiveLayerId()));
		Mode.AdvancePainting(1, 0.0, false);
		Mode.PrepareUndo();
		TestTrue(TEXT("Undo preparation finishes accepted source and closes its transaction without derived work"),
			Mode.Settings->WorkingData.Vertices == Reference.Settings->WorkingData.Vertices
			&& Mode.Settings->WorkingData.Indices == Reference.Settings->WorkingData.Indices
			&& !Mode.bPainting && !Mode.StrokeTask && !Mode.StrokeCoverage->HasPending() && !Mode.EditPreview->HasQueuedUpdates()
			&& Mode.bVisualizationDirty && !GEditor->IsTransactionActive());
		GEditor->RegisterForUndo(&Mode);
		GEditor->UndoTransaction();
		TestTrue(TEXT("Undo still restores the complete pre-stroke document"), Mode.Settings->WorkingData.Vertices == BeforeUndo.Vertices
			&& Mode.Settings->WorkingData.Indices == BeforeUndo.Indices);
		GEditor->RedoTransaction();
		TestTrue(TEXT("Redo retains every source sample after skipping obsolete preview"),
			Mode.Settings->WorkingData.Vertices == Reference.Settings->WorkingData.Vertices
			&& Mode.Settings->WorkingData.Indices == Reference.Settings->WorkingData.Indices);
		GEditor->UnregisterForUndo(&Mode);
		// Audit regression: a second press before any Tick must not turn the input
		// callback into an unlimited drain of the previously released stroke.
		Mode.bHasHoverHit = true; Mode.HoverHit.ImpactNormal = FVector::UpVector;
		Mode.Settings->Tool = EComposableCameraMeshDrawTool::Brush;
		Mode.BeginStroke();
		Mode.HoverHit.ImpactPoint = FVector(330, 100, 0); Mode.QueuePaintAtHover(nullptr);
		Mode.HoverHit.ImpactPoint = FVector(390, 100, 0); Mode.QueuePaintAtHover(nullptr);
		Mode.InputKey(nullptr, nullptr, EKeys::LeftMouseButton, IE_Released);
		TestTrue(TEXT("Rapid-press fixture has accepted source work awaiting Tick"), Mode.bPainting && Mode.bStrokeReleased
			&& Mode.QueuedStampIndex < Mode.QueuedStamps.Num());
		const int32 IndicesBeforePress = Mode.Settings->WorkingData.Indices.Num();
		Reference.Settings->WorkingData = Mode.Settings->WorkingData; Reference.AuthoringIndex->Reset();
		Reference.Settings->BrushRadius = Mode.Settings->BrushRadius;
		FHitResult ReferenceHit = Mode.HoverHit;
		ReferenceHit.ImpactPoint = FVector(330, 100, 0); Reference.AddProjectedBrushStamp(ReferenceHit, Layer);
		ReferenceHit.ImpactPoint = FVector(390, 100, 0); Reference.AddProjectedBrushStamp(ReferenceHit, Layer);
		const auto FirstRapidStroke = Reference.Settings->WorkingData;
		Mode.BeginStroke();
		TestEqual(TEXT("A rapid second press keeps source projection out of the mouse callback"),
			Mode.Settings->WorkingData.Indices.Num(), IndicesBeforePress);
		Mode.HoverHit.ImpactPoint = FVector(420, 180, 0); Mode.QueuePaintAtHover(nullptr);
		Reference.AddProjectedBrushStamp(Mode.HoverHit, Layer);
		Mode.InputKey(nullptr, nullptr, EKeys::LeftMouseButton, IE_Released);
		TestTrue(TEXT("Second rapid stroke retains its own captured input"), Mode.DeferredStrokes.Num() == 1
			&& Mode.DeferredStrokes[0].Stamps.Num() == 1 && Mode.DeferredStrokes[0].bReleased);
		for (int32 Step = 0; Step < 30000 && Mode.bPainting; ++Step) { Mode.AdvancePainting(1, 0.0); }
		TestTrue(TEXT("Rapid strokes preserve FIFO source exactly"), !Mode.bPainting && Mode.DeferredStrokes.IsEmpty()
			&& Mode.Settings->WorkingData.Vertices == Reference.Settings->WorkingData.Vertices && Mode.Settings->WorkingData.Indices == Reference.Settings->WorkingData.Indices);
		GEditor->RegisterForUndo(&Mode); GEditor->UndoTransaction();
		TestTrue(TEXT("Rapid strokes remain separate Undo steps"), Mode.Settings->WorkingData.Vertices == FirstRapidStroke.Vertices
			&& Mode.Settings->WorkingData.Indices == FirstRapidStroke.Indices);
		GEditor->UnregisterForUndo(&Mode);
		return true;
	}();
	World->DestroyWorld(false); return bResult;
}

#endif
