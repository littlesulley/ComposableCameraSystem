// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdMode.h"
#include "MeshCamera/ComposableCameraMeshLayerPreviewBuild.h"

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
	virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) override;
	/** Core ticker also calls this: static/throttled viewports must not gate loading. */
	void AdvancePreview();

private:
	friend class FComposableCameraMeshStationaryPreviewTest;
	struct FPreviewCache
	{
		UE::ComposableCamera::MeshEditor::FPreviewGeometryBuild Build;
		TArray<UE::ComposableCamera::MeshEditor::FNativePreviewLayerMesh> ReadyMeshes;
		TWeakObjectPtr<AActor> PreviewActor;
		int32 NextMesh = 0;
		uint64 LastSeenFrame = MAX_uint64;
		uint64 DataRevision = 0;
		bool bRestartAfterPIE = false;
	};
	TMap<TWeakObjectPtr<AComposableCameraMeshSurfaceStorageActor>, FPreviewCache> Previews;
	TArray<TWeakObjectPtr<AComposableCameraMeshSurfaceStorageActor>> WorkOrder;
	int32 NextPublicationJob = 0;
	uint64 LastTickFrame = MAX_uint64;
	int32 PendingRedrawFrames = 0;
	void ResetPreviews();
};
