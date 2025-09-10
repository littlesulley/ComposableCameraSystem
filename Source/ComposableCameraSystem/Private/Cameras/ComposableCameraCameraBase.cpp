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
	for (UComposableCameraCameraNodeBase* Node : CameraNodes)
	{
		Node->Initialize(this,  Manager);
	}
	OnInitialized();
}

void AComposableCameraCameraBase::BeginPlayCamera(const FComposableCameraPose& CurrentCameraPose)
{
	for (UComposableCameraCameraNodeBase* Node : CameraNodes)
	{
		Node->BeginPlayNode(CurrentCameraPose);
	}
}

FComposableCameraPose AComposableCameraCameraBase::TickCamera(float DeltaTime)
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
	
	return CameraPose;
}


