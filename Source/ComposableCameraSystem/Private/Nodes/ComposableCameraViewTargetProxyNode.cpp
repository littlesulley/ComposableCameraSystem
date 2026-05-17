// Copyright 2026 Sulley. All Rights Reserved.

#include "Nodes/ComposableCameraViewTargetProxyNode.h"

#include "Camera/CameraComponent.h"
#include "Engine/PostProcessUtils.h"

void UComposableCameraViewTargetProxyNode::SetViewTargetActor(AActor* InActor)
{
	ViewTargetActor = InActor;
	CachedCameraComponent = InActor ? InActor->FindComponentByClass<UCameraComponent>() : nullptr;
}

void UComposableCameraViewTargetProxyNode::OnTickNode_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	UCameraComponent* CameraComp = CachedCameraComponent.Get();
	if (!CameraComp)
	{
		return;
	}

	FMinimalViewInfo ViewInfo;
	CameraComp->GetCameraView(DeltaTime, ViewInfo);

	// Transform.
	OutCameraPose.Position = ViewInfo.Location;
	OutCameraPose.Rotation = ViewInfo.Rotation;

	// FOV: always in degrees mode.
	OutCameraPose.SetFieldOfViewDegrees(ViewInfo.FOV);

	// Projection.
	OutCameraPose.ProjectionMode = ViewInfo.ProjectionMode;
	OutCameraPose.ConstrainAspectRatio = ViewInfo.bConstrainAspectRatio;
	OutCameraPose.OrthographicWidth = ViewInfo.OrthoWidth;
	OutCameraPose.OrthoNearClipPlane = ViewInfo.OrthoNearClipPlane;
	OutCameraPose.OrthoFarClipPlane = ViewInfo.OrthoFarClipPlane;

	// Post-process: CineCameraComponent's GetCameraView already bakes physical
	// camera settings (DoF, exposure) into PostProcessSettings.
	FPostProcessUtils::OverridePostProcessSettings(OutCameraPose.PostProcessSettings, ViewInfo.PostProcessSettings);
}
