// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AdvancedPreviewScene.h"
#include "SEditorViewport.h"

class AActor;
class FComposableCameraRuntimePreviewerViewportClient;

enum class ERuntimePreviewerStatus : uint8
{
	NoPIE,
	NoCamera,
	NoPawn,
	Live
};

struct FComposableCameraRuntimePreviewData
{
	TWeakObjectPtr<AActor> ControlledPawn;
	FTransform SubjectWorldTransform = FTransform::Identity;
	FVector PawnVelocity = FVector::ZeroVector;
	FVector CameraPosition = FVector::ZeroVector;
	FRotator CameraRotation = FRotator::ZeroRotator;
	double CameraFieldOfView = 90.0;
	bool bHasValidCameraPose = false;
	FName ContextName = NAME_None;
	bool bIsActiveCamera = false;
};

class COMPOSABLECAMERASYSTEMEDITOR_API SComposableCameraRuntimePreviewer: public SEditorViewport
{
public:
	SLATE_BEGIN_ARGS(SComposableCameraRuntimePreviewer) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SComposableCameraRuntimePreviewer() override;

	void SetPreviewData(const FComposableCameraRuntimePreviewData& InData);
	void ClearPreviewData(ERuntimePreviewerStatus Status = ERuntimePreviewerStatus::NoPIE);
	void FramePreviewSubject();

protected:
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
	virtual TSharedPtr<SWidget> MakeViewportToolbar() override;

private:
	TUniquePtr<FAdvancedPreviewScene> PreviewScene;
	TSharedPtr<FComposableCameraRuntimePreviewerViewportClient> ViewportClient;
};
