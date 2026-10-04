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
struct FComposableCameraMeshShapeCreationTask;
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
	friend class FComposableCameraMeshLayerModeToolkit;
	friend class FComposableCameraMeshShapeInteractionTest;
	friend class FComposableCameraMeshShapeEditingTest;
	friend class FComposableCameraMeshDocumentUndoTest;
	friend class FComposableCameraMeshToolPanelsTest;
	friend class FComposableCameraMeshStrokeRefreshTest;
	friend class FComposableCameraMeshShapeCreationTest;
	friend class FComposableCameraMeshDiscardTest;
	void CancelInteraction();
	void ResetInteraction();
	bool QueueShapeCreation(FComposableCameraMeshAuthoredShape Shape, UWorld* World);
	void AdvanceShapeCreation();
	const UE::ComposableCamera::MeshEditor::FResolvedSurfaceLayerMesh& GetPendingShapePreview(int32 Index) const;
	void ApplyCreatedShape(FComposableCameraMeshSurfaceAuthoringData Data,
		UE::ComposableCamera::MeshEditor::FResolvedSurfaceVisualization Resolved, const FGuid& ShapeId, bool bPartial);
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
	void FinishStroke(bool bRevert = false);
	void BeginStroke();
	void RefreshDocumentState();
	void RebuildShapeOverlays();
	bool InitializeWorkingDocument();
	void CaptureSavedDocument();
	void RefreshPendingSaveState();
	AComposableCameraMeshSurfaceStorageActor* FindStorageActor() const;
	AComposableCameraMeshSurfaceStorageActor* FindOrCreateStorageActor();
	bool UpdateHoverHit(FEditorViewportClient* ViewportClient);
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
	FGuid StrokeStartRevision;
	UE::ComposableCamera::MeshEditor::FResolvedSurfaceVisualization Visualization;
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
	bool bExiting = false;
};
