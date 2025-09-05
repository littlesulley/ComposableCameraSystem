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
	
	OnTicked(CameraPose, NewCameraPose);
	
	LastFrameCameraPose = CameraPose;
	CameraPose = NewCameraPose;

	UpdateCamera(DeltaTime);
}

void AComposableCameraCameraBase::UpdateCamera(float DeltaTime)
{
	FComposableCameraPose OutCameraPose;
	FComposableCameraPose ThisCameraPose = CameraPose;

	if (OnUpdateCamera(DeltaTime, ThisCameraPose, OutCameraPose))
	{
		ThisCameraPose = OutCameraPose;
		CameraPose = OutCameraPose;
	}
	
	SetActorLocation(ThisCameraPose.Position);
	SetActorRotation(ThisCameraPose.Rotation);
	GetCameraComponent()->FieldOfView = ThisCameraPose.FieldOfView;
}

