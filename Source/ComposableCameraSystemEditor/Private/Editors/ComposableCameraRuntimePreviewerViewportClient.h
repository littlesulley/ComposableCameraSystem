// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorViewportClient.h"
#include "Widgets/SComposableCameraRuntimePreviewer.h"

class AActor;
class FAdvancedPreviewScene;
class FCanvas;
class FPrimitiveDrawInterface;
class FSceneView;
class UStaticMesh;
class USkeletalMesh;

COMPOSABLECAMERASYSTEMEDITOR_API FText RuntimePreviewerStatusToText(ERuntimePreviewerStatus Status);

namespace ComposableCameraSystem::RuntimePreviewer
{
	COMPOSABLECAMERASYSTEMEDITOR_API FTransform MakeSubjectRelativeTransform(
		const FTransform& SourceWorldTransform,
		const FTransform& SubjectWorldTransform);

	COMPOSABLECAMERASYSTEMEDITOR_API FTransform MakeSkeletalSubjectWorldTransform(
		const FTransform& ComponentWorldTransform,
		const TArray<FTransform>& ComponentSpaceTransforms);

	COMPOSABLECAMERASYSTEMEDITOR_API float ComputeFloorOffsetForBounds(const FBox& Bounds);
}

class FComposableCameraRuntimePreviewerViewportClient: public FEditorViewportClient
{
public:
	FComposableCameraRuntimePreviewerViewportClient(FAdvancedPreviewScene* InPreviewScene,
		const TSharedRef<SEditorViewport>& InEditorViewportWidget);
	virtual ~FComposableCameraRuntimePreviewerViewportClient();

	void SetPreviewData(const FComposableCameraRuntimePreviewData& InData);
	void ClearPreviewData(ERuntimePreviewerStatus InStatus = ERuntimePreviewerStatus::NoPIE);
	void RefreshPreviewNow();
	void FramePreviewSubject();
	void ReleaseSceneResources();

	virtual void Tick(float DeltaSeconds) override;
	virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual void DrawCanvas(FViewport& InViewport, FSceneView& View, FCanvas& Canvas) override;

private:
	bool NeedsProxyRebuild() const;
	void RebuildPawnProxy();
	void DestroyPawnProxy();
	void SyncPawnProxy();
	AActor* SpawnProxyForPawn(AActor* SourcePawn);
	void UpdateFloorOffsetForProxy();

	FTransform GetCameraPreviewTransform() const;
	void DrawRuntimeCamera(const FSceneView* View, FPrimitiveDrawInterface* PDI) const;
	void DrawMovementArrow(FPrimitiveDrawInterface* PDI) const;

private:
	FAdvancedPreviewScene* PreviewScene = nullptr;
	FComposableCameraRuntimePreviewData PreviewData;
	ERuntimePreviewerStatus Status = ERuntimePreviewerStatus::NoPIE;

	TWeakObjectPtr<AActor> LastSourcePawn;
	TWeakObjectPtr<AActor> ProxyActor;
	TWeakObjectPtr<USkeletalMesh> LastSkeletalMesh;
	TWeakObjectPtr<UStaticMesh> LastStaticMesh;
	bool bProxyUsesFallback = false;
	bool bSkeletalPoseCopyFailed = false;
};
