// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdMode.h"
#include "EditorUndoClient.h"
#include "ScopedTransaction.h"
#include "MeshCamera/ComposableCameraMeshLayerRendering.h"
#include "MeshCamera/ComposableCameraMeshSurfaceTypes.h"

class AComposableCameraMeshSurfaceStorageActor;
class FComposableCameraMeshLayerModeToolkit;
class UComposableCameraMeshLayerToolSettings;
class UComposableCameraMeshLayerSelection;
class ULevel;
class UWorld;
namespace UE::ComposableCamera::MeshEditor { class FMeshLayerEditPreview; class FMeshLayerAuthoringIndex; class FMeshLayerStrokeCoverage; class FMeshLayerDocumentBuild; }
namespace UE::ComposableCamera::MeshEditor { struct FEditPreviewCheckpoint; }
namespace UE::ComposableCamera::MeshEditor { struct FPreparedEditPreview; }
struct FComposableCameraMeshShapeCreationTask;
struct FComposableCameraMeshStrokeTask;
struct FComposableCameraMeshLiveShapeTask;
struct FComposableCameraMeshLiveShapeBase;
struct FCollisionQueryParams;
enum class EComposableCameraMeshDrawTool : uint8;

class FComposableCameraMeshLayerEdMode : public FEdMode, public FEditorUndoClient
{
public:
	static const FEditorModeID ModeId;
	FComposableCameraMeshLayerEdMode();
	virtual ~FComposableCameraMeshLayerEdMode() override;

	virtual void Enter() override;
	virtual void Exit() override;
	virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) override;
	virtual bool UsesToolkits() const override { return true; }
	virtual bool UsesTransformWidget() const override { return false; }
	virtual bool InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event) override;
	virtual bool MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 X, int32 Y) override;
	virtual bool CapturedMouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 X, int32 Y) override;
	virtual bool LostFocus(FEditorViewportClient* ViewportClient, FViewport* Viewport) override;
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FComposableCameraMeshLayerEdMode"); }
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override { PostUndo(bSuccess); }

	UComposableCameraMeshLayerToolSettings* GetSettings() const { return Settings; }
	UComposableCameraMeshLayerSelection* GetSelectionEditor() const { return SelectionEditor; }
	bool HasSelectedShape() const;
	void DeleteSelectedShape();
	bool SaveWorkingData();
	bool CanDiscardWorkingData() const;
	bool DiscardWorkingData();
	bool IsDirty() const { return bDirty || bPendingSaveNeedsDiscard; }
	bool IsCreatingShapes() const { return !PendingShapes.IsEmpty(); }
	FText GetStatusText() const;
	FText GetDocumentInfoText() const;
	static FText GetToolInstructions(EComposableCameraMeshDrawTool Tool);
	void RequestCloseFromToolkit();

private:
	friend struct FComposableCameraMeshStrokeTask;
	friend class FComposableCameraMeshLayerModeToolkit;
	friend class FComposableCameraMeshShapeInteractionTest;
	friend class FComposableCameraMeshShapeEditingTest;
	friend class FComposableCameraMeshDocumentUndoTest;
	friend class FComposableCameraMeshToolPanelsTest;
	friend class FComposableCameraMeshStrokeRefreshTest;
	friend class FComposableCameraMeshShapeCreationTest;
	friend class FComposableCameraMeshDiscardTest;
	friend class FComposableCameraMeshLayerTraceChannelTest;
	friend class FComposableCameraMeshUnevenBrushTest;
	friend class FComposableCameraMeshEditPreviewInvalidationTest;
	friend class FComposableCameraMeshBudgetedStrokeTest;
	friend class FComposableCameraMeshContinuousEraseTest;
	friend class FComposableCameraMeshDocumentPreviewTest;
	friend class FComposableCameraMeshRestorationDisplayTest;
	friend class FComposableCameraMeshRestoredStrokeTest;
	friend class FComposableCameraMeshRealtimeEditingTest;
	friend class FComposableCameraMeshResidentLoadingTest;
	friend class FComposableCameraMeshSavePreparationTest;
	FComposableCameraMeshSurfaceEditorPreview PrepareSavePreview(bool& bOutReusedCoverage);
	void QueuePaintAtHover(FEditorViewportClient* ViewportClient);
	void AdvancePainting(int32 MaxOperations = MAX_int32, double TimeBudgetSeconds = 0.004, bool bUpdatePreview = true);
	void AdvanceStrokeCoverage(bool bFlush);
	void AdvanceDocumentPreview(const FVector2D& LocalFocus = FVector2D::ZeroVector, bool bWait = false);
	void CancelDocumentBuild();
	void AdvanceOpeningRegions();
	void QueueOpeningRegion(const FBox2D& DirtyBounds);
	void PrepareUndo();
	void RememberPreview();
	const UE::ComposableCamera::MeshEditor::FResolvedSurfaceVisualization& GetVisualization() const;
	void RetireEditingCheckpoint();
	bool UpdateCachedPreview(const FBox2D* DirtyBounds = nullptr);
	void CancelInteraction();
	void ResetInteraction();
	bool QueueShapeCreation(FComposableCameraMeshAuthoredShape Shape, UWorld* World);
	void AdvanceShapeCreation();
	void CancelPendingShapes();
	void RetirePendingShape(int32 Index);
	void RequestLiveShapePreview(FComposableCameraMeshAuthoredShape Shape);
	void AdvanceLiveShapePreview();
	void CancelLiveShapePreview();
	const UE::ComposableCamera::MeshEditor::FResolvedSurfaceLayerMesh& GetPendingShapePreview(int32 Index) const;
	void ApplyCreatedShape(FComposableCameraMeshSurfaceAuthoringData Data,
		UE::ComposableCamera::MeshEditor::FResolvedSurfaceVisualization Resolved, const FGuid& ShapeId, bool bPartial,
		UE::ComposableCamera::MeshEditor::FPreparedEditPreview* Prepared = nullptr, bool bFull = true);
	void BeginShape();
	void UpdateShapePreview();
	bool CommitShape(TConstArrayView<FVector2D> Outline);
	FVector GetShapePlanePosition(const FVector2D& Point) const;
	bool CommitEditedShape(FComposableCameraMeshAuthoredShape Shape, bool bTransact = true);
	void ApplyShapeGeometry(FComposableCameraMeshAuthoredShape Shape, FComposableCameraMeshSurfaceAuthoringData ShapeData, bool bTransact);
	void RefreshSelectionEditor();
	void BeforeSelectionEdit();
	void ApplySelectedLayer();
	void ApplySelectedShape(const FPropertyChangedEvent& Event);
	void HandleToolSettingsChanged();
	void UpdateEditPreview();
	void FinishStroke(bool bRevert = false, bool bKeepPreview = false);
	void BeginStroke();
	void StartDeferredStroke();
	void CaptureStrokeSourceBeforeErase();
	void RefreshDocumentState();
	void RebuildShapeOverlays();
	bool InitializeWorkingDocument();
	void CaptureSavedDocument();
	void RefreshPendingSaveState();
	AComposableCameraMeshSurfaceStorageActor* FindStorageActor() const;
	AComposableCameraMeshSurfaceStorageActor* FindOrCreateStorageActor();
	bool UpdateHoverHit(FEditorViewportClient* ViewportClient);
	/** Resolve by owning Layer GUID, never by whichever row happens to be selected. */
	bool TraceLayerSurface(const UWorld& World, const FGuid& LayerId, const FVector& Start, const FVector& End,
		const FCollisionQueryParams& QueryParams, FHitResult& OutHit) const;
	bool PaintAtHover(FEditorViewportClient* ViewportClient);
	bool AddProjectedBrushStamp(const FHitResult& CenterHit, const FGuid& LayerId);
	bool EraseBrushStamp(const FHitResult& CenterHit, const FGuid& LayerId, FBox2D* OutDirtyBounds = nullptr);
	void RemoveOrphanedTriangles();
	void MarkLayerDataDirty();

	TObjectPtr<UComposableCameraMeshLayerToolSettings> Settings;
	/** Nontransactional checkpoint; reflected Layer asset references remain GC-visible. */
	TObjectPtr<UComposableCameraMeshLayerToolSettings> SavedDocument;
	TObjectPtr<UComposableCameraMeshLayerSelection> SelectionEditor;
	TWeakObjectPtr<ULevel> TargetLevel;
	TWeakObjectPtr<AComposableCameraMeshSurfaceStorageActor> StorageActor;
	/** A failed package save can already have applied unsaved data to this actor. */
	TWeakObjectPtr<AComposableCameraMeshSurfaceStorageActor> PendingSaveActor;
	bool bPendingSaveCreatedActor = false;
	bool bPendingSaveNeedsDiscard = false;
	FTransform AnchorTransform = FTransform::Identity;
	FGuid SavedRevision;
	FGuid SelectedShapeId;
	FComposableCameraMeshAuthoredShape EditShape;
	FVector2D EditDragStart = FVector2D::ZeroVector;
	int32 EditControlIndex = INDEX_NONE;
	bool bEditingShape = false;
	FComposableCameraMeshAuthoredShape LiveShape;
	UE::ComposableCamera::MeshEditor::FResolvedSurfaceLayerMesh LiveDraftFill;
	TUniquePtr<FComposableCameraMeshLiveShapeTask> LiveShapeTask;
	TSharedPtr<const FComposableCameraMeshLiveShapeBase, ESPMode::ThreadSafe> LiveShapeBase;
	bool bLiveShapeRequested = false, bLiveShapeVisible = false;
	FBox2D LivePublishedBounds = FBox2D(ForceInit);
	uint64 LastLiveShapeTickFrame = MAX_uint64;
	struct FShapeOverlay
	{
		FGuid ShapeId;
		TArray<FVector> Outline;
		TArray<FVector> Controls;
	};
	TArray<FShapeOverlay> ShapeOverlays;
	TArray<TUniquePtr<FComposableCameraMeshShapeCreationTask>> PendingShapes;
	uint64 LastShapeTickFrame = MAX_uint64;
	TUniquePtr<FScopedTransaction> StrokeTransaction;
	FComposableCameraMeshSurfaceAuthoringData StrokeStartData;
	int32 StrokeStartVertices = 0, StrokeStartIndices = 0, StrokeStartShapeIds = 0;
	bool bStrokeSourceSnapshot = false;
	FGuid StrokeStartRevision;
	struct FStrokeStamp
	{
		FGuid LayerId;
		FVector Center = FVector::ZeroVector, Normal = FVector::UpVector;
		double Radius = 0.0, ProjectionDistance = 0.0, MinimumFloorNormalZ = 0.0;
		int32 Segments = 12;
		ECollisionChannel Channel = ECC_Visibility;
		bool bErase = false;
	};
	TArray<FStrokeStamp> QueuedStamps;
	struct FDeferredStroke
	{
		TArray<FStrokeStamp> Stamps;
		FVector LastPosition = FVector::ZeroVector;
		bool bHasLastPosition = false, bReleased = false;
	};
	TArray<FDeferredStroke> DeferredStrokes;
	int32 QueuedStampIndex = 0;
	TUniquePtr<FComposableCameraMeshStrokeTask> StrokeTask;
	uint64 LastStrokeTickFrame = MAX_uint64;
	bool bStrokeReleased = false;
	UE::ComposableCamera::MeshEditor::FResolvedSurfaceVisualization Visualization;
	TSharedPtr<const UE::ComposableCamera::MeshEditor::FEditPreviewCheckpoint, ESPMode::ThreadSafe> EditingCheckpoint;
	TUniquePtr<UE::ComposableCamera::MeshEditor::FMeshLayerEditPreview> EditPreview;
	TUniquePtr<UE::ComposableCamera::MeshEditor::FMeshLayerAuthoringIndex> AuthoringIndex;
	TUniquePtr<UE::ComposableCamera::MeshEditor::FMeshLayerStrokeCoverage> StrokeCoverage;
	TUniquePtr<UE::ComposableCamera::MeshEditor::FMeshLayerDocumentBuild> DocumentBuild;
	TUniquePtr<UE::ComposableCamera::MeshEditor::FMeshLayerStrokeCoverage> OpeningRegionCoverage;
	UE::ComposableCamera::MeshEditor::FResolvedSurfaceVisualization OpeningRegionVisualization;
	TSet<FIntPoint> OpeningEditedRegions;
	double OpeningCellSize = 10.0;
	bool bOpeningIndexInstalled = false;
	uint64 LastDocumentTickFrame = MAX_uint64;
	uint64 LastRedrawFrame = MAX_uint64;
	int32 PendingRedrawFrames = 0;
	FGuid PreviewRevision;
	bool bHistoricalPreview = false;
	FHitResult HoverHit;
	FHitResult ShapeStartHit;
	FGuid ShapeLayerId;
	TArray<FVector2D> ShapePoints;
	TArray<FVector2D> ShapePreview;
	FVector2D ShapeStart = FVector2D::ZeroVector;
	FVector2D ShapeEnd = FVector2D::ZeroVector;
	FText ShapeFeedback;
	FText ShapeMeasurement;
	bool bDrawingShape = false;
	FVector LastPaintWorldPosition = FVector::ZeroVector;
	bool bHasHoverHit = false;
	bool bHasLastPaintPosition = false;
	bool bPainting = false;
	bool bDirty = false;
	bool bVisualizationDirty = true;
	bool bEditPreviewDirty = true;
	bool bExiting = false;
};
