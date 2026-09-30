// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdMode.h"
#include "MeshCamera/ComposableCameraMeshLayerRendering.h"

class AComposableCameraMeshSurfaceStorageActor;

class FComposableCameraMeshLayerPreviewEdMode : public FEdMode
{
public:
	static const FEditorModeID ModeId;

	virtual void Enter() override;
	virtual void Exit() override;
	virtual bool UsesToolkits() const override { return false; }
	virtual bool UsesTransformWidget() const override { return false; }
	virtual bool IsCompatibleWith(FEditorModeID OtherModeID) const override;
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;

private:
	TMap<
		TWeakObjectPtr<AComposableCameraMeshSurfaceStorageActor>,
		UE::ComposableCamera::MeshEditor::FResolvedSurfaceVisualization> VisualizationsByStorage;
};
