// Copyright 2026 Sulley. All Rights Reserved.

#include "MeshCamera/ComposableCameraMeshLayerEdMode.h"

#include "ComposableCameraSystemEditorModule.h"
#include "MeshCamera/ComposableCameraMeshLayerEditWork.h"
#include "HAL/PlatformTime.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "EditorViewportClient.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "MeshCamera/ComposableCameraMeshLayerModeToolkit.h"
#include "MeshCamera/ComposableCameraMeshLayerEditPreview.h"
#include "MeshCamera/ComposableCameraMeshLayerAuthoringIndex.h"
#include "MeshCamera/ComposableCameraMeshLayerStrokeCoverage.h"
#include "MeshCamera/ComposableCameraMeshLayerDocumentBuild.h"
#include "MeshCamera/ComposableCameraMeshLayerSavedPreview.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Utilities/ComposableCameraMeshLayerTool.h"
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
	UE::ComposableCamera::MeshEditor::FPreparedEditPreview Prepared;
	bool bFull = false;
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

struct FComposableCameraMeshStrokeTask
{
	enum class EPhase : uint8 { Project, Erase };
	FComposableCameraMeshLayerEdMode::FStrokeStamp Stamp;
	UE::ComposableCamera::MeshEditor::FProjectedShapeBuild Projection;
	UE::ComposableCamera::MeshEditor::FEraseGeometryBuild Erase;
	FBox2D DirtyBounds = FBox2D(ForceInit);
	FVector TangentX, TangentY;
	EPhase Phase = EPhase::Project;
	int32 FirstAddedTriangle = INDEX_NONE;
};

struct FComposableCameraMeshLiveShapeBase
{
	FComposableCameraMeshSurfaceAuthoringData Source;
	TSharedPtr<const UE::ComposableCamera::MeshEditor::FEditPreviewCheckpoint, ESPMode::ThreadSafe> Checkpoint;
	FGuid Revision;
};

struct FComposableCameraMeshLiveShapeTask
{
	FComposableCameraMeshAuthoredShape Shape;
	UE::ComposableCamera::MeshEditor::FProjectedShapeBuild Projection;
	TFuture<UE::ComposableCamera::MeshEditor::FPreparedEditPreview> Future;
	TSharedRef<std::atomic_bool, ESPMode::ThreadSafe> Alive = MakeShared<std::atomic_bool, ESPMode::ThreadSafe>(true);
	~FComposableCameraMeshLiveShapeTask() { Alive->store(false, std::memory_order_relaxed); }
};

FComposableCameraMeshLayerEdMode::FComposableCameraMeshLayerEdMode()
	: EditPreview(MakeUnique<UE::ComposableCamera::MeshEditor::FMeshLayerEditPreview>()),
	  AuthoringIndex(MakeUnique<UE::ComposableCamera::MeshEditor::FMeshLayerAuthoringIndex>()),
	  StrokeCoverage(MakeUnique<UE::ComposableCamera::MeshEditor::FMeshLayerStrokeCoverage>()),
	  DocumentBuild(MakeUnique<UE::ComposableCamera::MeshEditor::FMeshLayerDocumentBuild>()),
	  OpeningRegionCoverage(MakeUnique<UE::ComposableCamera::MeshEditor::FMeshLayerStrokeCoverage>())
{ StrokeCoverage->EnablePreparedPreview(); OpeningRegionCoverage->EnableRegionOnlyPreview(); }
FComposableCameraMeshLayerEdMode::~FComposableCameraMeshLayerEdMode() { RetireEditingCheckpoint(); }

void FComposableCameraMeshLayerEdMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	FEdMode::Tick(ViewportClient, DeltaTime);
	bool bAdvanced = false;
	if (LastDocumentTickFrame != GFrameCounter && (bVisualizationDirty || bEditPreviewDirty || DocumentBuild->HasPending()))
	{
		LastDocumentTickFrame = GFrameCounter;
		const FVector LocalView = AnchorTransform.InverseTransformPosition(ViewportClient ? ViewportClient->GetViewLocation() : FVector::ZeroVector);
		AdvanceDocumentPreview(FVector2D(LocalView.X, LocalView.Y));
		bAdvanced = true;
		if (ViewportClient) { ViewportClient->Invalidate(false, false); }
	}
	if ((bPainting || StrokeCoverage->HasPending() || EditPreview->HasQueuedUpdates()) && LastStrokeTickFrame != GFrameCounter)
	{
		LastStrokeTickFrame = GFrameCounter;
		AdvancePainting();
		bAdvanced = true;
		if (ViewportClient) { ViewportClient->Invalidate(false, false); }
	}
	// Multiple viewports tick the same mode. Spend the projection budget once.
	if (!PendingShapes.IsEmpty() && LastShapeTickFrame != GFrameCounter)
	{
		LastShapeTickFrame = GFrameCounter;
		AdvanceShapeCreation();
		bAdvanced = true;
	}
	RememberPreview();
	if ((bLiveShapeRequested || LiveShapeTask) && LastLiveShapeTickFrame != GFrameCounter)
	{
		LastLiveShapeTickFrame = GFrameCounter; AdvanceLiveShapePreview(); bAdvanced = true;
	}
	if (bAdvanced) { PendingRedrawFrames = 2; }
	if (PendingRedrawFrames > 0 && LastRedrawFrame != GFrameCounter)
	{
		LastRedrawFrame = GFrameCounter;
		if (GEditor) { GEditor->RedrawLevelEditingViewports(false); }
		if (!bAdvanced) { --PendingRedrawFrames; }
	}
}

void FComposableCameraMeshLayerEdMode::Enter()
{
	FEdMode::Enter();
	FComposableCameraMeshLayerTool::NotifyEditModeChanged(true);
	bExiting = false;

	Settings = NewObject<UComposableCameraMeshLayerToolSettings>(GetTransientPackage(), NAME_None, RF_Transactional);
	SelectionEditor = NewObject<UComposableCameraMeshLayerSelection>(GetTransientPackage(), NAME_None, RF_Transactional);
	Settings->OnBeforeEdit.BindRaw(this, &FComposableCameraMeshLayerEdMode::ResetInteraction);
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
	CancelDocumentBuild(); StrokeCoverage->Cancel();
	RetireEditingCheckpoint();
	EditPreview->Reset();
	AuthoringIndex->Reset();
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
		Settings->OnBeforeEdit.Unbind();
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
	FComposableCameraMeshLayerTool::NotifyEditModeChanged(false);
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
	bEditPreviewDirty = true;
	CancelDocumentBuild();
	AuthoringIndex->Reset();
	const auto* Actor = StorageActor.Get();
	DocumentBuild->StartResident(Settings->WorkingData, Settings->Layers, Settings->DocumentRevision,
		Actor ? &Actor->GetEditorPreview() : nullptr);
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
	TRACE_CPUPROFILER_EVENT_SCOPE(CCS_MeshLayers_Save);
	const double SaveStarted = FPlatformTime::Seconds();
	if (IsCreatingShapes()) { return false; }
	if (!Settings || !TargetLevel.IsValid())
	{
		return false;
	}
	CancelInteraction();

	TArray<FGuid> PreviousIds;
	for (const auto& Layer : Settings->Layers) { PreviousIds.Add(Layer.LayerId); }
	Settings->NormalizeLayers();
	for (int32 Index = 0; Index < PreviousIds.Num(); ++Index)
	{
		if (PreviousIds[Index] != Settings->Layers[Index].LayerId)
		{
			CancelDocumentBuild(); StrokeCoverage->Cancel(); bVisualizationDirty = bEditPreviewDirty = true;
			break;
		}
	}
	// Complete existing exact work before compaction invalidates its source-count snapshot.
	// Reuse CPU coverage after the explicit interaction boundary; this step need not drain GPU publication.
	if (DocumentBuild->IsResident()) { AdvanceDocumentPreview(FVector2D::ZeroVector, true); }
	AdvanceStrokeCoverage(true);
	RemoveOrphanedTriangles();
	const double FinalizeFinished = FPlatformTime::Seconds();
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
	const double RuntimeFinished = FPlatformTime::Seconds();
	const bool bKeptStoredPreview = !bVisualizationDirty && !StrokeCoverage->HasPending() && !DocumentBuild->HasPending()
		&& UE::ComposableCamera::MeshEditor::MatchesEditorPreview(Actor->GetEditorPreview(), GetVisualization(), Settings->Layers);
	bool bReusedCoverage = bKeptStoredPreview;
	if (!bKeptStoredPreview) { Actor->SetEditorPreview(PrepareSavePreview(bReusedCoverage)); }
	const double PreviewFinished = FPlatformTime::Seconds();
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
	const double PackagesFinished = FPlatformTime::Seconds();
	if (Result != FEditorFileUtils::PR_Success)
	{
		UE_LOG(LogComposableCameraSystemEditor, Warning,
			TEXT("Mesh layer data was applied but its package was not saved."));
		return false;
	}

	bDirty = false;
	CaptureSavedDocument();
	const double SaveFinished = FPlatformTime::Seconds();
	UE_LOG(LogComposableCameraSystemEditor, Log,
		TEXT("Saved %d mesh layers and %d authored triangles to Level '%s'. Save ms: finalize=%.2f runtime=%.2f preview=%.2f packages=%.2f checkpoint=%.2f total=%.2f; reusedCoverage=%d keptStoredPreview=%d vertices=%d/%d."),
		Settings->Layers.Num(),
		Settings->WorkingData.Indices.Num() / 3,
		*TargetLevel->GetOutermost()->GetName(), (FinalizeFinished - SaveStarted) * 1000,
		(RuntimeFinished - FinalizeFinished) * 1000, (PreviewFinished - RuntimeFinished) * 1000,
		(PackagesFinished - PreviewFinished) * 1000, (SaveFinished - PackagesFinished) * 1000,
		(SaveFinished - SaveStarted) * 1000, bReusedCoverage, bKeptStoredPreview,
		Settings->WorkingData.Vertices.Num(), Actor->GetRuntimeData().Vertices.Num());
	return true;
}

FComposableCameraMeshSurfaceEditorPreview FComposableCameraMeshLayerEdMode::PrepareSavePreview(bool& bOutReusedCoverage)
{
	using namespace UE::ComposableCamera::MeshEditor;
	TRACE_CPUPROFILER_EVENT_SCOPE(CCS_MeshLayers_SavePreview);
	bOutReusedCoverage = Settings->WorkingData.IsConsistent() && !bVisualizationDirty
		&& !StrokeCoverage->HasPending() && !DocumentBuild->HasPending();
	if (bOutReusedCoverage) { return SaveEditorPreview(GetVisualization(), Settings->Layers); }
	// History misses/structural invalidation have no reusable exact cache. Resolve once
	// and retain it for subsequent editing rather than rebuilding again after Save.
	RetireEditingCheckpoint();
	AsyncMeshLayerEdit([Retired = MoveTemp(Visualization)]() mutable { Retired.Reset(); });
	BuildAuthoringVisualization(Settings->WorkingData, Settings->Layers, Visualization);
	bVisualizationDirty = false; bEditPreviewDirty = true; PreviewRevision.Invalidate();
	return SaveEditorPreview(Visualization, Settings->Layers);
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
	RememberPreview();
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
	RememberPreview(); // Undo of Discard can reuse the committed edited display too.
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
		if (Key == EKeys::Z && !ViewportClient->IsShiftPressed())
		{
			if (!DeferredStrokes.IsEmpty()) { DeferredStrokes.Pop(); return true; }
			if (!PendingShapes.IsEmpty())
			{
				RetirePendingShape(PendingShapes.Num() - 1);
				if (GEditor) { GEditor->RedrawLevelEditingViewports(false); }
				return true;
			}
			if (bDrawingShape || bEditingShape || bLiveShapeRequested || LiveShapeTask || bLiveShapeVisible)
			{ ResetInteraction(); return true; }
			// Undo the current uncommitted stroke without first projecting/meshing its backlog.
			if (bPainting && (StrokeTask || QueuedStampIndex < QueuedStamps.Num())) { FinishStroke(true); return true; }
		}
		PrepareUndo();
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
		if (!DeferredStrokes.IsEmpty() && !DeferredStrokes.Last().bReleased)
		{ DeferredStrokes.Last().bReleased = true; return true; }
		if (bEditingShape)
		{
			UpdateHoverHit(ViewportClient);
			UpdateEditPreview();
			if (bHasHoverHit) { QueueShapeCreation(EditShape, GetWorld()); }
			ResetInteraction();
			return true;
		}
		if (bPainting)
		{
			bStrokeReleased = true;
			if (!StrokeTask && QueuedStampIndex >= QueuedStamps.Num()) { FinishStroke(false, true); }
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
				if (bPainting) { QueuePaintAtHover(ViewportClient); }
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
	if (bPainting && (!bStrokeReleased || !DeferredStrokes.IsEmpty()))
	{
		QueuePaintAtHover(ViewportClient);
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

bool FComposableCameraMeshLayerEdMode::TraceLayerSurface(const UWorld& World, const FGuid& LayerId,
	const FVector& Start, const FVector& End, const FCollisionQueryParams& QueryParams, FHitResult& OutHit) const
{
	if (!Settings || !LayerId.IsValid()) { return false; }
	const auto* Layer = Settings->Layers.FindByPredicate([&LayerId](const auto& Item) { return Item.LayerId == LayerId; });
	return Layer && Layer->TraceChannel.GetValue() < ECC_OverlapAll_Deprecated
		&& World.LineTraceSingleByChannel(OutHit, Start, End, Layer->TraceChannel.GetValue(), QueryParams);
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

	bHasHoverHit = TraceLayerSurface(*GetWorld(), Settings->GetActiveLayerId(),
		Cursor.GetOrigin(),
		Cursor.GetOrigin() + Cursor.GetDirection() * HALF_WORLD_MAX,
		QueryParams, HoverHit);
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
	CancelPendingShapes();
	ResetInteraction();
}

void FComposableCameraMeshLayerEdMode::ResetInteraction()
{
	CancelLiveShapePreview();
	FinishStroke();
	RememberPreview();
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
	FComposableCameraMeshAuthoredShape Draft;
	Draft.LayerId = ShapeLayerId;
	Draft.PlaneOrigin = AnchorTransform.InverseTransformPosition(ShapeStartHit.ImpactPoint);
	Draft.PlaneNormal = FVector(
		FVector::DotProduct(ShapeStartHit.ImpactNormal, AnchorTransform.TransformVector(FVector::ForwardVector)),
		FVector::DotProduct(ShapeStartHit.ImpactNormal, AnchorTransform.TransformVector(FVector::RightVector)),
		FVector::DotProduct(ShapeStartHit.ImpactNormal, AnchorTransform.TransformVector(FVector::UpVector))).GetSafeNormal();
	Draft.SampleSpacing = Settings->ShapeSampleSpacing; Draft.ProjectionDistance = Settings->ProjectionDistance;
	Draft.MinimumFloorNormalZ = Settings->MinimumFloorNormalZ; Draft.CircleSegments = Settings->CircleSegments;
	if (Settings->Tool == EComposableCameraMeshDrawTool::Rectangle) { Draft.Type = EComposableCameraMeshShapeType::Rectangle; Draft.ControlPoints = {ShapeStart, ShapeEnd}; }
	else if (Settings->Tool == EComposableCameraMeshDrawTool::Circle) { Draft.Type = EComposableCameraMeshShapeType::Circle; Draft.ControlPoints = {ShapeStart, ShapeEnd}; }
	else { Draft.Type = EComposableCameraMeshShapeType::Polygon; Draft.ControlPoints = ShapePreview; }
	RequestLiveShapePreview(MoveTemp(Draft));
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
	const double LocalErrorTolerance = 1.0 / FMath::Max(AnchorTransform.GetScale3D().GetAbsMax(), UE_DOUBLE_SMALL_NUMBER);
	const EShapeBuildResult Started = Task->Build.Begin(Outline, Shape.SampleSpacing, Shape.LayerId, 16384, LocalErrorTolerance);
	if (Started != EShapeBuildResult::Success)
	{
		ShapeFeedback = Started == EShapeBuildResult::TooComplex
			? LOCTEXT("ShapeTooDense", "Surface is too complex. Draw a smaller region or increase Shape Sample Spacing.")
			: LOCTEXT("InvalidShape", "Shape needs a nonzero area and a simple outline without crossing edges.");
		return false;
	}
	if (Shape.ShapeId.IsValid())
	{
		for (int32 Index = PendingShapes.Num() - 1; Index >= 0; --Index)
		{ if (PendingShapes[Index]->Shape.ShapeId == Shape.ShapeId) { RetirePendingShape(Index); } }
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

void FComposableCameraMeshLayerEdMode::RetirePendingShape(int32 Index)
{
	auto Retired = MoveTemp(PendingShapes[Index]); PendingShapes.RemoveAt(Index, 1, EAllowShrinking::No);
	Retired->Alive->store(false, std::memory_order_relaxed);
	UE::ComposableCamera::MeshEditor::AsyncMeshLayerEdit([Retired = MoveTemp(Retired)]() mutable { Retired.Reset(); });
}

void FComposableCameraMeshLayerEdMode::CancelPendingShapes()
{
	if (PendingShapes.IsEmpty()) { return; }
	for (const auto& Task : PendingShapes) { Task->Alive->store(false, std::memory_order_relaxed); }
	UE::ComposableCamera::MeshEditor::AsyncMeshLayerEdit([Retired = MoveTemp(PendingShapes)]() mutable { Retired.Reset(); });
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
			RetirePendingShape(0);
			ApplyCreatedShape(MoveTemp(Completed.Data), MoveTemp(Completed.Visualization), ShapeId, bPartial, &Completed.Prepared, Completed.bFull);
			return;
		}
		// An existing Shape may have been edited while coverage was computed.
		// Rebase plain geometry on the latest document instead of restoring stale
		// source, dropping the new region, or repeating collision projection.
		AsyncMeshLayerEdit([Retired = MoveTemp(Task.Future)]() mutable { Retired = TFuture<FComposableCameraMeshShapeCreationResult>(); });
	}
	const FVector Up = AnchorTransform.TransformVectorNoScale(FVector::UpVector).GetSafeNormal();
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CCSMeshLayerShape), true);
	if (AComposableCameraMeshSurfaceStorageActor* Actor = StorageActor.Get()) { QueryParams.AddIgnoredActor(Actor); }
	const bool bFinished = Task.Build.Advance([this, World, Up, &QueryParams, &Task](const FVector2D& Point, FVector& LocalPosition)
	{
		const FVector Desired = AnchorTransform.TransformPosition(ShapePlanePosition(Task.Shape, Point));
		FHitResult Hit;
		if (!TraceLayerSurface(*World, Task.Shape.LayerId, Desired + Up * Task.Shape.ProjectionDistance,
			Desired - Up * Task.Shape.ProjectionDistance, QueryParams, Hit)
			|| FVector::DotProduct(Hit.ImpactNormal, Up) < Task.Shape.MinimumFloorNormalZ) { return false; }
		LocalPosition = AnchorTransform.InverseTransformPosition(Hit.ImpactPoint);
		return true;
	}, 256, 0.004);
	if (!bFinished) { return; }
	const EShapeBuildResult Result = Task.Build.GetResult();
	if (Result != EShapeBuildResult::Success && Result != EShapeBuildResult::PartialSurface)
	{
		RetirePendingShape(0);
		ShapeFeedback = Result == EShapeBuildResult::NoSurface
			? LOCTEXT("ShapeNoFloor", "No compatible floor found inside this shape.")
			: Result == EShapeBuildResult::TooComplex
				? LOCTEXT("ShapeTooDense", "Surface is too complex. Draw a smaller region or increase Shape Sample Spacing.")
				: LOCTEXT("InvalidShape", "Shape needs a nonzero area and a simple outline without crossing edges.");
		if (GEditor) { GEditor->RedrawLevelEditingViewports(); }
		return;
	}
	Task.bPartial = Result == EShapeBuildResult::PartialSurface;
	if (Task.Geometry.Indices.IsEmpty()) { Task.Geometry = Task.Build.TakeData(); }
	Task.BaseRevision = Settings->DocumentRevision;
	FComposableCameraMeshSurfaceAuthoringData Source = Settings->WorkingData;
	const auto Checkpoint = EditingCheckpoint;
	FResolvedSurfaceVisualization Cached;
	if (!Checkpoint) { Cached = GetVisualization(); }
	const bool bRebuild = bVisualizationDirty;
	// Only GUID/enabled flags are needed for coverage. No UObject references go
	// to the worker, and all collision queries above stay on the editor thread.
	TArray<FComposableCameraMeshLayerDefinition> Layers;
	for (const auto& Layer : Settings->Layers)
	{
		auto& Snapshot = Layers.AddDefaulted_GetRef(); Snapshot.LayerId = Layer.LayerId; Snapshot.bEnabled = Layer.bEnabled;
	}
	Task.Future = UE::ComposableCamera::MeshEditor::AsyncMeshLayerEdit(
		[Source = MoveTemp(Source), Cached = MoveTemp(Cached), Layers = MoveTemp(Layers),
			Shape = Task.Shape, Geometry = Task.Geometry, Alive = Task.Alive, bRebuild, Checkpoint]() mutable
	{
		FComposableCameraMeshShapeCreationResult Completed;
		if (!Alive->load(std::memory_order_relaxed)) { return Completed; }
		if (Checkpoint) { Cached = Checkpoint->Visualization; }
		FBox2D Bounds(ForceInit);
		for (const FVector3f& Point : Geometry.Vertices) { Bounds += FVector2D(Point.X, Point.Y); }
		// Replacement uses the same resumable projection/worker path as creation.
		for (int32 Triangle = 0; Triangle < Source.TriangleShapeIds.Num(); ++Triangle)
		{
			if (Source.TriangleShapeIds[Triangle] == Shape.ShapeId)
			{ for (int32 Corner = 0; Corner < 3; ++Corner) { const auto& Point = Source.Vertices[Source.Indices[Triangle * 3 + Corner]]; Bounds += FVector2D(Point.X, Point.Y); } }
		}
		RemoveShapeGeometry(Source, Shape.ShapeId); Source.Shapes.RemoveAll([&](const auto& Item) { return Item.ShapeId == Shape.ShapeId; });
		for (const auto& Erasure : Shape.Erasures) { EraseShapeGeometry(Geometry, Shape.LayerId, Erasure, false); }
		AppendShapeGeometry(Source, Geometry, Shape.ShapeId); Source.Shapes.Add(MoveTemp(Shape));
		const double PreviousCellSize = Cached.CellSize;
		if (bRebuild) { BuildAuthoringVisualization(Source, Layers, Cached); }
		else { UpdateAuthoringVisualization(Source, Layers, Bounds, Cached); }
		Completed.bFull = bRebuild || PreviousCellSize != Cached.CellSize || !Cached.LocalBounds.bIsValid;
		Completed.Prepared = PrepareEditPreview(Cached, Layers, &Alive.Get(), Completed.bFull ? nullptr : &Bounds);
		Completed.Data = MoveTemp(Source); Completed.Visualization = MoveTemp(Cached);
		return Completed;
	});
}

void FComposableCameraMeshLayerEdMode::ApplyCreatedShape(FComposableCameraMeshSurfaceAuthoringData Data,
	UE::ComposableCamera::MeshEditor::FResolvedSurfaceVisualization Resolved, const FGuid& ShapeId, bool bPartial,
	UE::ComposableCamera::MeshEditor::FPreparedEditPreview* Prepared, bool bFull)
{
	RememberPreview();
	const FScopedTransaction Transaction(LOCTEXT("EditShapeTransaction", "Edit Mesh Camera Shape"));
	Settings->Modify();
	Settings->WorkingData = MoveTemp(Data);
	CancelDocumentBuild();
	bHistoricalPreview = false;
	AuthoringIndex->Reset();
	Settings->TouchDocument(); SelectedShapeId = ShapeId;
	RetireEditingCheckpoint(); Visualization = MoveTemp(Resolved); bVisualizationDirty = false;
	bEditPreviewDirty = true;
	if (Prepared && TargetLevel.IsValid())
	{
		bEditPreviewDirty = !EditPreview->QueuePreparedRegion(*TargetLevel.Get(), MoveTemp(*Prepared), bFull);
		EditPreview->AdvanceQueuedUpdates(*TargetLevel.Get(), AnchorTransform, Settings->Layers, 0.004);
	}
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
			if (!TraceLayerSurface(*World, Shape.LayerId, Desired + Up * Shape.ProjectionDistance,
				Desired - Up * Shape.ProjectionDistance, QueryParams, Hit)
				|| FVector::DotProduct(Hit.ImpactNormal, Up) < Shape.MinimumFloorNormalZ) { return false; }
			LocalPosition = AnchorTransform.InverseTransformPosition(Hit.ImpactPoint);
			return true;
		}, ShapeData, 16384, 1.0 / FMath::Max(AnchorTransform.GetScale3D().GetAbsMax(), UE_DOUBLE_SMALL_NUMBER));
	switch (Result)
	{
	case EShapeBuildResult::InvalidOutline:
		ShapeFeedback = LOCTEXT("InvalidShape", "Shape needs a nonzero area and a simple outline without crossing edges.");
		return false;
	case EShapeBuildResult::TooComplex:
		ShapeFeedback = LOCTEXT("ShapeTooDense", "Surface is too complex. Draw a smaller region or increase Shape Sample Spacing.");
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
	RememberPreview();
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

void FComposableCameraMeshLayerEdMode::QueuePaintAtHover(FEditorViewportClient* ViewportClient)
{
	if (!bPainting || (bStrokeReleased && DeferredStrokes.IsEmpty()) || !bHasHoverHit || !Settings || !Settings->Layers.IsValidIndex(Settings->ActiveLayerIndex)) { return; }
	FDeferredStroke* Deferred = DeferredStrokes.IsEmpty() ? nullptr : &DeferredStrokes.Last();
	if (Deferred && Deferred->bReleased) { return; }
	const double Spacing = Settings->BrushRadius * 0.35;
	const bool bHasLast = Deferred ? Deferred->bHasLastPosition : bHasLastPaintPosition;
	const FVector Last = Deferred ? Deferred->LastPosition : LastPaintWorldPosition;
	if (bHasLast && FVector::Distance(Last, HoverHit.ImpactPoint) < Spacing) { return; }
	// Landscape queues drag samples for Tick. Retain every old spacing-qualified
	// attempt, including Shift mode and options at that point; never overwrite the path.
	auto& Stamp = Deferred ? Deferred->Stamps.AddDefaulted_GetRef() : QueuedStamps.AddDefaulted_GetRef();
	const auto& Layer = Settings->Layers[Settings->ActiveLayerIndex];
	Stamp.LayerId = Layer.LayerId; Stamp.Channel = Layer.TraceChannel.GetValue();
	Stamp.Center = HoverHit.ImpactPoint; Stamp.Normal = HoverHit.ImpactNormal.GetSafeNormal();
	Stamp.Radius = Settings->BrushRadius; Stamp.ProjectionDistance = Settings->ProjectionDistance;
	Stamp.MinimumFloorNormalZ = Settings->MinimumFloorNormalZ; Stamp.Segments = FMath::Clamp(Settings->BrushSegments, 6, 32);
	Stamp.bErase = Settings->Tool == EComposableCameraMeshDrawTool::Erase || (ViewportClient && ViewportClient->IsShiftPressed());
	if (Deferred) { Deferred->LastPosition = Stamp.Center; Deferred->bHasLastPosition = true; }
	else { LastPaintWorldPosition = Stamp.Center; bHasLastPaintPosition = true; }
}

void FComposableCameraMeshLayerEdMode::AdvancePainting(int32 MaxOperations, double TimeBudgetSeconds, bool bUpdatePreview)
{
	using namespace UE::ComposableCamera::MeshEditor;
	TRACE_CPUPROFILER_EVENT_SCOPE(CCS_MeshLayers_StrokeTick);
	if (!Settings || MaxOperations <= 0 || GIsTransacting) { return; }
	if (!TargetLevel.IsValid() || !TargetLevel->GetWorld() || TargetLevel->GetWorld()->IsBeingCleanedUp())
	{
		FinishStroke(true); return;
	}
	const double Started = FPlatformTime::Seconds();
	if (DocumentBuild->IsResident() && (TimeBudgetSeconds <= 0.0 || !bOpeningIndexInstalled))
	{ AdvanceDocumentPreview(FVector2D::ZeroVector, TimeBudgetSeconds <= 0.0); }
	AdvanceOpeningRegions();
	if (bUpdatePreview) { AdvanceStrokeCoverage(TimeBudgetSeconds <= 0.0); }
	// Publication cannot sit behind a long source/coverage queue. Give an existing
	// ready tile first access to a quarter of the shared budget. Workers own only
	// plain coverage snapshots, so later source stamps do not wait for their meshes.
	if (bUpdatePreview && EditPreview->HasQueuedUpdates())
	{
		EditPreview->AdvanceQueuedUpdates(*TargetLevel.Get(), AnchorTransform, Settings->Layers,
			TimeBudgetSeconds > 0.0 ? TimeBudgetSeconds * 0.25 : 0.0);
	}
	auto Expired = [&]() { return TimeBudgetSeconds > 0.0 && FPlatformTime::Seconds() - Started >= TimeBudgetSeconds; };
	// Zero means "unlimited" to the resumable builders. An exhausted finite budget
	// must still be passed as a positive value if setup consumed its last fraction.
	auto Remaining = [&]() { return TimeBudgetSeconds > 0.0 ? FMath::Max(1.e-9, TimeBudgetSeconds - (FPlatformTime::Seconds() - Started)) : 0.0; };
	while (StrokeTask || QueuedStampIndex < QueuedStamps.Num())
	{
		if (Expired()) { break; }
		// An empty/stale equal-count index can look current before the opening index arrives.
		// Do not mutate source until that snapshot is installed, or it could overwrite an appended index.
		if (DocumentBuild->IsResident() && !bOpeningIndexInstalled) { break; }
		if (!AuthoringIndex->IsCurrent(Settings->WorkingData) && !StrokeTask)
		{
			if (DocumentBuild->IsResident()) { break; } // Early native index will arrive without an input-thread scan.
			AuthoringIndex->Build(Settings->WorkingData);
		}
		if (!StrokeTask)
		{
			StrokeTask = MakeUnique<FComposableCameraMeshStrokeTask>();
			auto& Task = *StrokeTask; Task.Stamp = QueuedStamps[QueuedStampIndex++];
			const auto& Stamp = Task.Stamp;
			Task.TangentX = FVector::CrossProduct(FVector::UpVector, Stamp.Normal).GetSafeNormal();
			if (Task.TangentX.IsNearlyZero()) { Task.TangentX = FVector::ForwardVector; }
			Task.TangentY = FVector::CrossProduct(Stamp.Normal, Task.TangentX).GetSafeNormal();
			if (Stamp.bErase)
			{
				FComposableCameraMeshEraseStamp Cut;
				Cut.Center = AnchorTransform.InverseTransformPosition(Stamp.Center);
				auto LocalAxis = [this](const FVector& Axis)
				{
					return FVector(FVector::DotProduct(Axis, AnchorTransform.TransformVector(FVector::ForwardVector)),
						FVector::DotProduct(Axis, AnchorTransform.TransformVector(FVector::RightVector)),
						FVector::DotProduct(Axis, AnchorTransform.TransformVector(FVector::UpVector)));
				};
				Cut.AxisX = LocalAxis(Task.TangentX); Cut.AxisY = LocalAxis(Task.TangentY); Cut.AxisZ = LocalAxis(Stamp.Normal);
				Cut.Radius = Stamp.Radius; Cut.Depth = Stamp.ProjectionDistance;
				if (!Task.Erase.Begin(Settings->WorkingData, Stamp.LayerId, Cut, true, AuthoringIndex.Get())) { StrokeTask.Reset(); continue; }
				// Begin only reads source. Empty indexed regions need no whole-document rollback copy.
				CaptureStrokeSourceBeforeErase();
				Task.Phase = FComposableCameraMeshStrokeTask::EPhase::Erase;
			}
			else
			{
				TArray<FVector2D> Outline;
				for (int32 Segment = 0; Segment < Stamp.Segments; ++Segment)
				{
					const double Angle = 2.0 * UE_DOUBLE_PI * Segment / Stamp.Segments;
					Outline.Add(FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Stamp.Radius);
				}
				const auto ProjectionStarted = Task.Projection.Begin(Outline, FMath::Clamp(Stamp.Radius * 0.5, 10.0, 100.0), Stamp.LayerId, 4096);
				if (ProjectionStarted != EShapeBuildResult::Success)
				{
					if (ProjectionStarted == EShapeBuildResult::TooComplex) { ShapeFeedback = LOCTEXT("BrushTooDense", "Surface is too complex for this stamp. Reduce Brush Radius."); }
					StrokeTask.Reset(); continue;
				}
			}
		}
		if (Expired()) { break; }
		auto& Task = *StrokeTask;
		const auto& Stamp = Task.Stamp;
		if (Task.Phase == FComposableCameraMeshStrokeTask::EPhase::Project)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(CCS_MeshLayers_BrushProjection);
			UWorld* World = GetWorld();
			if (!World || World->IsBeingCleanedUp() || !TargetLevel.IsValid() || TargetLevel->GetWorld() != World)
			{
				FinishStroke(true); return;
			}
			const FVector Up = AnchorTransform.TransformVectorNoScale(FVector::UpVector).GetSafeNormal();
			FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CCSMeshLayerProjection), true);
			QueryParams.bTraceComplex = true;
			if (auto* Actor = StorageActor.Get()) { QueryParams.AddIgnoredActor(Actor); }
			if (!Task.Projection.Advance([&](const FVector2D& Point, FVector& Position)
			{
				const FVector Desired = Stamp.Center + Task.TangentX * Point.X + Task.TangentY * Point.Y;
				FHitResult Hit;
				if (Stamp.Channel >= ECC_OverlapAll_Deprecated || !World->LineTraceSingleByChannel(Hit,
					Desired + Up * Stamp.ProjectionDistance, Desired - Up * Stamp.ProjectionDistance, Stamp.Channel, QueryParams)
					|| FVector::DotProduct(Hit.ImpactNormal, Up) < Stamp.MinimumFloorNormalZ) { return false; }
				Position = Hit.ImpactPoint - Stamp.Center; return true;
			}, MaxOperations, Remaining())) { break; }
			const auto Result = Task.Projection.GetResult();
			if (Result != EShapeBuildResult::Success && Result != EShapeBuildResult::PartialSurface)
			{
				if (Result == EShapeBuildResult::TooComplex) { ShapeFeedback = LOCTEXT("BrushTooDense", "Surface is too complex for this stamp. Reduce Brush Radius."); }
				StrokeTask.Reset(); continue;
			}
			ShapeFeedback = FText::GetEmpty();
			auto Geometry = Task.Projection.TakeData();
			auto& Data = Settings->WorkingData;
			const int32 Base = Data.Vertices.Num(); Task.FirstAddedTriangle = Data.TriangleLayerIds.Num();
			Data.TriangleShapeIds.SetNum(Data.TriangleLayerIds.Num());
			for (const auto& Vertex : Geometry.Vertices)
			{
				const FVector Local = AnchorTransform.InverseTransformPosition(Stamp.Center + FVector(Vertex));
				const FVector3f Stored(Local); Data.Vertices.Add(Stored); Task.DirtyBounds += FVector2D(Stored.X, Stored.Y);
			}
			for (int32 Index : Geometry.Indices) { Data.Indices.Add(Base + Index); }
			Data.TriangleLayerIds.Append(Geometry.TriangleLayerIds); Data.TriangleShapeIds.SetNum(Data.TriangleLayerIds.Num());
			AuthoringIndex->Append(Data, Task.FirstAddedTriangle);
		}
		else if (Task.Phase == FComposableCameraMeshStrokeTask::EPhase::Erase)
		{
			if (!Task.Erase.Advance(Settings->WorkingData, AuthoringIndex.Get(), MaxOperations, Remaining())) { break; }
			if (!Task.Erase.HasChanged()) { StrokeTask.Reset(); continue; }
			Task.DirtyBounds = Task.Erase.GetChangedBounds();
		}
		Settings->TouchDocument(); bDirty = Settings->DocumentRevision != SavedRevision;
		const bool bOpening = DocumentBuild->IsResident();
		if (!bOpening) { CancelDocumentBuild(); }
		if (bUpdatePreview)
		{
			const auto& Current = GetVisualization();
			StrokeCoverage->Queue(Settings->WorkingData, Settings->Layers, *AuthoringIndex, Task.DirtyBounds,
				Task.FirstAddedTriangle, bOpening ? OpeningCellSize : Current.CellSize,
				!bOpening && (bVisualizationDirty || !Current.LocalBounds.bIsValid));
			if (bOpening) { QueueOpeningRegion(Task.DirtyBounds); }
		}
		StrokeTask.Reset();
		// Captured only after index refresh, the snapshot is complete and owned.
		// Source processing no longer waits for this derived coverage work.
		if (bUpdatePreview) { AdvanceStrokeCoverage(TimeBudgetSeconds <= 0.0); }
		AdvanceOpeningRegions();
	}
	if (bUpdatePreview && !Expired() && EditPreview->HasQueuedUpdates())
	{
		EditPreview->AdvanceQueuedUpdates(*TargetLevel.Get(), AnchorTransform, Settings->Layers, Remaining());
	}
	if (StrokeTask || QueuedStampIndex < QueuedStamps.Num()) { return; }
	QueuedStamps.Reset(); QueuedStampIndex = 0;
	// A committed stroke need not hold its transaction or delay the next press
	// just because derived display is still publishing. Tick services it independently.
	if (bStrokeReleased) { FinishStroke(false, true); }
	if (!bUpdatePreview) { return; }
	if (StrokeCoverage->HasPending() || EditPreview->HasQueuedUpdates()) { return; }
	bEditPreviewDirty = !EditPreview->IsReadyFor(TargetLevel.Get());
	if (!bVisualizationDirty && !bPainting && !bEditPreviewDirty && !DocumentBuild->HasPending())
	{
		bHistoricalPreview = false;
		PreviewRevision = Settings->DocumentRevision;
		RememberPreview();
	}
}

void FComposableCameraMeshLayerEdMode::AdvanceStrokeCoverage(bool bFlush)
{
	if (DocumentBuild->IsResident()) { return; } // Pending FIFO deltas will consume its exact base, never reclip the whole source.
	FBox2D DirtyBounds(ForceInit); bool bFull = false;
	for (;;)
	{
		UE::ComposableCamera::MeshEditor::FPreparedEditPreview Prepared;
		bool bPreparedFull = false;
		while (StrokeCoverage->TakePrepared(Prepared, bPreparedFull))
		{
			bEditPreviewDirty = !EditPreview->QueuePreparedRegion(*TargetLevel.Get(), MoveTemp(Prepared), bPreparedFull);
		}
		// Every queued input owns a snapshot of a completed mutation. It can resolve
		// while the next Erase is partial; never inspect or snapshot that live source here.
		if (!StrokeCoverage->Advance(Visualization, DirtyBounds, bFull, bFlush))
		{
			if (bFlush && StrokeCoverage->HasPending()) { continue; }
			break;
		}
		RetireEditingCheckpoint();
		bVisualizationDirty = false;
		if (bEditPreviewDirty)
		{
			bEditPreviewDirty = !EditPreview->QueuePreparedRegion(*TargetLevel.Get(),
				UE::ComposableCamera::MeshEditor::PrepareEditPreview(Visualization, Settings->Layers), true);
		}
	}
}

void FComposableCameraMeshLayerEdMode::CancelDocumentBuild()
{
	DocumentBuild->Cancel(); OpeningRegionCoverage->Cancel();
	if (!OpeningRegionVisualization.Cells.IsEmpty() || OpeningRegionVisualization.LocalBounds.bIsValid)
	{
		UE::ComposableCamera::MeshEditor::AsyncMeshLayerEdit([Retired = MoveTemp(OpeningRegionVisualization)]() mutable { Retired.Reset(); });
		OpeningRegionVisualization.Reset();
	}
	OpeningEditedRegions.Reset(); bOpeningIndexInstalled = false;
}

void FComposableCameraMeshLayerEdMode::QueueOpeningRegion(const FBox2D& DirtyBounds)
{
	if (!DirtyBounds.bIsValid || !bOpeningIndexInstalled) { return; }
	const double RegionSize = OpeningCellSize * 32.0;
	const FIntPoint Min(FMath::FloorToInt(DirtyBounds.Min.X / RegionSize), FMath::FloorToInt(DirtyBounds.Min.Y / RegionSize));
	const FIntPoint Max(FMath::FloorToInt(DirtyBounds.Max.X / RegionSize), FMath::FloorToInt(DirtyBounds.Max.Y / RegionSize));
	for (int32 Y = Min.Y; Y <= Max.Y; ++Y) { for (int32 X = Min.X; X <= Max.X; ++X) { OpeningEditedRegions.Add(FIntPoint(X, Y)); } }
	// Resolve whole render regions, not just new Brush triangles: priority, neighbors and Erase holes stay exact.
	const FBox2D Bounds(FVector2D(Min.X, Min.Y) * RegionSize,
		FVector2D(Max.X + 1, Max.Y + 1) * RegionSize - FVector2D(OpeningCellSize * 1.e-6));
	OpeningRegionCoverage->Queue(Settings->WorkingData, Settings->Layers, *AuthoringIndex,
		Bounds, INDEX_NONE, OpeningCellSize, false);
}

void FComposableCameraMeshLayerEdMode::AdvanceOpeningRegions()
{
	if (!DocumentBuild->IsResident() || !bOpeningIndexInstalled || !Settings || !TargetLevel.IsValid() || GIsTransacting) { return; }
	using namespace UE::ComposableCamera::MeshEditor;
	FPreparedEditPreview Prepared; bool bFull = false;
	while (OpeningRegionCoverage->TakePrepared(Prepared, bFull))
	{
		if (bFull) { EditPreview->PublishPreparedDocument(*TargetLevel.Get(), AnchorTransform, Settings->Layers, MoveTemp(Prepared)); }
		else { EditPreview->PublishPreparedRegions(*TargetLevel.Get(), AnchorTransform, Settings->Layers, MoveTemp(Prepared)); }
	}
	FBox2D Changed(ForceInit);
	OpeningRegionCoverage->Advance(OpeningRegionVisualization, Changed, bFull);
}

void FComposableCameraMeshLayerEdMode::AdvanceDocumentPreview(const FVector2D& /*LocalFocus*/, bool bWait)
{
	using namespace UE::ComposableCamera::MeshEditor;
	if (!Settings || bExiting || GIsTransacting || !TargetLevel.IsValid() || !TargetLevel->GetWorld()
		|| TargetLevel->GetWorld()->IsBeingCleanedUp()) { return; }
	if (!DocumentBuild->IsResident() && (bPainting || StrokeTask || StrokeCoverage->HasPending())) { return; }
	if (bVisualizationDirty)
	{
		if (!DocumentBuild->HasPending())
		{
			if (!bHistoricalPreview) { EditPreview->CancelUpdate(); }
			CancelDocumentBuild();
			DocumentBuild->StartResident(Settings->WorkingData, Settings->Layers, Settings->DocumentRevision, nullptr, &Visualization, !bHistoricalPreview);
			return; // Scheduling never performs a simultaneous whole-scene installation on an input boundary.
		}
		if (DocumentBuild->IsResident())
		{
			if (bWait) { DocumentBuild->Wait(); }
			const FGuid BaseRevision = DocumentBuild->GetRevision();
			FDocumentPreviewIndex Index;
			if (DocumentBuild->TakeIndex(BaseRevision, Index))
			{
				*AuthoringIndex = MoveTemp(Index.Index); OpeningCellSize = Index.CellSize; bOpeningIndexInstalled = true;
				OpeningRegionVisualization.CellSize = OpeningCellSize;
				OpeningRegionVisualization.LocalBounds = AuthoringIndex->GetProjectedBounds(Settings->Layers);
			}
			AdvanceOpeningRegions();
			FPreparedEditPreview Document;
			while (DocumentBuild->TakeTile(BaseRevision, Document))
			{
				bEditPreviewDirty = !EditPreview->PublishPreparedDocument(*TargetLevel.Get(), AnchorTransform,
					Settings->Layers, MoveTemp(Document), &OpeningEditedRegions);
			}
			FDocumentPreviewResult Result;
			if (!DocumentBuild->Take(BaseRevision, Result)) { return; }
			const bool bIndexInstalled = bOpeningIndexInstalled;
			CancelDocumentBuild(); // Stops regional feedback before later primary deltas can supersede it.
			RetireEditingCheckpoint(); Visualization = MoveTemp(Result.Visualization);
			if (!bIndexInstalled) { *AuthoringIndex = MoveTemp(Result.Index); }
			else { AsyncMeshLayerEdit([Retired = MoveTemp(Result.Index)]() mutable { Retired.Reset(); }); }
			bVisualizationDirty = false;
			if (bHistoricalPreview) { bHistoricalPreview = false; bEditPreviewDirty = false; }
			if (!bPainting && !StrokeCoverage->HasPending() && !bEditPreviewDirty && !EditPreview->HasQueuedUpdates())
			{ PreviewRevision = Settings->DocumentRevision; RememberPreview(); }
			else { PreviewRevision.Invalidate(); }
			return;
		}
		// Retained only for callers explicitly using the old progressive build API.
		const double Started = FPlatformTime::Seconds();
		FPreparedEditPreview Tile;
		for (; FPlatformTime::Seconds() - Started < 0.004; )
		{
			if (!DocumentBuild->TakeTile(Settings->DocumentRevision, Tile)) { break; }
			bEditPreviewDirty = !EditPreview->QueuePreparedUpdate(*TargetLevel.Get(), MoveTemp(Tile), false);
		}
		FDocumentPreviewResult Result;
		if (!DocumentBuild->Take(Settings->DocumentRevision, Result)) { return; }
		RetireEditingCheckpoint(); Visualization = MoveTemp(Result.Visualization); *AuthoringIndex = MoveTemp(Result.Index);
		bVisualizationDirty = false;
		if (bHistoricalPreview)
		{
			// Exact historical buffers are already being restored. A tiny startup
			// region must not overwrite a complete tile, nor trigger a second full upload.
			bHistoricalPreview = false; bEditPreviewDirty = false;
			if (!EditPreview->HasQueuedUpdates()) { PreviewRevision = Settings->DocumentRevision; RememberPreview(); }
			return;
		}
		FPreparedEditPreview Complete; Complete.CellSize = Visualization.CellSize;
		bEditPreviewDirty = !EditPreview->QueuePreparedUpdate(*TargetLevel.Get(), MoveTemp(Complete));
	}
	else if (bEditPreviewDirty || (!EditPreview->IsReadyFor(TargetLevel.Get()) && !EditPreview->HasQueuedUpdates()))
	{
		// Shape completion already supplied exact coverage. Reuse it and assemble
		// tiles asynchronously; no full synchronous upload from Render remains.
		bEditPreviewDirty = !EditPreview->QueueUpdate(*TargetLevel.Get(), GetVisualization());
		EditPreview->LaunchQueuedTileWork(Settings->Layers);
	}
}

void FComposableCameraMeshLayerEdMode::PrepareUndo()
{
	RememberPreview();
	// Retain every accepted source stamp and close its transaction. Undo will
	// replace this document; coverage, assembly and uploads have no value here.
	CancelPendingShapes(); CancelDocumentBuild(); StrokeCoverage->Cancel(); EditPreview->CancelUpdate();
	bVisualizationDirty = bEditPreviewDirty = true;
	PreviewRevision.Invalidate();
	AdvancePainting(MAX_int32, 0.0, false);
	FinishStroke(false, true);
	CancelInteraction();
	RefreshDocumentState();
}

void FComposableCameraMeshLayerEdMode::RememberPreview()
{
	// Temporary fill and intermediate slider values must never acquire history identities.
	// Property transactions retain their source bookends; capture once after completion.
	if (bLiveShapeVisible || GIsTransacting || (GEditor && GEditor->IsTransactionActive())) { return; }
	// Transaction restoration can change Settings before PostUndo is called.
	// Never label an older or partially published display with the new revision.
	if (Settings && PreviewRevision == Settings->DocumentRevision && !bVisualizationDirty && !bEditPreviewDirty
		&& !bPainting && !StrokeTask && !DocumentBuild->HasPending() && !StrokeCoverage->HasPending())
	{
		// Keep the editable base and the displayed revision together. Move polygons
		// into an immutable checkpoint; no full cache copy on the editor thread.
		if ((!EditingCheckpoint || !Visualization.Cells.IsEmpty() || Visualization.LocalBounds.bIsValid)
			&& !EditPreview->HasQueuedUpdates() && EditPreview->IsReadyFor(TargetLevel.Get()))
		{
			using namespace UE::ComposableCamera::MeshEditor;
			if (!AuthoringIndex->IsCurrent(Settings->WorkingData)) { AuthoringIndex->Build(Settings->WorkingData); }
			auto Checkpoint = MakeShared<FEditPreviewCheckpoint, ESPMode::ThreadSafe>();
			Checkpoint->Revision = PreviewRevision; Checkpoint->Visualization = MoveTemp(Visualization); Visualization.Reset();
			Checkpoint->Index = *AuthoringIndex;
			Checkpoint->AllocatedBytes = sizeof(FEditPreviewCheckpoint) + Checkpoint->Index.GetAllocatedSize()
				+ Checkpoint->Visualization.Cells.GetAllocatedSize() + Checkpoint->Visualization.CellsByGrid.GetAllocatedSize();
			for (const auto& Cell : Checkpoint->Visualization.Cells)
			{
				Checkpoint->AllocatedBytes += Cell.Patches.GetAllocatedSize();
				for (const auto& Patch : Cell.Patches) { Checkpoint->AllocatedBytes += Patch.LocalVertices.GetAllocatedSize(); }
			}
			for (const auto& Pair : Checkpoint->Visualization.CellsByGrid) { Checkpoint->AllocatedBytes += Pair.Value.GetAllocatedSize(); }
			RetireEditingCheckpoint(); EditingCheckpoint = MoveTemp(Checkpoint);
		}
		EditPreview->RememberRevision(PreviewRevision, SavedRevision, EditingCheckpoint);
	}
}

const UE::ComposableCamera::MeshEditor::FResolvedSurfaceVisualization& FComposableCameraMeshLayerEdMode::GetVisualization() const
{
	// A freshly completed mutable cache wins until RememberPreview moves it.
	return EditingCheckpoint && Visualization.Cells.IsEmpty() && !Visualization.LocalBounds.bIsValid
		? EditingCheckpoint->Visualization : Visualization;
}

void FComposableCameraMeshLayerEdMode::RetireEditingCheckpoint()
{
	if (EditingCheckpoint)
	{
		UE::ComposableCamera::MeshEditor::AsyncMeshLayerEdit( [Retired = MoveTemp(EditingCheckpoint)]() mutable { Retired.Reset(); });
	}
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
	CancelDocumentBuild();
	if (!AuthoringIndex->IsCurrent(Settings->WorkingData)) { AuthoringIndex->Build(Settings->WorkingData); }
	const bool bTemporaryErase = ViewportClient && ViewportClient->IsShiftPressed();
	const bool bErase = Settings->Tool == EComposableCameraMeshDrawTool::Erase || bTemporaryErase;
	if (bErase && bPainting) { CaptureStrokeSourceBeforeErase(); }
	const int32 FirstAddedVertex = Settings->WorkingData.Vertices.Num();
	const int32 FirstAddedTriangle = Settings->WorkingData.TriangleLayerIds.Num();
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
			RetireEditingCheckpoint();
			UE::ComposableCamera::MeshEditor::BuildAuthoringVisualization(Settings->WorkingData, Settings->Layers, Visualization);
			bVisualizationDirty = false;
			bEditPreviewDirty = true;
			UpdateCachedPreview();
		}
		else
		{
			// Legacy synchronous/test path. Interactive input uses queued worker COW.
			if (EditingCheckpoint && Visualization.Cells.IsEmpty() && !Visualization.LocalBounds.bIsValid)
			{ Visualization = EditingCheckpoint->Visualization; }
			RetireEditingCheckpoint();
			if (bErase)
			{
				UE::ComposableCamera::MeshEditor::UpdateAuthoringVisualization(Settings->WorkingData, Settings->Layers, DirtyBounds,
					Visualization, nullptr, AuthoringIndex.Get());
			}
			else
			{
				UE::ComposableCamera::MeshEditor::AppendAuthoringVisualization(Settings->WorkingData, Settings->Layers, FirstAddedTriangle,
					DirtyBounds, Visualization, nullptr, AuthoringIndex.Get());
			}
			UpdateCachedPreview(bEditPreviewDirty ? nullptr : &DirtyBounds);
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
	TRACE_CPUPROFILER_EVENT_SCOPE(CCS_MeshLayers_BrushProjection);
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
	const FVector Up = AnchorTransform.TransformVectorNoScale(FVector::UpVector).GetSafeNormal();
	TArray<FVector2D> Outline;
	Outline.Reserve(SegmentCount);
	for (int32 Segment = 0; Segment < SegmentCount; ++Segment)
	{
		const double Angle = 2.0 * UE_DOUBLE_PI * Segment / SegmentCount;
		Outline.Add(FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Settings->BrushRadius);
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CCSMeshLayerProjection), true);
	QueryParams.bTraceComplex = true;
	if (AComposableCameraMeshSurfaceStorageActor* Actor = StorageActor.Get())
	{
		QueryParams.AddIgnoredActor(Actor);
	}

	// Reuse the bounded interior/curvature sampling used by Draw. Radius and
	// segment count still define the same tangent-plane footprint; document-up
	// traces keep neighboring samples' XY positions fixed on uneven ground.
	FComposableCameraMeshSurfaceAuthoringData Stamp;
	using namespace UE::ComposableCamera::MeshEditor;
	const EShapeBuildResult Result = BuildProjectedShape(Outline,
		FMath::Clamp(Settings->BrushRadius * 0.5, 10.0, 100.0), LayerId,
		[this, World, LayerId, &CenterHit, TangentX, TangentY, Up, &QueryParams](const FVector2D& Point, FVector& Position)
	{
		const FVector Desired = CenterHit.ImpactPoint
			+ TangentX * Point.X + TangentY * Point.Y;
		FHitResult ProjectionHit;
		if (!TraceLayerSurface(*World, LayerId, Desired + Up * Settings->ProjectionDistance,
			Desired - Up * Settings->ProjectionDistance, QueryParams, ProjectionHit)
			|| FVector::DotProduct(ProjectionHit.ImpactNormal, Up) < Settings->MinimumFloorNormalZ) { return false; }
		Position = ProjectionHit.ImpactPoint - CenterHit.ImpactPoint; // Relative world cm; retain precision far from the origin.
		return true;
	}, Stamp, 4096);
	if (Result != EShapeBuildResult::Success && Result != EShapeBuildResult::PartialSurface)
	{
		if (Result == EShapeBuildResult::TooComplex)
		{
			ShapeFeedback = LOCTEXT("BrushTooDense", "Surface is too complex for this stamp. Reduce Brush Radius.");
		}
		return false;
	}
	ShapeFeedback = FText::GetEmpty();
	const int32 Base = Settings->WorkingData.Vertices.Num();
	const int32 FirstTriangle = Settings->WorkingData.TriangleLayerIds.Num();
	Settings->WorkingData.TriangleShapeIds.SetNum(Settings->WorkingData.TriangleLayerIds.Num());
	for (const FVector3f& Vertex : Stamp.Vertices)
	{
		Settings->WorkingData.Vertices.Add(FVector3f(AnchorTransform.InverseTransformPosition(CenterHit.ImpactPoint + FVector(Vertex))));
	}
	for (int32 Index : Stamp.Indices) { Settings->WorkingData.Indices.Add(Base + Index); }
	Settings->WorkingData.TriangleLayerIds.Append(Stamp.TriangleLayerIds);
	Settings->WorkingData.TriangleShapeIds.SetNum(Settings->WorkingData.TriangleLayerIds.Num());
	AuthoringIndex->Append(Settings->WorkingData, FirstTriangle);
	return true;
}

bool FComposableCameraMeshLayerEdMode::EraseBrushStamp(const FHitResult& CenterHit, const FGuid& LayerId, FBox2D* OutDirtyBounds)
{
	if (!Settings || !LayerId.IsValid()) { return false; }
	if (bPainting) { CaptureStrokeSourceBeforeErase(); }
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
	return UE::ComposableCamera::MeshEditor::EraseShapeGeometry(Settings->WorkingData, LayerId, Stamp, true, nullptr, OutDirtyBounds, AuthoringIndex.Get());
}
void FComposableCameraMeshLayerEdMode::RemoveOrphanedTriangles()
{
	if (!Settings || !Settings->WorkingData.IsConsistent())
	{
		return;
	}
	if (DocumentBuild->HasPending())
	{
		// Save compaction rewrites index counts without changing DocumentRevision.
		// Its old snapshot must not install an index or finish a partial tile stream.
		CancelDocumentBuild();
		if (!bHistoricalPreview) { EditPreview->CancelUpdate(); }
		bVisualizationDirty = true; bEditPreviewDirty = !bHistoricalPreview;
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
	CleanData.TriangleShapeIds.Reserve(Settings->WorkingData.TriangleLayerIds.Num());
	TArray<int32> VertexRemap; VertexRemap.Init(INDEX_NONE, Settings->WorkingData.Vertices.Num());
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

		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const int32 SourceVertex = Settings->WorkingData.Indices[Offset + Corner];
			int32& TargetVertex = VertexRemap[SourceVertex];
			if (TargetVertex == INDEX_NONE) { TargetVertex = CleanData.Vertices.Add(Settings->WorkingData.Vertices[SourceVertex]); }
			CleanData.Indices.Add(TargetVertex);
		}
		CleanData.TriangleLayerIds.Add(Settings->WorkingData.TriangleLayerIds[TriangleIndex]);
		CleanData.TriangleShapeIds.Add(Settings->WorkingData.TriangleShapeIds.IsValidIndex(TriangleIndex)
			? Settings->WorkingData.TriangleShapeIds[TriangleIndex] : FGuid());
	}
	const auto& Current = Settings->WorkingData;
	const bool bGeometryChanged = Current.Vertices != CleanData.Vertices || Current.Indices != CleanData.Indices
		|| Current.TriangleLayerIds != CleanData.TriangleLayerIds;
	if (bGeometryChanged || Current.TriangleShapeIds != CleanData.TriangleShapeIds || Current.Shapes.Num() != CleanData.Shapes.Num())
	{
		UE::ComposableCamera::MeshEditor::AsyncMeshLayerEdit([Retired = MoveTemp(Settings->WorkingData)]() mutable { Retired.Reset(); });
		Settings->WorkingData = MoveTemp(CleanData);
	}
	else { UE::ComposableCamera::MeshEditor::AsyncMeshLayerEdit([Retired = MoveTemp(CleanData)]() mutable { Retired.Reset(); }); }
	if (bGeometryChanged)
	{
		UE::ComposableCamera::MeshEditor::AsyncMeshLayerEdit([Retired = MoveTemp(*AuthoringIndex)]() mutable { Retired.Reset(); });
		AuthoringIndex->Reset();
	}
}

void FComposableCameraMeshLayerEdMode::MarkLayerDataDirty()
{
	CancelInteraction();
	RemoveOrphanedTriangles();
	RefreshDocumentState();
}

void FComposableCameraMeshLayerEdMode::RefreshDocumentState()
{
	CancelDocumentBuild(); EditPreview->CancelUpdate();
	StrokeCoverage->Cancel();
	AuthoringIndex->Reset();
	bDirty = Settings && Settings->DocumentRevision != SavedRevision;
	bVisualizationDirty = true;
	bEditPreviewDirty = true;
	bHistoricalPreview = Settings && TargetLevel.IsValid() && EditPreview->QueueRestoreRevision(*TargetLevel.Get(), Settings->DocumentRevision);
	RetireEditingCheckpoint();
	if (bHistoricalPreview)
	{
		bEditPreviewDirty = false;
		EditingCheckpoint = EditPreview->FindCheckpoint(Settings->DocumentRevision);
		if (EditingCheckpoint)
		{
			// Discard/Undo may leave another revision's mutable polygons in the mode.
			// Retire them instead of choosing them over the restored immutable base.
			if (!Visualization.Cells.IsEmpty() || Visualization.LocalBounds.bIsValid)
			{
				UE::ComposableCamera::MeshEditor::AsyncMeshLayerEdit( [Retired = MoveTemp(Visualization)]() mutable { Retired.Reset(); });
				Visualization.Reset();
			}
			*AuthoringIndex = EditingCheckpoint->Index; bVisualizationDirty = false;
		}
	}
	RebuildShapeOverlays();
	if (GEditor) { GEditor->RedrawLevelEditingViewports(); }
}

void FComposableCameraMeshLayerEdMode::PostUndo(bool bSuccess)
{
	if (!bSuccess || !Settings || bExiting) { return; }
	// Settings has already changed. Invalidate display provenance before any
	// interaction boundary can try to remember/relabel the still-older mesh.
	bVisualizationDirty = bEditPreviewDirty = true; PreviewRevision.Invalidate();
	// Undo has already restored the authoritative UObject. Never apply queued work
	// or restore a pre-stroke checkpoint over that restored document.
	StrokeTask.Reset(); QueuedStamps.Reset(); DeferredStrokes.Reset(); QueuedStampIndex = 0; StrokeCoverage->Cancel(); EditPreview->CancelUpdate();
	if (StrokeTransaction) { StrokeTransaction->Cancel(); StrokeTransaction.Reset(); }
	StrokeStartData.Reset(); bStrokeReleased = false;
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
	if (bPainting && bStrokeReleased) { DeferredStrokes.AddDefaulted(); return; }
	if (!Settings || bPainting || IsCreatingShapes()) { return; }
	RememberPreview();
	if (bVisualizationDirty && !DocumentBuild->HasPending()) { AdvanceDocumentPreview(); }
	if (!DocumentBuild->IsResident())
	{
		if (DocumentBuild->HasPending() || (EditPreview->IsRestoringRevision() && bVisualizationDirty))
		{ EditPreview->CancelUpdate(); bEditPreviewDirty = true; }
		CancelDocumentBuild();
	}
	if (!bVisualizationDirty && EditingCheckpoint && Visualization.Cells.IsEmpty() && !Visualization.LocalBounds.bIsValid
		&& !StrokeCoverage->HasPending()) { StrokeCoverage->UseCheckpoint(EditingCheckpoint); }
	bHistoricalPreview = false;
	QueuedStamps.Reset(); QueuedStampIndex = 0; StrokeTask.Reset(); bStrokeReleased = false;
	StrokeStartData.Reset(); bStrokeSourceSnapshot = false;
	StrokeStartVertices = Settings->WorkingData.Vertices.Num(); StrokeStartIndices = Settings->WorkingData.Indices.Num();
	StrokeStartShapeIds = Settings->WorkingData.TriangleShapeIds.Num();
	StrokeStartRevision = Settings->DocumentRevision;
	StrokeTransaction = MakeUnique<FScopedTransaction>(LOCTEXT("BrushStrokeTransaction", "Paint / Erase Mesh Camera Layer"));
	Settings->Modify();
	bPainting = true;
	bHasLastPaintPosition = false;
}

void FComposableCameraMeshLayerEdMode::StartDeferredStroke()
{
	if (bPainting || DeferredStrokes.IsEmpty()) { return; }
	auto Next = MoveTemp(DeferredStrokes[0]); DeferredStrokes.RemoveAt(0, 1, EAllowShrinking::No);
	BeginStroke();
	QueuedStamps = MoveTemp(Next.Stamps); bStrokeReleased = Next.bReleased;
	LastPaintWorldPosition = Next.LastPosition; bHasLastPaintPosition = Next.bHasLastPosition;
}

void FComposableCameraMeshLayerEdMode::CaptureStrokeSourceBeforeErase()
{
	if (!Settings || bStrokeSourceSnapshot) { return; }
	TRACE_CPUPROFILER_EVENT_SCOPE(CCS_MeshLayers_EraseSourceSnapshot);
	StrokeStartData = Settings->WorkingData;
	StrokeStartData.Vertices.SetNum(StrokeStartVertices, EAllowShrinking::No);
	StrokeStartData.Indices.SetNum(StrokeStartIndices, EAllowShrinking::No);
	StrokeStartData.TriangleLayerIds.SetNum(StrokeStartIndices / 3, EAllowShrinking::No);
	StrokeStartData.TriangleShapeIds.SetNum(StrokeStartShapeIds, EAllowShrinking::No);
	bStrokeSourceSnapshot = true;
}

void FComposableCameraMeshLayerEdMode::FinishStroke(bool bRevert, bool bKeepPreview)
{
	// Explicit document/tool boundaries retain Landscape's EndTool flush semantics.
	// Normal mouse release instead lets Tick finish every accepted stamp, without a long release hitch.
	bStrokeReleased = false;
	const bool bDocumentPending = DocumentBuild->HasPending() || bHistoricalPreview || EditPreview->IsRestoringRevision();
	if (!bRevert && (StrokeTask || QueuedStampIndex < QueuedStamps.Num()
		|| (!bKeepPreview && (StrokeCoverage->HasPending() || (!bDocumentPending && EditPreview->HasQueuedUpdates()))))) { AdvancePainting(MAX_int32, 0.0); }
	StrokeTask.Reset(); QueuedStamps.Reset(); QueuedStampIndex = 0;
	if (bRevert)
	{
		DeferredStrokes.Reset();
		CancelDocumentBuild(); StrokeCoverage->Cancel(); bVisualizationDirty = true; bEditPreviewDirty = true;
	}
	if (bRevert || (!bKeepPreview && !bDocumentPending)) { EditPreview->CancelUpdate(); }
	const bool bChanged = StrokeTransaction && Settings && Settings->DocumentRevision != StrokeStartRevision;
	if (StrokeTransaction)
	{
		if (bRevert && Settings)
		{
			if (bStrokeSourceSnapshot) { Settings->WorkingData = MoveTemp(StrokeStartData); }
			else
			{
				Settings->WorkingData.Vertices.SetNum(StrokeStartVertices, EAllowShrinking::No);
				Settings->WorkingData.Indices.SetNum(StrokeStartIndices, EAllowShrinking::No);
				Settings->WorkingData.TriangleLayerIds.SetNum(StrokeStartIndices / 3, EAllowShrinking::No);
				Settings->WorkingData.TriangleShapeIds.SetNum(StrokeStartShapeIds, EAllowShrinking::No);
			}
			Settings->DocumentRevision = StrokeStartRevision;
			RefreshDocumentState();
		}
		if (!Settings || bRevert || Settings->DocumentRevision == StrokeStartRevision) { StrokeTransaction->Cancel(); }
		StrokeTransaction.Reset();
		StrokeStartData.Reset();
	}
	bPainting = false;
	bHasLastPaintPosition = false;
	if (bChanged && !bRevert && Settings && !bVisualizationDirty && !bEditPreviewDirty && !DocumentBuild->HasPending()
		&& !StrokeCoverage->HasPending() && !EditPreview->HasQueuedUpdates() && EditPreview->IsReadyFor(TargetLevel.Get()))
	{
		// Explicit focus/save boundaries can finish publication while bPainting was
		// still true. Label the completed display only after closing that stroke.
		PreviewRevision = Settings->DocumentRevision; RememberPreview();
	}
	if (bChanged && !bRevert)
	{
		// Source is complete. Derived coverage/fill can keep progressing without
		// retaining this transaction or stalling the next press.
		bDirty = Settings->DocumentRevision != SavedRevision;
		if (GEditor) { GEditor->RedrawLevelEditingViewports(); }
	}
	if (!bRevert && bKeepPreview) { StartDeferredStroke(); }
	else if (!bRevert)
	{
		while (!DeferredStrokes.IsEmpty()) { StartDeferredStroke(); FinishStroke(false, false); }
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
	for (int32 Index = PendingShapes.Num() - 1; Index >= 0; --Index)
	{ if (PendingShapes[Index]->Shape.ShapeId == SelectedShapeId) { RetirePendingShape(Index); } }
	FBox2D DirtyBounds(ForceInit);
	for (int32 Triangle = 0; Triangle < FMath::Min(Settings->WorkingData.TriangleShapeIds.Num(), Settings->WorkingData.Indices.Num() / 3); ++Triangle)
	{
		if (Settings->WorkingData.TriangleShapeIds[Triangle] != SelectedShapeId) { continue; }
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const int32 Vertex = Settings->WorkingData.Indices[Triangle * 3 + Corner];
			if (Settings->WorkingData.Vertices.IsValidIndex(Vertex))
			{ const auto& Point = Settings->WorkingData.Vertices[Vertex]; DirtyBounds += FVector2D(Point.X, Point.Y); }
		}
	}
	const FScopedTransaction Transaction(LOCTEXT("DeleteShapeTransaction", "Delete Mesh Camera Shape"));
	Settings->Modify();
	UE::ComposableCamera::MeshEditor::RemoveShapeGeometry(Settings->WorkingData, SelectedShapeId);
	Settings->WorkingData.Shapes.RemoveAll([this](const auto& Shape) { return Shape.ShapeId == SelectedShapeId; });
	SelectedShapeId.Invalidate();
	Settings->TouchDocument();
	if (TargetLevel.IsValid() && DirtyBounds.bIsValid && Settings->WorkingData.IsConsistent())
	{
		CancelDocumentBuild(); bHistoricalPreview = false; PreviewRevision.Invalidate();
		AuthoringIndex->Build(Settings->WorkingData);
		if (!StrokeCoverage->HasPending()) { StrokeCoverage->UseCheckpoint(EditingCheckpoint); }
		StrokeCoverage->Queue(Settings->WorkingData, Settings->Layers, *AuthoringIndex, DirtyBounds, INDEX_NONE,
			GetVisualization().CellSize, bVisualizationDirty);
		bDirty = Settings->DocumentRevision != SavedRevision; RebuildShapeOverlays();
	}
	else { RefreshDocumentState(); }
	RefreshSelectionEditor();
	if (GEditor) { GEditor->RedrawLevelEditingViewports(false); }
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
	// Repeated Details notifications belong to the same drag. Retain its immutable base.
	if (!LiveShapeBase && !bLiveShapeRequested) { ResetInteraction(); }
	if (Settings) { Settings->Modify(); }
}

void FComposableCameraMeshLayerEdMode::ApplySelectedLayer()
{
	if (!Settings || !SelectionEditor || !Settings->Layers.IsValidIndex(Settings->ActiveLayerIndex)) { return; }
	const auto Previous = Settings->Layers[Settings->ActiveLayerIndex];
	const FGuid LayerId = Settings->GetActiveLayerId();
	Settings->Layers[Settings->ActiveLayerIndex] = SelectionEditor->Layer;
	Settings->Layers[Settings->ActiveLayerIndex].LayerId = LayerId;
	const auto& Current = Settings->Layers[Settings->ActiveLayerIndex];
	if (!FComposableCameraMeshLayerDefinition::StaticStruct()->CompareScriptStruct(&Previous, &Current, 0))
	{
		if (Previous.bEnabled != Current.bEnabled) { Settings->NotifyLayerDataChanged(); }
		else
		{
			// Name/Profile/Channel are prospective metadata. Color changes only the render parameter.
			Settings->TouchDocument(); bDirty = Settings->DocumentRevision != SavedRevision;
			EditPreview->UpdateLayerAppearance(Settings->Layers);
			DocumentBuild->RetagRevision(Settings->DocumentRevision);
			for (auto& Task : PendingShapes) { Task->BaseRevision = Settings->DocumentRevision; }
			if (Previous.TraceChannel != Current.TraceChannel) { CancelPendingShapes(); }
			if (!bVisualizationDirty && !bEditPreviewDirty && !StrokeCoverage->HasPending())
			{ PreviewRevision = Settings->DocumentRevision; RememberPreview(); }
			if (GEditor) { GEditor->RedrawLevelEditingViewports(false); }
		}
	}
	// Keep the active color picker/slider and its single property transaction alive.
	if (Toolkit.IsValid() && !SelectionEditor->bInteractiveChange)
	{ StaticCastSharedPtr<FComposableCameraMeshLayerModeToolkit>(Toolkit)->RefreshDocument(); }
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
	// Interactive changes stay native; completion commits source in the shape task's transaction.
	if (SelectionEditor->bInteractiveChange) { RequestLiveShapePreview(MoveTemp(Shape)); }
	else { CancelLiveShapePreview(); QueueShapeCreation(MoveTemp(Shape), GetWorld()); }
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
	RequestLiveShapePreview(EditShape);
}

void FComposableCameraMeshLayerEdMode::RequestLiveShapePreview(FComposableCameraMeshAuthoredShape Shape)
{
	using namespace UE::ComposableCamera::MeshEditor;
	if (!Settings || Shape.LayerId != Settings->GetActiveLayerId()) { return; }
	if (FComposableCameraMeshAuthoredShape::StaticStruct()->CompareScriptStruct(&LiveShape, &Shape, 0)) { return; }
	LiveShape = MoveTemp(Shape); bLiveShapeRequested = true;
	// Immediate bounded fill follows the latest input, independently of exact projection progress.
	TArray<FVector2D> Outline; BuildShapeOutline(LiveShape, Outline);
	FProjectedShapeBuild Planar;
	LiveDraftFill.LocalVertices.Reset(); LiveDraftFill.Indices.Reset();
	if (Planar.Begin(Outline, HALF_WORLD_MAX, LiveShape.LayerId, 512) == EShapeBuildResult::Success)
	{
		for (const auto& Point : Planar.GetOutline()) { LiveDraftFill.LocalVertices.Add(ShapePlanePosition(LiveShape, Point)); }
		const auto Indices = Planar.GetOutlineIndices(); LiveDraftFill.Indices.Append(Indices.GetData(), Indices.Num());
		LiveDraftFill.LayerIndex = Settings->ActiveLayerIndex;
		FLinearColor Color = Settings->Layers[Settings->ActiveLayerIndex].DebugColor; Color.A = 0.2f;
		LiveDraftFill.Color = Color.ToFColor(true);
	}
	if (GEditor) { GEditor->RedrawLevelEditingViewports(false); }
}

void FComposableCameraMeshLayerEdMode::CancelLiveShapePreview()
{
	bLiveShapeRequested = false; LiveDraftFill.LocalVertices.Reset(); LiveDraftFill.Indices.Reset();
	LiveShape = FComposableCameraMeshAuthoredShape();
	if (LiveShapeTask) { LiveShapeTask->Alive->store(false, std::memory_order_relaxed); }
	if (LiveShapeTask || LiveShapeBase)
	{
		UE::ComposableCamera::MeshEditor::AsyncMeshLayerEdit([Task = MoveTemp(LiveShapeTask), Base = MoveTemp(LiveShapeBase)]() mutable
		{ Task.Reset(); Base.Reset(); });
	}
	if (bLiveShapeVisible && Settings && TargetLevel.IsValid())
	{
		if (!EditPreview->QueueRestoreRevision(*TargetLevel.Get(), Settings->DocumentRevision))
		{ CancelDocumentBuild(); bVisualizationDirty = bEditPreviewDirty = true; }
		else { bEditPreviewDirty = false; }
	}
	bLiveShapeVisible = false;
	LivePublishedBounds = FBox2D(ForceInit);
}

void FComposableCameraMeshLayerEdMode::AdvanceLiveShapePreview()
{
	using namespace UE::ComposableCamera::MeshEditor;
	if (!Settings || !TargetLevel.IsValid() || !GetWorld() || GetWorld()->IsBeingCleanedUp() || GIsTransacting) { return; }
	if (LiveShapeBase && LiveShapeBase->Revision != Settings->DocumentRevision) { CancelLiveShapePreview(); return; }
	if (!LiveShapeTask)
	{
		if (!bLiveShapeRequested) { return; }
		// Loading/restoration must finish its document publication before temporary tiles replace it.
		// Immediate planar feedback stays visible while the authoritative base becomes ready.
		if (!LiveShapeBase && (bVisualizationDirty || DocumentBuild->HasPending()
			|| StrokeCoverage->HasPending() || EditPreview->HasQueuedUpdates())) { return; }
		RememberPreview();
		if (!LiveShapeBase)
		{
			auto Base = MakeShared<FComposableCameraMeshLiveShapeBase, ESPMode::ThreadSafe>();
			Base->Revision = Settings->DocumentRevision; Base->Checkpoint = EditingCheckpoint;
			// One immutable native snapshot per drag, rather than one whole document per mouse move.
			Base->Source.Vertices = Settings->WorkingData.Vertices; Base->Source.Indices = Settings->WorkingData.Indices;
			Base->Source.TriangleLayerIds = Settings->WorkingData.TriangleLayerIds;
			Base->Source.TriangleShapeIds = Settings->WorkingData.TriangleShapeIds; Base->Source.Shapes = Settings->WorkingData.Shapes;
			LiveShapeBase = MoveTemp(Base);
		}
		LiveShapeTask = MakeUnique<FComposableCameraMeshLiveShapeTask>(); LiveShapeTask->Shape = LiveShape;
		TArray<FVector2D> Outline; BuildShapeOutline(LiveShapeTask->Shape, Outline);
		if (LiveShapeTask->Projection.Begin(Outline, LiveShapeTask->Shape.SampleSpacing, LiveShapeTask->Shape.LayerId,
			4096, 1.0 / FMath::Max(AnchorTransform.GetScale3D().GetAbsMax(), UE_DOUBLE_SMALL_NUMBER)) != EShapeBuildResult::Success)
		{ LiveShapeTask.Reset(); bLiveShapeRequested = false; return; }
		bLiveShapeRequested = false;
	}
	auto& Task = *LiveShapeTask;
	if (Task.Future.IsValid())
	{
		if (!Task.Future.IsReady()) { return; }
		auto Prepared = Task.Future.Consume();
		const bool bFull = !LiveShapeBase->Checkpoint;
		const bool bPublished = EditPreview->QueuePreparedRegion(*TargetLevel.Get(), MoveTemp(Prepared), bFull);
		bLiveShapeVisible |= bPublished;
		if (bPublished)
		{
			EditPreview->AdvanceQueuedUpdates(*TargetLevel.Get(), AnchorTransform, Settings->Layers, 0.004);
			LivePublishedBounds = FBox2D(ForceInit);
			TArray<FVector2D> PublishedOutline; BuildShapeOutline(Task.Shape, PublishedOutline);
			for (const auto& Point : PublishedOutline) { LivePublishedBounds += Point; }
		}
		LiveShapeTask.Reset();
		if (!bLiveShapeRequested) { LiveDraftFill.LocalVertices.Reset(); LiveDraftFill.Indices.Reset(); }
		return;
	}
	const FVector Up = AnchorTransform.TransformVectorNoScale(FVector::UpVector).GetSafeNormal();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CCSMeshLayerLiveShape), true);
	if (const auto* Storage = StorageActor.Get()) { Params.AddIgnoredActor(Storage); }
	UWorld* World = GetWorld();
	if (!Task.Projection.Advance([&](const FVector2D& Point, FVector& Local)
	{
		const FVector Desired = AnchorTransform.TransformPosition(ShapePlanePosition(Task.Shape, Point)); FHitResult Hit;
		if (!TraceLayerSurface(*World, Task.Shape.LayerId, Desired + Up * Task.Shape.ProjectionDistance,
			Desired - Up * Task.Shape.ProjectionDistance, Params, Hit)
			|| FVector::DotProduct(Hit.ImpactNormal, Up) < Task.Shape.MinimumFloorNormalZ) { return false; }
		Local = AnchorTransform.InverseTransformPosition(Hit.ImpactPoint); return true;
	}, 256, 0.003)) { return; }
	if (Task.Projection.GetResult() != EShapeBuildResult::Success && Task.Projection.GetResult() != EShapeBuildResult::PartialSurface)
	{ LiveShapeTask.Reset(); return; }
	TArray<FComposableCameraMeshLayerDefinition> Layers; Layers.SetNum(Settings->Layers.Num());
	for (int32 Index = 0; Index < Layers.Num(); ++Index)
	{ Layers[Index].LayerId = Settings->Layers[Index].LayerId; Layers[Index].bEnabled = Settings->Layers[Index].bEnabled; }
	Task.Future = AsyncMeshLayerEdit([Base = LiveShapeBase, Shape = Task.Shape, Geometry = Task.Projection.TakeData(),
		Layers = MoveTemp(Layers), Alive = Task.Alive, PreviousBounds = LivePublishedBounds]() mutable
	{
		TArray<FBox2D, TInlineAllocator<3>> Regions;
		FBox2D Bounds(ForceInit); TArray<FVector2D> Outline; BuildShapeOutline(Shape, Outline);
		for (const auto& Point : Outline) { Bounds += Point; }
		if (Bounds.bIsValid) { Regions.Add(Bounds); }
		if (PreviousBounds.bIsValid) { Regions.Add(PreviousBounds); Bounds += PreviousBounds; }
		if (const auto* Previous = Base->Source.Shapes.FindByPredicate([&](const auto& Item) { return Item.ShapeId == Shape.ShapeId; }))
		{
			FBox2D OldBounds(ForceInit); BuildShapeOutline(*Previous, Outline);
			for (const auto& Point : Outline) { OldBounds += Point; }
			if (OldBounds.bIsValid) { Regions.Add(OldBounds); Bounds += OldBounds; }
		}
		for (const auto& Erasure : Shape.Erasures) { EraseShapeGeometry(Geometry, Shape.LayerId, Erasure, false); }
		if (!Alive->load(std::memory_order_relaxed)) { return FPreparedEditPreview(); }
		FResolvedSurfaceVisualization Cache;
		FComposableCameraMeshSurfaceAuthoringData Candidates;
		if (Base->Checkpoint)
		{
			const auto& Original = Base->Checkpoint->Visualization;
			Cache.CellSize = Original.CellSize; Cache.LocalBounds = Original.LocalBounds;
			const double TileSize = Cache.CellSize * 32.0;
			TSet<FIntPoint> Tiles;
			for (const auto& Region : Regions)
			{
				const FIntPoint Min(FMath::FloorToInt(Region.Min.X / TileSize), FMath::FloorToInt(Region.Min.Y / TileSize));
				const FIntPoint Max(FMath::FloorToInt(Region.Max.X / TileSize), FMath::FloorToInt(Region.Max.Y / TileSize));
				for (int32 Y = Min.Y; Y <= Max.Y; ++Y) { for (int32 X = Min.X; X <= Max.X; ++X) { Tiles.Add(FIntPoint(X, Y)); } }
			}
			for (const auto& Tile : Tiles) { for (int32 Cell = 0; Cell < 32 * 32; ++Cell)
			{
				if (!Alive->load(std::memory_order_relaxed)) { return FPreparedEditPreview(); }
				const FIntPoint Grid(Tile.X * 32 + Cell % 32, Tile.Y * 32 + Cell / 32);
				if (const auto* Indices = Original.CellsByGrid.Find(Grid))
				{ for (int32 Index : *Indices) { const int32 Added = Cache.Cells.Add(Original.Cells[Index]); Cache.CellsByGrid.FindOrAdd(Grid).Add(Added); } }
			} }
			FMeshLayerAuthoringIndex RebuiltIndex;
			const FMeshLayerAuthoringIndex* Index = &Base->Checkpoint->Index;
			if (!Index->IsCurrent(Base->Source)) { RebuiltIndex.Build(Base->Source); Index = &RebuiltIndex; }
			TSet<int32> UniqueTriangles; TArray<int32> Triangles;
			for (const auto& Region : Regions)
			{
				// Resolver replaces whole dirty cells, so every competing neighbor in those cells is required.
				const FBox2D WholeCells(
					FVector2D(FMath::FloorToDouble(Region.Min.X / Cache.CellSize), FMath::FloorToDouble(Region.Min.Y / Cache.CellSize)) * Cache.CellSize,
					FVector2D(FMath::FloorToDouble(Region.Max.X / Cache.CellSize) + 1, FMath::FloorToDouble(Region.Max.Y / Cache.CellSize) + 1) * Cache.CellSize);
				Index->FindVisualizationCandidates(WholeCells, Layers, Triangles);
				for (int32 Triangle : Triangles) { UniqueTriangles.Add(Triangle); }
			}
			Triangles = UniqueTriangles.Array(); Triangles.Sort();
			for (int32 Triangle : Triangles)
			{
				if (!Alive->load(std::memory_order_relaxed)) { return FPreparedEditPreview(); }
				if (Shape.ShapeId.IsValid() && Base->Source.TriangleShapeIds.IsValidIndex(Triangle)
					&& Base->Source.TriangleShapeIds[Triangle] == Shape.ShapeId) { continue; }
				const int32 First = Candidates.Vertices.Num();
				for (int32 Corner = 0; Corner < 3; ++Corner)
				{ Candidates.Vertices.Add(Base->Source.Vertices[Base->Source.Indices[Triangle * 3 + Corner]]); Candidates.Indices.Add(First + Corner); }
				Candidates.TriangleLayerIds.Add(Base->Source.TriangleLayerIds[Triangle]);
			}
		}
		else { Candidates = Base->Source; if (Shape.ShapeId.IsValid()) { RemoveShapeGeometry(Candidates, Shape.ShapeId); } }
		AppendShapeGeometry(Candidates, Geometry, Shape.ShapeId);
		if (Base->Checkpoint)
		{
			FBox2D Global = Cache.LocalBounds; Global += Bounds;
			// This is a sparse regional scratch cache, including an empty saved document.
			// Never reserve every cell of the gap between disjoint preview footprints.
			Cache.LocalBounds = Global;
			for (const auto& Region : Regions)
			{
				FAuthoringVisualizationUpdate Update; Update.Begin(Candidates, Layers, Cache, &Region, nullptr, INDEX_NONE, &Global, Cache.CellSize);
				while (Alive->load(std::memory_order_relaxed) && !Update.Advance(Candidates, Layers, Cache, 256)) {}
			}
			FPreparedEditPreview Result; Result.CellSize = Cache.CellSize; TSet<FIntPoint> PreparedTiles;
			for (const auto& Region : Regions)
			{
				auto Prepared = PrepareEditPreview(Cache, Layers, &Alive.Get(), &Region);
				for (auto& Tile : Prepared.Tiles) { if (!PreparedTiles.Contains(Tile.Tile)) { PreparedTiles.Add(Tile.Tile); Result.Tiles.Add(MoveTemp(Tile)); } }
			}
			return Result;
		}
		BuildAuthoringVisualization(Candidates, Layers, Cache);
		return PrepareEditPreview(Cache, Layers, &Alive.Get());
	});
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

bool FComposableCameraMeshLayerEdMode::UpdateCachedPreview(const FBox2D* DirtyBounds)
{
	// Synchronous callers have already installed this revision's mutable cache,
	// including an empty result. Never fall back to an older checkpoint for it.
	if (Settings && EditingCheckpoint && EditingCheckpoint->Revision != Settings->DocumentRevision) { RetireEditingCheckpoint(); }
	const bool bUpdated = Settings && TargetLevel.IsValid()
		&& EditPreview->Update(*TargetLevel.Get(), AnchorTransform, GetVisualization(), Settings->Layers, DirtyBounds);
	bEditPreviewDirty = !bUpdated;
	if (bUpdated && !bVisualizationDirty) { bHistoricalPreview = false; PreviewRevision = Settings->DocumentRevision; RememberPreview(); }
	return bUpdated;
}

void FComposableCameraMeshLayerEdMode::Render(
	const FSceneView* View,
	FViewport* Viewport,
	FPrimitiveDrawInterface* PDI)
{
	FEdMode::Render(View, Viewport, PDI);
	// Rendering only submits existing fill/controls. Full document resolution and
	// publication progress in Tick, including when the camera remains stationary.
	if (Settings && !bVisualizationDirty && !DocumentBuild->HasPending()
		&& !StrokeTask && !StrokeCoverage->HasPending() && !EditPreview->HasQueuedUpdates())
	{
		// Retain the original path if publication is unavailable (e.g. a tearing-down Level).
		if (!EditPreview->IsReadyFor(TargetLevel.Get()))
		{
			UE::ComposableCamera::MeshEditor::DrawVisualization(PDI, AnchorTransform, GetVisualization(), Settings->Layers);
		}
	}
	if (Settings)
	{
		if (!LiveDraftFill.Indices.IsEmpty()) { UE::ComposableCamera::MeshEditor::DrawShapePreview(PDI, AnchorTransform, LiveDraftFill); }
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
		bStrokeReleased ? LOCTEXT("StrokeCompleting", "Completing brush stroke")
			: IsCreatingShapes() ? LOCTEXT("ShapePreparing", "Completing floor projection")
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
