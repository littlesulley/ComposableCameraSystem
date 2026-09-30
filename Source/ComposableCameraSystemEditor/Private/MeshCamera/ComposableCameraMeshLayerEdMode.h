// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdMode.h"
#include "MeshCamera/ComposableCameraMeshLayerRendering.h"
#include "MeshCamera/ComposableCameraMeshSurfaceTypes.h"

class AComposableCameraMeshSurfaceStorageActor;
class FComposableCameraMeshLayerModeToolkit;
class UComposableCameraMeshLayerToolSettings;
class ULevel;

class FComposableCameraMeshLayerEdMode : public FEdMode
{
public:
	static const FEditorModeID ModeId;

	virtual void Enter() override;
	virtual void Exit() override;
	virtual bool UsesToolkits() const override { return true; }
	virtual bool UsesTransformWidget() const override { return false; }
	virtual bool InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event) override;
	virtual bool MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 X, int32 Y) override;
	virtual bool CapturedMouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 X, int32 Y) override;
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FComposableCameraMeshLayerEdMode"); }

	UComposableCameraMeshLayerToolSettings* GetSettings() const { return Settings; }
	bool SaveWorkingData();
	bool IsDirty() const { return bDirty; }
	FText GetStatusText() const;
	void RequestCloseFromToolkit();

private:
	bool InitializeWorkingDocument();
	AComposableCameraMeshSurfaceStorageActor* FindStorageActor() const;
	AComposableCameraMeshSurfaceStorageActor* FindOrCreateStorageActor();
	bool UpdateHoverHit(FEditorViewportClient* ViewportClient);
	bool PaintAtHover(FEditorViewportClient* ViewportClient);
	bool AddProjectedBrushStamp(const FHitResult& CenterHit, const FGuid& LayerId);
	bool EraseBrushStamp(const FHitResult& CenterHit, const FGuid& LayerId);
	void RemoveOrphanedTriangles();
	void MarkLayerDataDirty();

	TObjectPtr<UComposableCameraMeshLayerToolSettings> Settings;
	TWeakObjectPtr<ULevel> TargetLevel;
	TWeakObjectPtr<AComposableCameraMeshSurfaceStorageActor> StorageActor;
	FTransform AnchorTransform = FTransform::Identity;
	FComposableCameraMeshSurfaceAuthoringData WorkingData;
	UE::ComposableCamera::MeshEditor::FResolvedSurfaceVisualization Visualization;
	FHitResult HoverHit;
	FVector LastPaintWorldPosition = FVector::ZeroVector;
	bool bHasHoverHit = false;
	bool bHasLastPaintPosition = false;
	bool bPainting = false;
	bool bDirty = false;
	bool bVisualizationDirty = true;
	bool bExiting = false;
};
