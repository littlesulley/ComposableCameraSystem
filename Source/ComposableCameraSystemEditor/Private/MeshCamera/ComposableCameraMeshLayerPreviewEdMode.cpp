// Copyright 2026 Sulley. All Rights Reserved.

#include "MeshCamera/ComposableCameraMeshLayerPreviewEdMode.h"

#include "EngineUtils.h"
#include "MeshCamera/ComposableCameraMeshLayerEdMode.h"
#include "MeshCamera/ComposableCameraMeshLayerRendering.h"
#include "MeshCamera/ComposableCameraMeshSurfaceStorageActor.h"

const FEditorModeID FComposableCameraMeshLayerPreviewEdMode::ModeId =
	TEXT("EM_ComposableCameraMeshLayerPreview");

void FComposableCameraMeshLayerPreviewEdMode::Enter()
{
	FEdMode::Enter();
	VisualizationsByStorage.Reset();
}

void FComposableCameraMeshLayerPreviewEdMode::Exit()
{
	VisualizationsByStorage.Reset();
	FEdMode::Exit();
}

bool FComposableCameraMeshLayerPreviewEdMode::IsCompatibleWith(FEditorModeID OtherModeID) const
{
	return OtherModeID != FComposableCameraMeshLayerEdMode::ModeId;
}

void FComposableCameraMeshLayerPreviewEdMode::Render(
	const FSceneView* View,
	FViewport* Viewport,
	FPrimitiveDrawInterface* PDI)
{
	FEdMode::Render(View, Viewport, PDI);
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<AComposableCameraMeshSurfaceStorageActor> Iterator(World); Iterator; ++Iterator)
	{
		AComposableCameraMeshSurfaceStorageActor* StorageActor = *Iterator;
		const TWeakObjectPtr<AComposableCameraMeshSurfaceStorageActor> StorageKey(StorageActor);
		UE::ComposableCamera::MeshEditor::FResolvedSurfaceVisualization* Visualization =
			VisualizationsByStorage.Find(StorageKey);
		if (!Visualization)
		{
			UE::ComposableCamera::MeshEditor::FResolvedSurfaceVisualization NewVisualization;
			UE::ComposableCamera::MeshEditor::BuildRuntimeVisualization(
				StorageActor->GetRuntimeData(),
				StorageActor->GetLayers(),
				NewVisualization);
			Visualization = &VisualizationsByStorage.Add(
				StorageKey,
				MoveTemp(NewVisualization));
		}

		UE::ComposableCamera::MeshEditor::DrawVisualization(
			PDI,
			StorageActor->GetActorTransform(),
			*Visualization,
			StorageActor->GetLayers());
	}
}
