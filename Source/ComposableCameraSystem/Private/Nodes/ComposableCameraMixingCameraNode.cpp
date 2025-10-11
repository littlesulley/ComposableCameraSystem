// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraMixingCameraNode.h"

#include "Utils/ComposableCameraBlueprintLibrary.h"

void UComposableCameraMixingCameraNode::OnBeginPlayNode_Implementation(const FComposableCameraPose& CurrentCameraPose)
{
	for (const FComposableCameraMixingCameraNodeCameraDefinition& Definition : Cameras)
	{
		FComposableCameraActivateParams ActivationParams (
			Definition.ActivationParams.bPreserveCameraPose,
			Definition.ActivationParams.InitialTransform,
			Definition.ActivationParams.NodeInitializerDataAsset,
			false,
			0.f
		);
		AComposableCameraCameraBase* CameraInstance = UComposableCameraBlueprintLibrary::CreateComposableCameraByClass(
			this, OwningPlayerCameraManager, Definition.CameraClass, ActivationParams);

		CameraInstances.Add(CameraInstance);
	}
}

void UComposableCameraMixingCameraNode::OnTickNode_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	
}

void UComposableCameraMixingCameraNode::BeginDestroy()
{
	Super::BeginDestroy();

	for (AComposableCameraCameraBase* CameraInstance : CameraInstances)
	{
		CameraInstance->Destroy();
	}
}
