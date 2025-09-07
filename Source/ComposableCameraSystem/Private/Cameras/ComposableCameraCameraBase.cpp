// Copyright Sulley. All rights reserved.

#include "Cameras/ComposableCameraCameraBase.h"

#include "Camera/CameraComponent.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"

AComposableCameraCameraBase::AComposableCameraCameraBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void AComposableCameraCameraBase::Initialize(AComposableCameraPlayerCamaraManager* Manager)
{
	CameraManager = Manager;
	OnInitialized();
}

void AComposableCameraCameraBase::BeginPlayCamera()
{
	for (UComposableCameraCameraNodeBase* Node : CameraNodes)
	{
		Node->BeginPlayNode();
	}
}

void AComposableCameraCameraBase::TickCamera(float DeltaTime)
{
	FComposableCameraPose NewCameraPose {};
	
	for (UComposableCameraCameraNodeBase* Node : CameraNodes)
	{
		Node->TickNode(DeltaTime, NewCameraPose, NewCameraPose);
	}

	CameraPose = NewCameraPose;
	if (OnUpdateCamera(DeltaTime, LastFrameCameraPose, NewCameraPose, NewCameraPose))
	{
		CameraPose = NewCameraPose;
	}
	LastFrameCameraPose = CameraPose;

	UpdateCamera();
}

void AComposableCameraCameraBase::UpdateCamera()
{
	SetActorLocation(CameraPose.Position);
	SetActorRotation(CameraPose.Rotation);
	GetCameraComponent()->FieldOfView = CameraPose.FieldOfView;
}

