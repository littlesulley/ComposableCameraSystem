// Copyright 2026 Sulley. All Rights Reserved.

#include "MeshCamera/ComposableCameraMeshLayerEdMode.h"

#include "ComposableCameraSystemEditorModule.h"
#include "Async/Async.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "EditorViewportClient.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "MeshCamera/ComposableCameraMeshLayerModeToolkit.h"
#include "MeshCamera/ComposableCameraMeshLayerRendering.h"
#include "MeshCamera/ComposableCameraMeshLayerShapes.h"
#include "MeshCamera/ComposableCameraMeshLayerToolSettings.h"
#include "MeshCamera/ComposableCameraMeshSurfaceStorageActor.h"
#include "LevelUtils.h"
#include "Misc/MessageDialog.h"
#include "PrimitiveDrawingUtils.h"
#include "HitProxies.h"
#include <atomic>
#include "UObject/UnrealType.h"
#include "Toolkits/ToolkitManager.h"

#define LOCTEXT_NAMESPACE "ComposableCameraMeshLayerEdMode"

const FEditorModeID FComposableCameraMeshLayerEdMode::ModeId =
	TEXT("EM_ComposableCameraMeshLayers");

struct HComposableCameraMeshShapeControl : public HHitProxy
{
	DECLARE_HIT_PROXY();
	FGuid ShapeId;
	int32 ControlIndex;
	HComposableCameraMeshShapeControl(const FGuid& InId, int32 InIndex)
		: HHitProxy(HPP_UI), ShapeId(InId), ControlIndex(InIndex) {}
};
IMPLEMENT_HIT_PROXY(HComposableCameraMeshShapeControl, HHitProxy);

struct FComposableCameraMeshShapeCreationResult
{
	FComposableCameraMeshSurfaceAuthoringData Data;
	UE::ComposableCamera::MeshEditor::FResolvedSurfaceVisualization Visualization;
};

struct FComposableCameraMeshShapeCreationTask
{
	FComposableCameraMeshAuthoredShape Shape;
	TWeakObjectPtr<UWorld> World;
	UE::ComposableCamera::MeshEditor::FProjectedShapeBuild Build;
	FComposableCameraMeshSurfaceAuthoringData Geometry;
	UE::ComposableCamera::MeshEditor::FResolvedSurfaceLayerMesh Preview;
	TFuture<FComposableCameraMeshShapeCreationResult> Future;
	FGuid BaseRevision;
	bool bPartial = false;
	TSharedRef<std::atomic<bool>, ESPMode::ThreadSafe> Alive = MakeShared<std::atomic<bool>, ESPMode::ThreadSafe>(true);
	~FComposableCameraMeshShapeCreationTask() { Alive->store(false, std::memory_order_relaxed); }
};

FComposableCameraMeshLayerEdMode::FComposableCameraMeshLayerEdMode() = default;
FComposableCameraMeshLayerEdMode::~FComposableCameraMeshLayerEdMode() = default;

void FComposableCameraMeshLayerEdMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	FEdMode::Tick(ViewportClient, DeltaTime);
	// Multiple viewports tick the same mode. Spend the projection budget once.
	if (!PendingShapes.IsEmpty() && LastShapeTickFrame != GFrameCounter)
	{
		LastShapeTickFrame = GFrameCounter;
		AdvanceShapeCreation();
	}
}

void FComposableCameraMeshLayerEdMode::Enter()
{
	FEdMode::Enter();
	bExiting = false;

	Settings = NewObject<UComposableCameraMeshLayerToolSettings>(GetTransientPackage(), NAME_None, RF_Transactional);
	SelectionEditor = NewObject<UComposableCameraMeshLayerSelection>(GetTransientPackage(), NAME_None, RF_Transactional);
	Settings->OnLayerDataChanged.BindRaw(this, &FComposableCameraMeshLayerEdMode::MarkLayerDataDirty);
	Settings->OnToolSettingsChanged.BindRaw(this, &FComposableCameraMeshLayerEdMode::HandleToolSettingsChanged);
	SelectionEditor->OnBeforeEdit.BindRaw(this, &FComposableCameraMeshLayerEdMode::BeforeSelectionEdit);
	SelectionEditor->OnLayerEdited.BindRaw(this, &FComposableCameraMeshLayerEdMode::ApplySelectedLayer);
	SelectionEditor->OnShapeEdited.BindRaw(this, &FComposableCameraMeshLayerEdMode::ApplySelectedShape);
	if (!InitializeWorkingDocument())
	{
		RequestDeletion();
		return;
	}
	if (GEditor) { GEditor->RegisterForUndo(this); }
	RefreshSelectionEditor();

	if (!Toolkit.IsValid())
	{
		Toolkit = MakeShared<FComposableCameraMeshLayerModeToolkit>(this);
		Toolkit->Init(Owner->GetToolkitHost());
	}
}

void FComposableCameraMeshLayerEdMode::Exit()
{
	bExiting = true;
	CancelInteraction();
	if (GEditor) { GEditor->UnregisterForUndo(this); }
	if (IsDirty())
	{
		const EAppReturnType::Type Response = FMessageDialog::Open(
			EAppMsgType::YesNo,
			LOCTEXT("SaveBeforeExit", "Mesh Camera Layer data changed. Save before closing the tool?"));
		if (Response == EAppReturnType::Yes)
		{
			SaveWorkingData();
		}
	}

	if (Toolkit.IsValid())
	{
		FToolkitManager::Get().CloseToolkit(Toolkit.ToSharedRef());
		Toolkit.Reset();
	}

	if (Settings)
	{
		Settings->OnLayerDataChanged.Unbind();
		Settings->OnToolSettingsChanged.Unbind();
		Settings = nullptr;
	}
	if (SelectionEditor)
	{
		SelectionEditor->OnBeforeEdit.Unbind();
		SelectionEditor->OnLayerEdited.Unbind();
		SelectionEditor->OnShapeEdited.Unbind();
		SelectionEditor = nullptr;
	}

	bPainting = false;
	bHasHoverHit = false;
	SavedDocument = nullptr;
	PendingSaveActor.Reset();
	bPendingSaveCreatedActor = false;
	bPendingSaveNeedsDiscard = false;
	Visualization.Reset();
	FEdMode::Exit();
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

void FComposableCameraMeshLayerEdMode::RequestCloseFromToolkit()
{
	if (!bExiting)
	{
		RequestDeletion();
	}
}

void FComposableCameraMeshLayerEdMode::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Settings);
	Collector.AddReferencedObject(SavedDocument);
	Collector.AddReferencedObject(SelectionEditor);
	FEdMode::AddReferencedObjects(Collector);
}

bool FComposableCameraMeshLayerEdMode::InitializeWorkingDocument()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	TargetLevel = World->GetCurrentLevel();
	if (!TargetLevel.IsValid())
	{
		return false;
	}

	StorageActor = FindStorageActor();
	if (AComposableCameraMeshSurfaceStorageActor* ExistingActor = StorageActor.Get())
	{
		AnchorTransform = ExistingActor->GetActorTransform();
		Settings->Layers = ExistingActor->GetLayers();
		Settings->WorkingData = ExistingActor->GetAuthoringData();
	}
	else
	{
		AnchorTransform = FTransform::Identity;
		if (ULevelStreaming* StreamingLevel = FLevelUtils::FindStreamingLevel(TargetLevel.Get()))
		{
			AnchorTransform = StreamingLevel->LevelTransform;
		}
		Settings->WorkingData.Reset();
	}

	if (Settings->Layers.IsEmpty())
	{
		Settings->Layers.AddDefaulted();
	}
	Settings->NormalizeLayers();
	Settings->DocumentRevision = FGuid::NewGuid();
	CaptureSavedDocument();
	bDirty = false;
	bVisualizationDirty = true;
	return true;
}

AComposableCameraMeshSurfaceStorageActor*
FComposableCameraMeshLayerEdMode::FindStorageActor() const
{
	const ULevel* Level = TargetLevel.Get();
	if (!Level)
	{
		return nullptr;
	}

	for (AActor* Actor : Level->Actors)
	{
		if (AComposableCameraMeshSurfaceStorageActor* Storage =
			Cast<AComposableCameraMeshSurfaceStorageActor>(Actor))
		{
			return Storage;
		}
	}
	return nullptr;
}

AComposableCameraMeshSurfaceStorageActor*
FComposableCameraMeshLayerEdMode::FindOrCreateStorageActor()
{
	if (AComposableCameraMeshSurfaceStorageActor* ExistingActor = StorageActor.Get())
	{
		return ExistingActor;
	}

	ULevel* Level = TargetLevel.Get();
	UWorld* World = GetWorld();
	if (!Level || !World || FLevelUtils::IsLevelLocked(Level))
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.OverrideLevel = Level;
	SpawnParameters.ObjectFlags = RF_Transactional;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.Name = MakeUniqueObjectName(
		Level,
		AComposableCameraMeshSurfaceStorageActor::StaticClass(),
		TEXT("CCS_MeshSurfaceData"));

	AComposableCameraMeshSurfaceStorageActor* NewActor =
		World->SpawnActor<AComposableCameraMeshSurfaceStorageActor>(
			AComposableCameraMeshSurfaceStorageActor::StaticClass(),
			AnchorTransform,
			SpawnParameters);
	if (!NewActor)
	{
		return nullptr;
	}

	if (Level->IsUsingExternalActors() && !NewActor->IsPackageExternal())
	{
		NewActor->SetPackageExternal(true);
	}
	StorageActor = NewActor;
	return NewActor;
}

bool FComposableCameraMeshLayerEdMode::SaveWorkingData()
{
	if (IsCreatingShapes()) { return false; }
	if (!Settings || !TargetLevel.IsValid())
	{
		return false;
	}
	CancelInteraction();

	Settings->NormalizeLayers();
	RemoveOrphanedTriangles();
	const bool bHadStorageActor = StorageActor.IsValid();
	AComposableCameraMeshSurfaceStorageActor* Actor = FindOrCreateStorageActor();
	if (!Actor)
	{
		UE_LOG(LogComposableCameraSystemEditor, Error,
			TEXT("Mesh layer save failed: current Level is unavailable or locked."));
		return false;
	}

	if (PendingSaveActor.Get() != Actor) { bPendingSaveCreatedActor = !bHadStorageActor; }
	PendingSaveActor = Actor;
	bPendingSaveNeedsDiscard = true;
	Actor->Modify();
	Actor->SetAuthoringData(Settings->Layers, Settings->WorkingData);
	Actor->MarkPackageDirty();
	TargetLevel->MarkPackageDirty();
	ULevel::LevelDirtiedEvent.Broadcast();

	TArray<UPackage*> PackagesToSave;
	PackagesToSave.AddUnique(TargetLevel->GetPackage());
	PackagesToSave.AddUnique(Actor->GetPackage());
	const FEditorFileUtils::EPromptReturnCode Result =
		FEditorFileUtils::PromptForCheckoutAndSave(
			PackagesToSave,
			false,
			false);
	if (Result != FEditorFileUtils::PR_Success)
	{
		UE_LOG(LogComposableCameraSystemEditor, Warning,
			TEXT("Mesh layer data was applied but its package was not saved."));
		return false;
	}

	bDirty = false;
	CaptureSavedDocument();
	UE_LOG(LogComposableCameraSystemEditor, Log,
		TEXT("Saved %d mesh layers and %d authored triangles to Level '%s'."),
		Settings->Layers.Num(),
		Settings->WorkingData.Indices.Num() / 3,
		*TargetLevel->GetOutermost()->GetName());
	return true;
}

void FComposableCameraMeshLayerEdMode::CaptureSavedDocument()
{
	if (!Settings) { return; }
	if (!SavedDocument)
	{
		SavedDocument = NewObject<UComposableCameraMeshLayerToolSettings>(GetTransientPackage(), NAME_None, RF_Transient);
	}
	SavedDocument->Layers = Settings->Layers;
	SavedDocument->WorkingData = Settings->WorkingData;
	SavedDocument->ActiveLayerIndex = Settings->ActiveLayerIndex;
	SavedDocument->DocumentRevision = Settings->DocumentRevision;
	SavedRevision = Settings->DocumentRevision;
	PendingSaveActor.Reset();
	bPendingSaveCreatedActor = false;
	bPendingSaveNeedsDiscard = false;
}

void FComposableCameraMeshLayerEdMode::RefreshPendingSaveState()
{
	AComposableCameraMeshSurfaceStorageActor* Actor = PendingSaveActor.Get();
	bPendingSaveNeedsDiscard = Actor && SavedDocument;
	if (!bPendingSaveNeedsDiscard || bPendingSaveCreatedActor) { return; }
	const auto& ActorLayers = Actor->GetLayers();
	if (ActorLayers.Num() != SavedDocument->Layers.Num()) { return; }
	for (int32 Index = 0; Index < ActorLayers.Num(); ++Index)
	{
		if (!FComposableCameraMeshLayerDefinition::StaticStruct()->CompareScriptStruct(
			&ActorLayers[Index], &SavedDocument->Layers[Index], 0)) { return; }
	}
	bPendingSaveNeedsDiscard = !FComposableCameraMeshSurfaceAuthoringData::StaticStruct()->CompareScriptStruct(
		&Actor->GetAuthoringData(), &SavedDocument->WorkingData, 0);
}

bool FComposableCameraMeshLayerEdMode::CanDiscardWorkingData() const
{
	return Settings && SavedDocument && (Settings->DocumentRevision != SavedRevision
		|| bPendingSaveNeedsDiscard || IsCreatingShapes() || bDrawingShape || bEditingShape || bPainting);
}

bool FComposableCameraMeshLayerEdMode::DiscardWorkingData()
{
	if (!CanDiscardWorkingData()) { return false; }
	// Revert the unfinished stroke before recording Discard, so its Undo restores
	// only committed edits. Released Shapes have no source mutation to undo yet.
	FinishStroke(true);
	CancelInteraction();
	const bool bRestoreDocument = Settings->DocumentRevision != SavedRevision;
	FScopedTransaction Transaction(LOCTEXT("DiscardTransaction", "Discard Mesh Camera Layer Changes"),
		bRestoreDocument || bPendingSaveNeedsDiscard);
	if (AComposableCameraMeshSurfaceStorageActor* Actor = bPendingSaveNeedsDiscard ? PendingSaveActor.Get() : nullptr)
	{
		Actor->Modify();
		if (bPendingSaveCreatedActor)
		{
			UWorld* World = Actor->GetWorld();
			if (!World || !World->EditorDestroyActor(Actor, true))
			{
				Transaction.Cancel();
				UE_LOG(LogComposableCameraSystemEditor, Error, TEXT("Mesh layer discard failed: cannot remove the unsaved storage actor."));
				return false;
			}
		}
		else
		{
			Actor->SetAuthoringData(SavedDocument->Layers, SavedDocument->WorkingData);
			Actor->MarkPackageDirty();
			if (ULevel* Level = Actor->GetLevel()) { Level->MarkPackageDirty(); }
			ULevel::LevelDirtiedEvent.Broadcast();
		}
		// Retain this weak identity until a successful Save. Undo of Discard can
		// restore the failed-save actor data, which another Discard must revert too.
	}
	bPendingSaveNeedsDiscard = false;
	if (bRestoreDocument)
	{
		Settings->Modify();
		Settings->Layers = SavedDocument->Layers;
		Settings->WorkingData = SavedDocument->WorkingData;
		Settings->ActiveLayerIndex = SavedDocument->ActiveLayerIndex;
		Settings->DocumentRevision = SavedRevision;
		Settings->NormalizeLayers();
	}
	SelectedShapeId.Invalidate();
	EditControlIndex = INDEX_NONE;
	ShapeFeedback = FText::GetEmpty();
	bHasHoverHit = false;
	RefreshDocumentState();
	RefreshSelectionEditor();
	if (Toolkit.IsValid()) { StaticCastSharedPtr<FComposableCameraMeshLayerModeToolkit>(Toolkit)->RefreshDocument(); }
	return true;
}

bool FComposableCameraMeshLayerEdMode::InputKey(
	FEditorViewportClient* ViewportClient,
	FViewport* Viewport,
	FKey Key,
	EInputEvent Event)
{
	if (Event == IE_Pressed && ViewportClient && ViewportClient->IsCtrlPressed()
		&& (Key == EKeys::Z || Key == EKeys::Y))
	{
		CancelInteraction();
		if (GEditor)
		{
			if (Key == EKeys::Y || ViewportClient->IsShiftPressed()) { GEditor->RedoTransaction(); }
			else { GEditor->UndoTransaction(); }
		}
		return true;
	}
	if (Event == IE_Pressed && Key == EKeys::Delete && HasSelectedShape())
	{
		DeleteSelectedShape();
		return true;
	}
	if (Event == IE_Pressed && (bDrawingShape || bPainting || bEditingShape || IsCreatingShapes())
		&& (Key == EKeys::Escape || Key == EKeys::RightMouseButton))
	{
		FinishStroke(true);
		CancelInteraction();
		return true;
	}
	if (Settings && bDrawingShape && Settings->Tool == EComposableCameraMeshDrawTool::Polygon)
	{
		if (Key == EKeys::Enter && Event == IE_Pressed)
		{
			CommitShape(ShapePoints);
			return true;
		}
		if (Key == EKeys::BackSpace && Event == IE_Pressed)
		{
			ShapeFeedback = FText::GetEmpty();
			ShapePoints.Pop(EAllowShrinking::No);
			if (ShapePoints.IsEmpty()) { CancelInteraction(); }
			else { UpdateShapePreview(); }
			return true;
		}
		if (Key == EKeys::LeftMouseButton && Event == IE_DoubleClick && ViewportClient && !ViewportClient->IsAltPressed())
		{
			CommitShape(ShapePoints);
			return true;
		}
	}
	// Always finish captured input, even when Alt becomes pressed during the drag.
	if (Key == EKeys::LeftMouseButton && Event == IE_Released)
	{
		if (bEditingShape)
		{
			UpdateHoverHit(ViewportClient);
			UpdateEditPreview();
			if (bHasHoverHit) { CommitEditedShape(EditShape); }
			ResetInteraction();
			return true;
		}
		if (bPainting)
		{
			FinishStroke();
			return true;
		}
		if (bDrawingShape && Settings)
		{
			if (Settings->Tool != EComposableCameraMeshDrawTool::Polygon)
			{
				UpdateHoverHit(ViewportClient);
				UpdateShapePreview();
				if (bHasHoverHit) { CommitShape(ShapePreview); }
				else { ShapeFeedback = LOCTEXT("ShapeMissingEndpoint", "Release over a valid floor to create the shape."); }
				ResetInteraction();
			}
			return true;
		}
	}
	if (Key == EKeys::LeftMouseButton && ViewportClient && !ViewportClient->IsAltPressed())
	{
		if (Event == IE_Pressed && Settings)
		{
			UpdateHoverHit(ViewportClient);
			if (Settings->Tool == EComposableCameraMeshDrawTool::Select)
			{
				FGuid Picked;
				int32 Control = INDEX_NONE;
				if (Viewport)
				{
					HHitProxy* Hit = Viewport->GetHitProxy(Viewport->GetMouseX(), Viewport->GetMouseY());
					if (Hit && Hit->IsA(HComposableCameraMeshShapeControl::StaticGetType()))
					{
						const auto* ShapeHit = static_cast<HComposableCameraMeshShapeControl*>(Hit);
						Picked = ShapeHit->ShapeId;
						Control = ShapeHit->ControlIndex;
					}
				}
				if (!Picked.IsValid())
				{
					const FViewportCursorLocation Cursor = ViewportClient->GetCursorWorldLocationFromMousePos();
					Picked = UE::ComposableCamera::MeshEditor::FindShapeOnRay(Settings->WorkingData, Settings->GetActiveLayerId(),
						AnchorTransform.InverseTransformPosition(Cursor.GetOrigin()), AnchorTransform.InverseTransformVector(Cursor.GetDirection()).GetSafeNormal());
				}
				SelectedShapeId = Picked;
				RefreshSelectionEditor();
				if (const FComposableCameraMeshAuthoredShape* Shape = Settings->WorkingData.Shapes.FindByPredicate(
					[Picked](const auto& Item) { return Item.ShapeId == Picked; }))
				{
					if (bHasHoverHit)
					{
						EditShape = *Shape;
						EditControlIndex = Control;
						const FVector LocalHit = AnchorTransform.InverseTransformPosition(HoverHit.ImpactPoint);
						EditDragStart = FVector2D(LocalHit.X, LocalHit.Y);
						bEditingShape = true;
						UE::ComposableCamera::MeshEditor::BuildShapeOutline(EditShape, ShapePreview);
					}
				}
				return true;
			}
			if (Settings->Tool == EComposableCameraMeshDrawTool::Brush || Settings->Tool == EComposableCameraMeshDrawTool::Erase)
			{
				BeginStroke();
				if (bPainting) { PaintAtHover(ViewportClient); }
			}
			else if (bHasHoverHit && Settings->GetActiveLayerId().IsValid())
			{
				if (!bDrawingShape) { BeginShape(); }
				else if (Settings->Tool == EComposableCameraMeshDrawTool::Polygon)
				{
					UpdateShapePreview();
					if (ShapePoints.Num() >= 3 && ShapeEnd.Equals(ShapePoints[0], 0.001)) { CommitShape(ShapePoints); }
					else if (!ShapeEnd.Equals(ShapePoints.Last(), 0.001) && ShapePoints.Num() < 256) { ShapeFeedback = FText::GetEmpty(); ShapePoints.Add(ShapeEnd); UpdateShapePreview(); }
					else if (ShapePoints.Num() >= 256) { ShapeFeedback = LOCTEXT("PolygonPointLimit", "Polygon supports up to 256 vertices. Finish this shape before starting another."); }
				}
			}
			return true;
		}
	}

	return FEdMode::InputKey(ViewportClient, Viewport, Key, Event);
}

bool FComposableCameraMeshLayerEdMode::MouseMove(
	FEditorViewportClient* ViewportClient,
	FViewport* Viewport,
	int32 X,
	int32 Y)
{
	UpdateHoverHit(ViewportClient);
	if (bEditingShape)
	{
		UpdateEditPreview();
		return true;
	}
	if (bDrawingShape)
	{
		UpdateShapePreview();
		return true;
	}
	if (bPainting)
	{
		PaintAtHover(ViewportClient);
		return true;
	}
	return false;
}

bool FComposableCameraMeshLayerEdMode::CapturedMouseMove(
	FEditorViewportClient* ViewportClient,
	FViewport* Viewport,
	int32 X,
	int32 Y)
{
	return MouseMove(ViewportClient, Viewport, X, Y);
}

bool FComposableCameraMeshLayerEdMode::LostFocus(FEditorViewportClient* ViewportClient, FViewport* Viewport)
{
	// A released region keeps completing when focus moves to another panel.
	ResetInteraction();
	return FEdMode::LostFocus(ViewportClient, Viewport);
}

bool FComposableCameraMeshLayerEdMode::UpdateHoverHit(FEditorViewportClient* ViewportClient)
{
	bHasHoverHit = false;
	if (!ViewportClient || !GetWorld() || !Settings)
	{
		return false;
	}

	const FViewportCursorLocation Cursor = ViewportClient->GetCursorWorldLocationFromMousePos();
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CCSMeshLayerBrush), true);
	QueryParams.bTraceComplex = true;
	if (AComposableCameraMeshSurfaceStorageActor* Actor = StorageActor.Get())
	{
		QueryParams.AddIgnoredActor(Actor);
	}

	bHasHoverHit = GetWorld()->LineTraceSingleByChannel(
		HoverHit,
		Cursor.GetOrigin(),
		Cursor.GetOrigin() + Cursor.GetDirection() * HALF_WORLD_MAX,
		ECC_Visibility,
		QueryParams);
	const FVector DocumentUp = AnchorTransform.TransformVectorNoScale(FVector::UpVector).GetSafeNormal();
	if (bHasHoverHit
		&& FVector::DotProduct(HoverHit.ImpactNormal, DocumentUp) < Settings->MinimumFloorNormalZ)
	{
		bHasHoverHit = false;
	}
	ViewportClient->Invalidate(false, false);
	return bHasHoverHit;
}

void FComposableCameraMeshLayerEdMode::CancelInteraction()
{
	// Workers own plain snapshots, never the mode, Settings or World. Discarding
	// their future does not wait, and no cancelled result can write the document.
	PendingShapes.Reset();
	ResetInteraction();
}

void FComposableCameraMeshLayerEdMode::ResetInteraction()
{
	FinishStroke();
	bEditingShape = false;
	bDrawingShape = false;
	bPainting = false;
	bHasLastPaintPosition = false;
	ShapeLayerId.Invalidate();
	ShapePoints.Reset();
	ShapePreview.Reset();
	ShapeMeasurement = FText::GetEmpty();
	if (GEditor) { GEditor->RedrawLevelEditingViewports(); }
}

void FComposableCameraMeshLayerEdMode::BeginShape()
{
	if (!Settings || !bHasHoverHit || !Settings->GetActiveLayerId().IsValid()) { return; }
	ShapeStartHit = HoverHit;
	ShapeLayerId = Settings->GetActiveLayerId();
	const FVector LocalHit = AnchorTransform.InverseTransformPosition(HoverHit.ImpactPoint);
	ShapeStart = UE::ComposableCamera::MeshEditor::SnapShapePoint(FVector2D(LocalHit.X, LocalHit.Y), Settings->ShapeGridSize);
	ShapeEnd = ShapeStart;
	ShapeFeedback = FText::GetEmpty();
	ShapePoints.Reset();
	if (Settings->Tool == EComposableCameraMeshDrawTool::Polygon) { ShapePoints.Add(ShapeStart); }
	bDrawingShape = true;
	UpdateShapePreview();
}

void FComposableCameraMeshLayerEdMode::UpdateShapePreview()
{
	if (!Settings || !bDrawingShape) { return; }
	if (bHasHoverHit)
	{
		const FVector LocalHit = AnchorTransform.InverseTransformPosition(HoverHit.ImpactPoint);
		ShapeEnd = UE::ComposableCamera::MeshEditor::SnapShapePoint(FVector2D(LocalHit.X, LocalHit.Y), Settings->ShapeGridSize);
	}
	switch (Settings->Tool)
	{
	case EComposableCameraMeshDrawTool::Rectangle:
		UE::ComposableCamera::MeshEditor::BuildRectangleOutline(ShapeStart, ShapeEnd, ShapePreview);
		ShapeMeasurement = FText::Format(LOCTEXT("RectangleSize", "Width: {0} cm  Height: {1} cm"),
			FText::AsNumber(FMath::Abs(ShapeEnd.X - ShapeStart.X)), FText::AsNumber(FMath::Abs(ShapeEnd.Y - ShapeStart.Y)));
		break;
	case EComposableCameraMeshDrawTool::Circle:
	{
		double Radius = (ShapeEnd - ShapeStart).Size();
		if (Settings->ShapeGridSize > 0.0) { Radius = FMath::GridSnap(Radius, Settings->ShapeGridSize); }
		UE::ComposableCamera::MeshEditor::BuildCircleOutline(ShapeStart, Radius, Settings->CircleSegments, ShapePreview);
		ShapeMeasurement = FText::Format(LOCTEXT("CircleSize", "Radius: {0} cm"), FText::AsNumber(Radius));
		break;
	}
	case EComposableCameraMeshDrawTool::Polygon:
		ShapePreview = ShapePoints;
		if (bHasHoverHit && !ShapeEnd.Equals(ShapePoints.Last(), 0.001)) { ShapePreview.Add(ShapeEnd); }
		ShapeMeasurement = FText::Format(LOCTEXT("PolygonSize", "Vertices: {0}"), FText::AsNumber(ShapePoints.Num()));
		break;
	case EComposableCameraMeshDrawTool::Brush:
	case EComposableCameraMeshDrawTool::Select:
	case EComposableCameraMeshDrawTool::Erase:
		break;
	}
}

FVector FComposableCameraMeshLayerEdMode::GetShapePlanePosition(const FVector2D& Point) const
{
	const FVector Up = AnchorTransform.TransformVectorNoScale(FVector::UpVector).GetSafeNormal();
	const double LocalHeight = AnchorTransform.InverseTransformPosition(ShapeStartHit.ImpactPoint).Z;
	FVector Position = AnchorTransform.TransformPosition(FVector(Point.X, Point.Y, LocalHeight));
	const double Denominator = FVector::DotProduct(Up, ShapeStartHit.ImpactNormal);
	if (!FMath::IsNearlyZero(Denominator))
	{
		Position += Up * (FVector::DotProduct(ShapeStartHit.ImpactPoint - Position, ShapeStartHit.ImpactNormal) / Denominator);
	}
	return Position;
}

bool FComposableCameraMeshLayerEdMode::CommitShape(TConstArrayView<FVector2D> Outline)
{
	if (!Settings || ShapeLayerId != Settings->GetActiveLayerId()) { return false; }
	FComposableCameraMeshAuthoredShape Shape;
	Shape.ShapeId = FGuid::NewGuid();
	Shape.LayerId = ShapeLayerId;
	Shape.PlaneOrigin = AnchorTransform.InverseTransformPosition(ShapeStartHit.ImpactPoint);
	Shape.PlaneNormal = FVector(
		FVector::DotProduct(ShapeStartHit.ImpactNormal, AnchorTransform.TransformVector(FVector::ForwardVector)),
		FVector::DotProduct(ShapeStartHit.ImpactNormal, AnchorTransform.TransformVector(FVector::RightVector)),
		FVector::DotProduct(ShapeStartHit.ImpactNormal, AnchorTransform.TransformVector(FVector::UpVector))).GetSafeNormal();
	Shape.SampleSpacing = Settings->ShapeSampleSpacing;
	Shape.ProjectionDistance = Settings->ProjectionDistance;
	Shape.MinimumFloorNormalZ = Settings->MinimumFloorNormalZ;
	Shape.CircleSegments = Settings->CircleSegments;
	switch (Settings->Tool)
	{
	case EComposableCameraMeshDrawTool::Rectangle:
		Shape.Type = EComposableCameraMeshShapeType::Rectangle;
		Shape.ControlPoints = {ShapeStart, ShapeEnd};
		break;
	case EComposableCameraMeshDrawTool::Circle:
	{
		Shape.Type = EComposableCameraMeshShapeType::Circle;
		const double Radius = Settings->ShapeGridSize > 0.0 ? FMath::GridSnap((ShapeEnd - ShapeStart).Size(), Settings->ShapeGridSize) : (ShapeEnd - ShapeStart).Size();
		Shape.ControlPoints = {ShapeStart, ShapeStart + (ShapeEnd - ShapeStart).GetSafeNormal() * Radius};
		break;
	}
	case EComposableCameraMeshDrawTool::Polygon:
		Shape.Type = EComposableCameraMeshShapeType::Polygon;
		Shape.ControlPoints.Append(Outline.GetData(), Outline.Num());
		break;
	default:
		return false;
	}
	if (!QueueShapeCreation(MoveTemp(Shape), GetWorld())) { return false; }
	ResetInteraction();
	return true;
}

bool FComposableCameraMeshLayerEdMode::QueueShapeCreation(FComposableCameraMeshAuthoredShape Shape, UWorld* World)
{
	using namespace UE::ComposableCamera::MeshEditor;
	if (!Settings || Shape.LayerId != Settings->GetActiveLayerId()) { return false; }
	auto Task = MakeUnique<FComposableCameraMeshShapeCreationTask>();
	TArray<FVector2D> Outline;
	BuildShapeOutline(Shape, Outline);
	const EShapeBuildResult Started = Task->Build.Begin(Outline, Shape.SampleSpacing, Shape.LayerId);
	if (Started != EShapeBuildResult::Success)
	{
		ShapeFeedback = Started == EShapeBuildResult::TooComplex
			? LOCTEXT("ShapeTooDense", "Shape is too dense. Increase Shape Sample Spacing or draw a smaller region.")
			: LOCTEXT("InvalidShape", "Shape needs a nonzero area and a simple outline without crossing edges.");
		return false;
	}
	Task->Shape = MoveTemp(Shape); Task->World = World;
	Task->Preview.LayerIndex = Settings->ActiveLayerIndex;
	FLinearColor Color = Settings->Layers[Settings->ActiveLayerIndex].DebugColor; Color.A = 0.2f;
	Task->Preview.Color = Color.ToFColor(true);
	for (const FVector2D& Point : Task->Build.GetOutline()) { Task->Preview.LocalVertices.Add(ShapePlanePosition(Task->Shape, Point)); }
	const auto Indices = Task->Build.GetOutlineIndices();
	Task->Preview.Indices.Append(Indices.GetData(), Indices.Num());
	PendingShapes.Add(MoveTemp(Task));
	ShapeFeedback = FText::GetEmpty();
	if (GEditor) { GEditor->RedrawLevelEditingViewports(); }
	return true;
}

const UE::ComposableCamera::MeshEditor::FResolvedSurfaceLayerMesh& FComposableCameraMeshLayerEdMode::GetPendingShapePreview(int32 Index) const
{
	return PendingShapes[Index]->Preview;
}

void FComposableCameraMeshLayerEdMode::AdvanceShapeCreation()
{
	using namespace UE::ComposableCamera::MeshEditor;
	if (!Settings || PendingShapes.IsEmpty() || bExiting || GIsTransacting || (GEditor && GEditor->IsTransactionActive())) { return; }
	auto& Task = *PendingShapes[0];
	UWorld* World = Task.World.Get();
	if (!World || World != GetWorld() || Task.Shape.LayerId != Settings->GetActiveLayerId()) { CancelInteraction(); return; }
	if (Task.Future.IsValid())
	{
		if (!Task.Future.IsReady()) { return; }
		if (bDrawingShape || bEditingShape || bPainting) { return; }
		if (Task.BaseRevision == Settings->DocumentRevision)
		{
			auto Completed = Task.Future.Consume();
			const FGuid ShapeId = Task.Shape.ShapeId;
			const bool bPartial = Task.bPartial;
			PendingShapes.RemoveAt(0);
			ApplyCreatedShape(MoveTemp(Completed.Data), MoveTemp(Completed.Visualization), ShapeId, bPartial);
			return;
		}
		// An existing Shape may have been edited while coverage was computed.
		// Rebase plain geometry on the latest document instead of restoring stale
		// source, dropping the new region, or repeating collision projection.
		Task.Future = TFuture<FComposableCameraMeshShapeCreationResult>();
	}
	const FVector Up = AnchorTransform.TransformVectorNoScale(FVector::UpVector).GetSafeNormal();
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CCSMeshLayerShape), true);
	if (AComposableCameraMeshSurfaceStorageActor* Actor = StorageActor.Get()) { QueryParams.AddIgnoredActor(Actor); }
	const bool bFinished = Task.Build.Advance([this, World, Up, &QueryParams, &Task](const FVector2D& Point, FVector& LocalPosition)
	{
		const FVector Desired = AnchorTransform.TransformPosition(ShapePlanePosition(Task.Shape, Point));
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Desired + Up * Task.Shape.ProjectionDistance,
			Desired - Up * Task.Shape.ProjectionDistance, ECC_Visibility, QueryParams)
			|| FVector::DotProduct(Hit.ImpactNormal, Up) < Task.Shape.MinimumFloorNormalZ) { return false; }
		LocalPosition = AnchorTransform.InverseTransformPosition(Hit.ImpactPoint);
		return true;
	}, 256, 0.004);
	if (!bFinished) { return; }
	const EShapeBuildResult Result = Task.Build.GetResult();
	if (Result != EShapeBuildResult::Success && Result != EShapeBuildResult::PartialSurface)
	{
		PendingShapes.RemoveAt(0);
		ShapeFeedback = Result == EShapeBuildResult::NoSurface
			? LOCTEXT("ShapeNoFloor", "No compatible floor found inside this shape.")
			: Result == EShapeBuildResult::TooComplex
				? LOCTEXT("ShapeTooDense", "Shape is too dense. Increase Shape Sample Spacing or draw a smaller region.")
				: LOCTEXT("InvalidShape", "Shape needs a nonzero area and a simple outline without crossing edges.");
		if (GEditor) { GEditor->RedrawLevelEditingViewports(); }
		return;
	}
	Task.bPartial = Result == EShapeBuildResult::PartialSurface;
	if (Task.Geometry.Indices.IsEmpty()) { Task.Geometry = Task.Build.TakeData(); }
	Task.BaseRevision = Settings->DocumentRevision;
	FComposableCameraMeshSurfaceAuthoringData Source = Settings->WorkingData;
	FResolvedSurfaceVisualization Cached = Visualization;
	const bool bRebuild = bVisualizationDirty;
	// Only GUID/enabled flags are needed for coverage. No UObject references go
	// to the worker, and all collision queries above stay on the editor thread.
	TArray<FComposableCameraMeshLayerDefinition> Layers;
	for (const auto& Layer : Settings->Layers)
	{
		auto& Snapshot = Layers.AddDefaulted_GetRef(); Snapshot.LayerId = Layer.LayerId; Snapshot.bEnabled = Layer.bEnabled;
	}
	Task.Future = Async(EAsyncExecution::ThreadPool,
		[Source = MoveTemp(Source), Cached = MoveTemp(Cached), Layers = MoveTemp(Layers),
			Shape = Task.Shape, Geometry = Task.Geometry, Alive = Task.Alive, bRebuild]() mutable
	{
		FComposableCameraMeshShapeCreationResult Completed;
		if (!Alive->load(std::memory_order_relaxed)) { return Completed; }
		FBox2D Bounds(ForceInit);
		for (const FVector3f& Point : Geometry.Vertices) { Bounds += FVector2D(Point.X, Point.Y); }
		AppendShapeGeometry(Source, Geometry, Shape.ShapeId); Source.Shapes.Add(MoveTemp(Shape));
		if (bRebuild) { BuildAuthoringVisualization(Source, Layers, Cached); }
		else { UpdateAuthoringVisualization(Source, Layers, Bounds, Cached); }
		Completed.Data = MoveTemp(Source); Completed.Visualization = MoveTemp(Cached);
		return Completed;
	});
}

void FComposableCameraMeshLayerEdMode::ApplyCreatedShape(FComposableCameraMeshSurfaceAuthoringData Data,
	UE::ComposableCamera::MeshEditor::FResolvedSurfaceVisualization Resolved, const FGuid& ShapeId, bool bPartial)
{
	const FScopedTransaction Transaction(LOCTEXT("EditShapeTransaction", "Edit Mesh Camera Shape"));
	Settings->Modify();
	Settings->WorkingData = MoveTemp(Data);
	Settings->TouchDocument(); SelectedShapeId = ShapeId;
	Visualization = MoveTemp(Resolved); bVisualizationDirty = false;
	bDirty = Settings->DocumentRevision != SavedRevision;
	ShapeFeedback = bPartial ? LOCTEXT("ShapePartialFloor", "Shape updated. Samples without compatible floor were omitted.") : FText::GetEmpty();
	RebuildShapeOverlays(); RefreshSelectionEditor();
	if (GEditor) { GEditor->RedrawLevelEditingViewports(); }
}

bool FComposableCameraMeshLayerEdMode::CommitEditedShape(FComposableCameraMeshAuthoredShape Shape, bool bTransact)
{
	UWorld* World = GetWorld();
	if (!World || !Settings || Shape.LayerId != Settings->GetActiveLayerId()) { return false; }
	const FComposableCameraMeshAuthoredShape* Previous = Settings->WorkingData.Shapes.FindByPredicate(
		[&Shape](const auto& Item) { return Item.ShapeId == Shape.ShapeId; });
	if (Previous && Previous->ControlPoints == Shape.ControlPoints) { return true; }
	const FVector Up = AnchorTransform.TransformVectorNoScale(FVector::UpVector).GetSafeNormal();
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CCSMeshLayerShape), true);
	if (AComposableCameraMeshSurfaceStorageActor* Actor = StorageActor.Get()) { QueryParams.AddIgnoredActor(Actor); }
	FComposableCameraMeshSurfaceAuthoringData ShapeData;
	using namespace UE::ComposableCamera::MeshEditor;
	TArray<FVector2D> Outline;
	BuildShapeOutline(Shape, Outline);
	const EShapeBuildResult Result = BuildProjectedShape(Outline, Shape.SampleSpacing, Shape.LayerId,
		[this, World, Up, &QueryParams, &Shape](const FVector2D& Point, FVector& LocalPosition)
		{
			const FVector Desired = AnchorTransform.TransformPosition(ShapePlanePosition(Shape, Point));
			FHitResult Hit;
			if (!World->LineTraceSingleByChannel(Hit, Desired + Up * Shape.ProjectionDistance,
				Desired - Up * Shape.ProjectionDistance, ECC_Visibility, QueryParams)
				|| FVector::DotProduct(Hit.ImpactNormal, Up) < Shape.MinimumFloorNormalZ) { return false; }
			LocalPosition = AnchorTransform.InverseTransformPosition(Hit.ImpactPoint);
			return true;
		}, ShapeData);
	switch (Result)
	{
	case EShapeBuildResult::InvalidOutline:
		ShapeFeedback = LOCTEXT("InvalidShape", "Shape needs a nonzero area and a simple outline without crossing edges.");
		return false;
	case EShapeBuildResult::TooComplex:
		ShapeFeedback = LOCTEXT("ShapeTooDense", "Shape is too dense. Increase Shape Sample Spacing or draw a smaller region.");
		return false;
	case EShapeBuildResult::NoSurface:
		ShapeFeedback = LOCTEXT("ShapeNoFloor", "No compatible floor found inside this shape.");
		return false;
	case EShapeBuildResult::PartialSurface:
		ShapeFeedback = LOCTEXT("ShapePartialFloor", "Shape updated. Samples without compatible floor were omitted.");
		break;
	case EShapeBuildResult::Success:
		ShapeFeedback = FText::GetEmpty();
		break;
	}
	ApplyShapeGeometry(MoveTemp(Shape), MoveTemp(ShapeData), bTransact);
	return true;
}

void FComposableCameraMeshLayerEdMode::ApplyShapeGeometry(FComposableCameraMeshAuthoredShape Shape,
	FComposableCameraMeshSurfaceAuthoringData ShapeData, bool bTransact)
{
	using namespace UE::ComposableCamera::MeshEditor;
	for (const auto& Erasure : Shape.Erasures) { EraseShapeGeometry(ShapeData, Shape.LayerId, Erasure, false); }
	const FScopedTransaction Transaction(LOCTEXT("EditShapeTransaction", "Edit Mesh Camera Shape"), bTransact);
	Settings->Modify();
	if (Settings->WorkingData.Shapes.ContainsByPredicate([&Shape](const auto& Item) { return Item.ShapeId == Shape.ShapeId; }))
	{
		RemoveShapeGeometry(Settings->WorkingData, Shape.ShapeId);
	}
	Settings->WorkingData.Shapes.RemoveAll([&Shape](const auto& Item) { return Item.ShapeId == Shape.ShapeId; });
	AppendShapeGeometry(Settings->WorkingData, ShapeData, Shape.ShapeId);
	SelectedShapeId = Shape.ShapeId;
	Settings->WorkingData.Shapes.Add(MoveTemp(Shape));
	Settings->TouchDocument();
	RefreshDocumentState();
	RefreshSelectionEditor();
}

bool FComposableCameraMeshLayerEdMode::PaintAtHover(FEditorViewportClient* ViewportClient)
{
	if (!bHasHoverHit || !Settings || !Settings->Layers.IsValidIndex(Settings->ActiveLayerIndex))
	{
		return false;
	}

	const double MinimumSpacing = Settings->BrushRadius * 0.35;
	if (bHasLastPaintPosition
		&& FVector::Distance(LastPaintWorldPosition, HoverHit.ImpactPoint) < MinimumSpacing)
	{
		return false;
	}

	const FGuid LayerId = Settings->Layers[Settings->ActiveLayerIndex].LayerId;
	const bool bTemporaryErase = ViewportClient && ViewportClient->IsShiftPressed();
	const bool bErase = Settings->Tool == EComposableCameraMeshDrawTool::Erase || bTemporaryErase;
	const int32 FirstAddedVertex = Settings->WorkingData.Vertices.Num();
	FBox2D DirtyBounds(ForceInit);
	const bool bChanged = bErase
		? EraseBrushStamp(HoverHit, LayerId, &DirtyBounds)
		: AddProjectedBrushStamp(HoverHit, LayerId);
	// Space attempts too: a no-op eraser must not rescan the document at every pixel.
	LastPaintWorldPosition = HoverHit.ImpactPoint;
	bHasLastPaintPosition = true;
	if (bChanged)
	{
		if (!bErase)
		{
			for (int32 Index = FirstAddedVertex; Index < Settings->WorkingData.Vertices.Num(); ++Index)
			{
				const FVector3f& Point = Settings->WorkingData.Vertices[Index];
				DirtyBounds += FVector2D(Point.X, Point.Y);
			}
		}
		Settings->TouchDocument();
		bDirty = Settings->DocumentRevision != SavedRevision;
		if (bVisualizationDirty)
		{
			UE::ComposableCamera::MeshEditor::BuildAuthoringVisualization(Settings->WorkingData, Settings->Layers, Visualization);
			bVisualizationDirty = false;
		}
		else
		{
			UE::ComposableCamera::MeshEditor::UpdateAuthoringVisualization(Settings->WorkingData, Settings->Layers, DirtyBounds, Visualization);
		}
		// Brush/erase does not move controls; retain their cached outlines.
		if (ViewportClient) { ViewportClient->Invalidate(false, false); }
	}
	return bChanged;
}

bool FComposableCameraMeshLayerEdMode::AddProjectedBrushStamp(
	const FHitResult& CenterHit,
	const FGuid& LayerId)
{
	UWorld* World = GetWorld();
	if (!World || !LayerId.IsValid())
	{
		return false;
	}

	const FVector Normal = CenterHit.ImpactNormal.GetSafeNormal();
	FVector TangentX = FVector::CrossProduct(FVector::UpVector, Normal).GetSafeNormal();
	if (TangentX.IsNearlyZero())
	{
		TangentX = FVector::ForwardVector;
	}
	const FVector TangentY = FVector::CrossProduct(Normal, TangentX).GetSafeNormal();
	const int32 SegmentCount = FMath::Clamp(Settings->BrushSegments, 6, 32);

	TArray<FVector, TInlineAllocator<32>> RingPoints;
	TArray<bool, TInlineAllocator<32>> ValidPoints;
	RingPoints.SetNum(SegmentCount);
	ValidPoints.Init(false, SegmentCount);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CCSMeshLayerProjection), true);
	QueryParams.bTraceComplex = true;
	if (AComposableCameraMeshSurfaceStorageActor* Actor = StorageActor.Get())
	{
		QueryParams.AddIgnoredActor(Actor);
	}

	for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
	{
		const double Angle = 2.0 * UE_DOUBLE_PI * SegmentIndex / SegmentCount;
		const FVector Desired = CenterHit.ImpactPoint
			+ TangentX * (FMath::Cos(Angle) * Settings->BrushRadius)
			+ TangentY * (FMath::Sin(Angle) * Settings->BrushRadius);

		FHitResult ProjectionHit;
		if (World->LineTraceSingleByChannel(
			ProjectionHit,
			Desired + Normal * Settings->ProjectionDistance,
			Desired - Normal * Settings->ProjectionDistance,
			ECC_Visibility,
			QueryParams)
			&& FVector::DotProduct(
				ProjectionHit.ImpactNormal,
				AnchorTransform.TransformVectorNoScale(FVector::UpVector).GetSafeNormal())
				>= Settings->MinimumFloorNormalZ)
		{
			RingPoints[SegmentIndex] = ProjectionHit.ImpactPoint;
			ValidPoints[SegmentIndex] = true;
		}
	}

	int32 AddedTriangleCount = 0;
	Settings->WorkingData.TriangleShapeIds.SetNum(Settings->WorkingData.TriangleLayerIds.Num());
	for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
	{
		const int32 NextIndex = (SegmentIndex + 1) % SegmentCount;
		if (!ValidPoints[SegmentIndex] || !ValidPoints[NextIndex])
		{
			continue;
		}

		const int32 Index0 = Settings->WorkingData.Vertices.Add(
			FVector3f(AnchorTransform.InverseTransformPosition(CenterHit.ImpactPoint)));
		const int32 Index1 = Settings->WorkingData.Vertices.Add(
			FVector3f(AnchorTransform.InverseTransformPosition(RingPoints[SegmentIndex])));
		const int32 Index2 = Settings->WorkingData.Vertices.Add(
			FVector3f(AnchorTransform.InverseTransformPosition(RingPoints[NextIndex])));
		Settings->WorkingData.Indices.Add(Index0);
		Settings->WorkingData.Indices.Add(Index1);
		Settings->WorkingData.Indices.Add(Index2);
		Settings->WorkingData.TriangleLayerIds.Add(LayerId);
		Settings->WorkingData.TriangleShapeIds.Add(FGuid());
		++AddedTriangleCount;
	}

	return AddedTriangleCount > 0;
}

bool FComposableCameraMeshLayerEdMode::EraseBrushStamp(const FHitResult& CenterHit, const FGuid& LayerId, FBox2D* OutDirtyBounds)
{
	if (!Settings || !LayerId.IsValid()) { return false; }
	const FVector Normal = CenterHit.ImpactNormal.GetSafeNormal();
	FVector TangentX = FVector::CrossProduct(FVector::UpVector, Normal).GetSafeNormal();
	if (TangentX.IsNearlyZero()) { TangentX = FVector::ForwardVector; }
	const FVector TangentY = FVector::CrossProduct(Normal, TangentX).GetSafeNormal();
	FComposableCameraMeshEraseStamp Stamp;
	Stamp.Center = AnchorTransform.InverseTransformPosition(CenterHit.ImpactPoint);
	auto LocalAxis = [this](const FVector& WorldAxis)
	{
		return FVector(FVector::DotProduct(WorldAxis, AnchorTransform.TransformVector(FVector::ForwardVector)),
			FVector::DotProduct(WorldAxis, AnchorTransform.TransformVector(FVector::RightVector)),
			FVector::DotProduct(WorldAxis, AnchorTransform.TransformVector(FVector::UpVector)));
	};
	Stamp.AxisX = LocalAxis(TangentX);
	Stamp.AxisY = LocalAxis(TangentY);
	Stamp.AxisZ = LocalAxis(Normal);
	Stamp.Radius = Settings->BrushRadius;
	Stamp.Depth = Settings->ProjectionDistance;
	return UE::ComposableCamera::MeshEditor::EraseShapeGeometry(Settings->WorkingData, LayerId, Stamp, true, nullptr, OutDirtyBounds);
}
void FComposableCameraMeshLayerEdMode::RemoveOrphanedTriangles()
{
	if (!Settings || !Settings->WorkingData.IsConsistent())
	{
		return;
	}

	TSet<FGuid> ValidLayerIds;
	for (const FComposableCameraMeshLayerDefinition& Layer : Settings->Layers)
	{
		ValidLayerIds.Add(Layer.LayerId);
	}

	FComposableCameraMeshSurfaceAuthoringData CleanData;
	CleanData.Shapes = Settings->WorkingData.Shapes;
	CleanData.Shapes.RemoveAll([&ValidLayerIds](const auto& Shape) { return !ValidLayerIds.Contains(Shape.LayerId); });
	CleanData.Vertices.Reserve(Settings->WorkingData.Vertices.Num());
	CleanData.Indices.Reserve(Settings->WorkingData.Indices.Num());
	CleanData.TriangleLayerIds.Reserve(Settings->WorkingData.TriangleLayerIds.Num());
	const int32 TriangleCount = Settings->WorkingData.Indices.Num() / 3;
	for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
	{
		if (!ValidLayerIds.Contains(Settings->WorkingData.TriangleLayerIds[TriangleIndex]))
		{
			continue;
		}

		const int32 Offset = TriangleIndex * 3;
		const int32 SourceIndex0 = Settings->WorkingData.Indices[Offset];
		const int32 SourceIndex1 = Settings->WorkingData.Indices[Offset + 1];
		const int32 SourceIndex2 = Settings->WorkingData.Indices[Offset + 2];
		if (!Settings->WorkingData.Vertices.IsValidIndex(SourceIndex0)
			|| !Settings->WorkingData.Vertices.IsValidIndex(SourceIndex1)
			|| !Settings->WorkingData.Vertices.IsValidIndex(SourceIndex2))
		{
			continue;
		}

		const int32 TargetIndex0 = CleanData.Vertices.Add(Settings->WorkingData.Vertices[SourceIndex0]);
		const int32 TargetIndex1 = CleanData.Vertices.Add(Settings->WorkingData.Vertices[SourceIndex1]);
		const int32 TargetIndex2 = CleanData.Vertices.Add(Settings->WorkingData.Vertices[SourceIndex2]);
		CleanData.Indices.Add(TargetIndex0);
		CleanData.Indices.Add(TargetIndex1);
		CleanData.Indices.Add(TargetIndex2);
		CleanData.TriangleLayerIds.Add(Settings->WorkingData.TriangleLayerIds[TriangleIndex]);
		CleanData.TriangleShapeIds.Add(Settings->WorkingData.TriangleShapeIds.IsValidIndex(TriangleIndex)
			? Settings->WorkingData.TriangleShapeIds[TriangleIndex] : FGuid());
	}
	Settings->WorkingData = MoveTemp(CleanData);
}

void FComposableCameraMeshLayerEdMode::MarkLayerDataDirty()
{
	CancelInteraction();
	RemoveOrphanedTriangles();
	RefreshDocumentState();
}

void FComposableCameraMeshLayerEdMode::RefreshDocumentState()
{
	bDirty = Settings && Settings->DocumentRevision != SavedRevision;
	bVisualizationDirty = true;
	RebuildShapeOverlays();
	if (GEditor) { GEditor->RedrawLevelEditingViewports(); }
}

void FComposableCameraMeshLayerEdMode::PostUndo(bool bSuccess)
{
	if (!bSuccess || !Settings || bExiting) { return; }
	CancelInteraction();
	Settings->NormalizeLayers();
	RefreshPendingSaveState();
	ShapeFeedback = FText::GetEmpty();
	RefreshDocumentState();
	RefreshSelectionEditor();
	if (Toolkit.IsValid()) { StaticCastSharedPtr<FComposableCameraMeshLayerModeToolkit>(Toolkit)->RefreshDocument(); }
}

void FComposableCameraMeshLayerEdMode::BeginStroke()
{
	if (!Settings || bPainting || IsCreatingShapes()) { return; }
	StrokeStartData = Settings->WorkingData;
	StrokeStartRevision = Settings->DocumentRevision;
	StrokeTransaction = MakeUnique<FScopedTransaction>(LOCTEXT("BrushStrokeTransaction", "Paint / Erase Mesh Camera Layer"));
	Settings->Modify();
	bPainting = true;
	bHasLastPaintPosition = false;
}

void FComposableCameraMeshLayerEdMode::FinishStroke(bool bRevert)
{
	const bool bChanged = StrokeTransaction && Settings && Settings->DocumentRevision != StrokeStartRevision;
	if (StrokeTransaction)
	{
		if (bRevert && Settings)
		{
			Settings->WorkingData = MoveTemp(StrokeStartData);
			Settings->DocumentRevision = StrokeStartRevision;
			RefreshDocumentState();
		}
		if (!Settings || bRevert || Settings->DocumentRevision == StrokeStartRevision) { StrokeTransaction->Cancel(); }
		StrokeTransaction.Reset();
		StrokeStartData.Reset();
	}
	bPainting = false;
	bHasLastPaintPosition = false;
	if (bChanged && !bRevert)
	{
		// Every stamp has already updated the coverage cache. Rebuilding the entire
		// document on release repeats that work and stalls the next viewport render.
		bDirty = Settings->DocumentRevision != SavedRevision;
		if (GEditor) { GEditor->RedrawLevelEditingViewports(); }
	}
}

bool FComposableCameraMeshLayerEdMode::HasSelectedShape() const
{
	return Settings && SelectedShapeId.IsValid() && Settings->WorkingData.Shapes.ContainsByPredicate(
		[this](const auto& Shape)
		{
			return Shape.ShapeId == SelectedShapeId && Shape.LayerId == Settings->GetActiveLayerId()
				&& (Shape.Type == EComposableCameraMeshShapeType::Polygon ? Shape.ControlPoints.Num() >= 3 : Shape.ControlPoints.Num() == 2);
		});
}

void FComposableCameraMeshLayerEdMode::DeleteSelectedShape()
{
	if (!HasSelectedShape()) { return; }
	ResetInteraction();
	const FScopedTransaction Transaction(LOCTEXT("DeleteShapeTransaction", "Delete Mesh Camera Shape"));
	Settings->Modify();
	UE::ComposableCamera::MeshEditor::RemoveShapeGeometry(Settings->WorkingData, SelectedShapeId);
	Settings->WorkingData.Shapes.RemoveAll([this](const auto& Shape) { return Shape.ShapeId == SelectedShapeId; });
	SelectedShapeId.Invalidate();
	Settings->TouchDocument();
	RefreshDocumentState();
	RefreshSelectionEditor();
}

void FComposableCameraMeshLayerEdMode::HandleToolSettingsChanged()
{
	// Released regions captured their own options. Changing tools cancels only
	// the active drag, so a queued Rectangle survives switching to Circle/Select.
	ResetInteraction();
	RebuildShapeOverlays();
	RefreshSelectionEditor();
}

void FComposableCameraMeshLayerEdMode::BeforeSelectionEdit()
{
	ResetInteraction();
	if (Settings) { Settings->Modify(); }
}

void FComposableCameraMeshLayerEdMode::ApplySelectedLayer()
{
	if (!Settings || !SelectionEditor || !Settings->Layers.IsValidIndex(Settings->ActiveLayerIndex)) { return; }
	const FGuid LayerId = Settings->GetActiveLayerId();
	Settings->Layers[Settings->ActiveLayerIndex] = SelectionEditor->Layer;
	Settings->Layers[Settings->ActiveLayerIndex].LayerId = LayerId;
	Settings->NotifyLayerDataChanged();
	if (Toolkit.IsValid()) { StaticCastSharedPtr<FComposableCameraMeshLayerModeToolkit>(Toolkit)->RefreshDocument(); }
}

void FComposableCameraMeshLayerEdMode::RefreshSelectionEditor()
{
	if (!Settings || !SelectionEditor) { return; }
	if (Settings->Layers.IsValidIndex(Settings->ActiveLayerIndex)) { SelectionEditor->Layer = Settings->Layers[Settings->ActiveLayerIndex]; }
	SelectionEditor->bHasShape = HasSelectedShape();
	if (SelectionEditor->bHasShape)
	{
		const FComposableCameraMeshAuthoredShape& Shape = *Settings->WorkingData.Shapes.FindByPredicate(
			[this](const auto& Item) { return Item.ShapeId == SelectedShapeId; });
		SelectionEditor->ShapeType = Shape.Type;
		SelectionEditor->Position = FVector2D::ZeroVector;
		for (const FVector2D& Point : Shape.ControlPoints) { SelectionEditor->Position += Point; }
		if (!Shape.ControlPoints.IsEmpty()) { SelectionEditor->Position /= Shape.ControlPoints.Num(); }
		if (Shape.Type == EComposableCameraMeshShapeType::Rectangle && Shape.ControlPoints.Num() == 2)
		{
			const FVector2D Delta = Shape.ControlPoints[1] - Shape.ControlPoints[0];
			SelectionEditor->Size = FVector2D(FMath::Abs(Delta.X), FMath::Abs(Delta.Y));
		}
		else if (Shape.Type == EComposableCameraMeshShapeType::Circle && Shape.ControlPoints.Num() == 2)
		{
			SelectionEditor->Position = Shape.ControlPoints[0];
			SelectionEditor->Radius = (Shape.ControlPoints[1] - Shape.ControlPoints[0]).Size();
		}
		SelectionEditor->Vertices = Shape.ControlPoints;
	}
	if (Toolkit.IsValid()) { StaticCastSharedPtr<FComposableCameraMeshLayerModeToolkit>(Toolkit)->RefreshSelectionDetails(); }
}

void FComposableCameraMeshLayerEdMode::ApplySelectedShape(const FPropertyChangedEvent& Event)
{
	if (!HasSelectedShape() || !SelectionEditor) { return; }
	FComposableCameraMeshAuthoredShape Shape = *Settings->WorkingData.Shapes.FindByPredicate(
		[this](const auto& Item) { return Item.ShapeId == SelectedShapeId; });
	const FName Name = Event.MemberProperty ? Event.MemberProperty->GetFName() : Event.GetPropertyName();
	if (Name == GET_MEMBER_NAME_CHECKED(UComposableCameraMeshLayerSelection, Position))
	{
		FVector2D PreviousPosition = Shape.Type == EComposableCameraMeshShapeType::Circle ? Shape.ControlPoints[0] : FVector2D::ZeroVector;
		if (Shape.Type != EComposableCameraMeshShapeType::Circle)
		{
			for (const auto& Point : Shape.ControlPoints) { PreviousPosition += Point; }
			PreviousPosition /= Shape.ControlPoints.Num();
		}
		for (auto& Point : Shape.ControlPoints) { Point += SelectionEditor->Position - PreviousPosition; }
	}
	else if (Shape.Type == EComposableCameraMeshShapeType::Rectangle)
	{
		Shape.ControlPoints = {SelectionEditor->Position - SelectionEditor->Size * 0.5, SelectionEditor->Position + SelectionEditor->Size * 0.5};
	}
	else if (Shape.Type == EComposableCameraMeshShapeType::Circle)
	{
		Shape.ControlPoints[1] = Shape.ControlPoints[0] + (Shape.ControlPoints[1] - Shape.ControlPoints[0]).GetSafeNormal() * SelectionEditor->Radius;
	}
	else { Shape.ControlPoints = SelectionEditor->Vertices; }
	// Details already owns the transaction and PreEditChange saved the document.
	CommitEditedShape(MoveTemp(Shape), false);
	RefreshSelectionEditor();
}

void FComposableCameraMeshLayerEdMode::UpdateEditPreview()
{
	if (!Settings || !bEditingShape || !bHasHoverHit) { return; }
	const FComposableCameraMeshAuthoredShape* Original = Settings->WorkingData.Shapes.FindByPredicate(
		[this](const auto& Item) { return Item.ShapeId == SelectedShapeId; });
	if (!Original) { CancelInteraction(); return; }
	EditShape = *Original;
	const FVector LocalHit = AnchorTransform.InverseTransformPosition(HoverHit.ImpactPoint);
	const FVector2D Position = UE::ComposableCamera::MeshEditor::SnapShapePoint(FVector2D(LocalHit.X, LocalHit.Y), Settings->ShapeGridSize);
	if (EditShape.ControlPoints.IsValidIndex(EditControlIndex))
	{
		const FVector2D Delta = Position - EditShape.ControlPoints[EditControlIndex];
		EditShape.ControlPoints[EditControlIndex] = Position;
		if (EditShape.Type == EComposableCameraMeshShapeType::Circle && EditControlIndex == 0) { EditShape.ControlPoints[1] += Delta; }
	}
	else
	{
		const FVector2D Delta = UE::ComposableCamera::MeshEditor::SnapShapePoint(Position - EditDragStart, Settings->ShapeGridSize);
		for (auto& Point : EditShape.ControlPoints) { Point += Delta; }
	}
	UE::ComposableCamera::MeshEditor::BuildShapeOutline(EditShape, ShapePreview);
}

void FComposableCameraMeshLayerEdMode::RebuildShapeOverlays()
{
	ShapeOverlays.Reset();
	if (!Settings) { return; }
	const FVector Up = AnchorTransform.TransformVectorNoScale(FVector::UpVector).GetSafeNormal();
	for (const auto& Shape : Settings->WorkingData.Shapes)
	{
		if (Shape.LayerId != Settings->GetActiveLayerId()) { continue; }
		FShapeOverlay& Overlay = ShapeOverlays.AddDefaulted_GetRef();
		Overlay.ShapeId = Shape.ShapeId;
		TArray<FVector2D> Outline;
		UE::ComposableCamera::MeshEditor::BuildShapeOutline(Shape, Outline);
		for (const auto& Point : Outline) { Overlay.Outline.Add(AnchorTransform.TransformPosition(UE::ComposableCamera::MeshEditor::ShapePlanePosition(Shape, Point)) + Up * 2.0); }
		for (const auto& Point : Shape.ControlPoints) { Overlay.Controls.Add(AnchorTransform.TransformPosition(UE::ComposableCamera::MeshEditor::ShapePlanePosition(Shape, Point)) + Up * 2.0); }
	}
}

void FComposableCameraMeshLayerEdMode::Render(
	const FSceneView* View,
	FViewport* Viewport,
	FPrimitiveDrawInterface* PDI)
{
	FEdMode::Render(View, Viewport, PDI);
	if (Settings)
	{
		if (bVisualizationDirty)
		{
			UE::ComposableCamera::MeshEditor::BuildAuthoringVisualization(
				Settings->WorkingData,
				Settings->Layers,
				Visualization);
			bVisualizationDirty = false;
		}
		UE::ComposableCamera::MeshEditor::DrawVisualization(
			PDI,
			AnchorTransform,
			Visualization,
			Settings->Layers);
		for (int32 Index = 0; Index < PendingShapes.Num(); ++Index)
		{
			UE::ComposableCamera::MeshEditor::DrawShapePreview(PDI, AnchorTransform, GetPendingShapePreview(Index));
		}
	}

	if (Settings && Settings->Tool == EComposableCameraMeshDrawTool::Select)
	{
		for (const FShapeOverlay& Overlay : ShapeOverlays)
		{
			if (bEditingShape && Overlay.ShapeId == SelectedShapeId) { continue; }
			const bool bSelected = Overlay.ShapeId == SelectedShapeId;
			const FLinearColor Color = bSelected ? FLinearColor::Yellow : FLinearColor(0.4f, 0.8f, 1.0f);
			for (int32 Index = 0; Overlay.Outline.Num() >= 2 && Index < Overlay.Outline.Num(); ++Index)
			{
				PDI->DrawLine(Overlay.Outline[Index], Overlay.Outline[(Index + 1) % Overlay.Outline.Num()], Color, SDPG_Foreground, bSelected ? 2.5f : 1.0f);
			}
			if (bSelected)
			{
				for (int32 Index = 0; Index < Overlay.Controls.Num(); ++Index)
				{
					// Hit-proxy allocation only during editor picking, never camera evaluation.
					if (PDI->IsHitTesting()) { PDI->SetHitProxy(new HComposableCameraMeshShapeControl(Overlay.ShapeId, Index)); }
					PDI->DrawPoint(Overlay.Controls[Index], Color, 14.0f, SDPG_Foreground);
					PDI->SetHitProxy(nullptr);
				}
			}
		}
	}
	if (bEditingShape && Settings)
	{
		const FVector Up = AnchorTransform.TransformVectorNoScale(FVector::UpVector).GetSafeNormal();
		for (int32 Index = 0; ShapePreview.Num() >= 2 && Index < ShapePreview.Num(); ++Index)
		{
			PDI->DrawLine(AnchorTransform.TransformPosition(UE::ComposableCamera::MeshEditor::ShapePlanePosition(EditShape, ShapePreview[Index])) + Up * 2.0,
				AnchorTransform.TransformPosition(UE::ComposableCamera::MeshEditor::ShapePlanePosition(EditShape, ShapePreview[(Index + 1) % ShapePreview.Num()])) + Up * 2.0,
				bHasHoverHit ? FLinearColor::Yellow : FLinearColor::Red, SDPG_Foreground, 2.5f);
		}
	}
	else if (bDrawingShape && Settings)
	{
		const FVector Up = AnchorTransform.TransformVectorNoScale(FVector::UpVector).GetSafeNormal();
		const FLinearColor Color = bHasHoverHit ? FLinearColor::Yellow : FLinearColor::Red;
		for (int32 Index = 0; ShapePreview.Num() >= 2 && Index < ShapePreview.Num(); ++Index)
		{
			PDI->DrawLine(GetShapePlanePosition(ShapePreview[Index]) + Up * 2.0,
				GetShapePlanePosition(ShapePreview[(Index + 1) % ShapePreview.Num()]) + Up * 2.0,
				Color, SDPG_Foreground, 2.0f);
		}
		if (Settings->Tool == EComposableCameraMeshDrawTool::Polygon)
		{
			for (const FVector2D& Point : ShapePoints) { PDI->DrawPoint(GetShapePlanePosition(Point) + Up * 2.0, Color, 8.0f, SDPG_Foreground); }
		}
		else
		{
			PDI->DrawPoint(GetShapePlanePosition(ShapeStart) + Up * 2.0, Color, 8.0f, SDPG_Foreground);
		}
	}
	else if (bHasHoverHit && Settings && (Settings->Tool == EComposableCameraMeshDrawTool::Brush || Settings->Tool == EComposableCameraMeshDrawTool::Erase))
	{
		const FVector Normal = HoverHit.ImpactNormal.GetSafeNormal();
		FVector TangentX = FVector::CrossProduct(FVector::UpVector, Normal).GetSafeNormal();
		if (TangentX.IsNearlyZero())
		{
			TangentX = FVector::ForwardVector;
		}
		const FVector TangentY = FVector::CrossProduct(Normal, TangentX).GetSafeNormal();
		const bool bTemporaryErase = Viewport && (Viewport->KeyState(EKeys::LeftShift) || Viewport->KeyState(EKeys::RightShift));
		const FLinearColor Color = Settings->Tool == EComposableCameraMeshDrawTool::Erase || bTemporaryErase ? FLinearColor::Red : FLinearColor::White;
		DrawCircle(PDI,
			HoverHit.ImpactPoint + Normal * 2.0,
			TangentX,
			TangentY,
			Color,
			Settings->BrushRadius,
			32,
			SDPG_Foreground,
			1.5f);
	}
}

FText FComposableCameraMeshLayerEdMode::GetStatusText() const
{
	FText Status;
	for (const FText& Detail : { ShapeMeasurement, ShapeFeedback })
	{
		if (!Detail.IsEmpty())
		{
			Status = Status.IsEmpty() ? Detail : FText::Format(LOCTEXT("StatusWithDetail", "{0}\n{1}"), Status, Detail);
		}
	}
	return Status;
}

FText FComposableCameraMeshLayerEdMode::GetDocumentInfoText() const
{
	const FString LevelName = TargetLevel.IsValid()
		? TargetLevel->GetOutermost()->GetName()
		: TEXT("<no level>");
	return FText::Format(
		LOCTEXT("DocumentInfo", "Level: {0}\nLayers: {1}  Triangles: {2}\nState: {3}"),
		FText::FromString(LevelName),
		FText::AsNumber(Settings ? Settings->Layers.Num() : 0),
		FText::AsNumber(Settings ? Settings->WorkingData.Indices.Num() / 3 : 0),
		IsCreatingShapes() ? LOCTEXT("ShapePreparing", "Completing floor projection")
			: IsDirty() ? LOCTEXT("Dirty", "Unsaved") : LOCTEXT("Saved", "Saved"));
}

FText FComposableCameraMeshLayerEdMode::GetToolInstructions(EComposableCameraMeshDrawTool Tool)
{
	switch (Tool)
	{
	case EComposableCameraMeshDrawTool::Rectangle:
		return LOCTEXT("RectangleInstructions", "Drag between opposite corners on the floor; release to add a rectangle. Esc cancels.\nShape Grid Size snaps corners to the Level document's XY grid.");
	case EComposableCameraMeshDrawTool::Circle:
		return LOCTEXT("CircleInstructions", "Drag from center to edge on the floor; release to add a circle. Esc cancels.\nShape Grid Size snaps the center and radius.");
	case EComposableCameraMeshDrawTool::Polygon:
		return LOCTEXT("PolygonInstructions", "Click floor points to draw a polygon. Enter, double-click, or click the first point to finish.\nBackspace removes the last point. Esc cancels. Shape Grid Size snaps vertices.");
	case EComposableCameraMeshDrawTool::Brush:
		return LOCTEXT("BrushInstructions", "Drag with left mouse to paint coverage in the selected Layer.\nShift + left mouse temporarily erases. Ctrl+Z / Ctrl+Y undo / redo.");
	case EComposableCameraMeshDrawTool::Select:
		return LOCTEXT("SelectInstructions", "Click a Shape in the selected Layer. Drag its interior to move, or a yellow control to resize / edit vertices.\nDetails provides exact dimensions. Delete removes the Shape. Ctrl+Z / Ctrl+Y undo / redo.");
	case EComposableCameraMeshDrawTool::Erase:
		return LOCTEXT("EraseInstructions", "Drag to erase coverage in the selected Layer. Brush Radius controls the eraser.\nCtrl+Z restores the whole stroke; Ctrl+Y reapplies it.");
	}
	return FText::GetEmpty();
}

#undef LOCTEXT_NAMESPACE
