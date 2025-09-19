// Copyright Sulley. All rights reserved.

#include "Cameras/ComposableCameraCameraBase.h"

#include "Camera/CameraComponent.h"
#include "Core/ComposableCameraPlayerCamaraManager.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"

AComposableCameraCameraBase::AComposableCameraCameraBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void AComposableCameraCameraBase::BeginPlay()
{
	Super::BeginPlay();
	
	BeginPlayCamera(CameraManager->GetCurrentCameraPose());
}

void AComposableCameraCameraBase::Initialize(AComposableCameraPlayerCamaraManager* Manager)
{
	CameraManager = Manager;
	for (UComposableCameraCameraNodeBase* Node : CameraNodes)
	{
		if (Node)
		{
			Node->Initialize(this,  Manager);
		}
	}
	OnInitialized();
}

void AComposableCameraCameraBase::BeginPlayCamera(const FComposableCameraPose& CurrentCameraPose)
{
	for (UComposableCameraCameraNodeBase* Node : CameraNodes)
	{
		if (Node)
		{
			Node->BeginPlayNode(CurrentCameraPose);
		}
	}
}

FComposableCameraPose AComposableCameraCameraBase::TickCamera(float DeltaTime)
{
	FComposableCameraPose NewCameraPose {};
	
	for (UComposableCameraCameraNodeBase* Node : CameraNodes)
	{
		if (Node)
		{
			Node->TickNode(DeltaTime, NewCameraPose, NewCameraPose);
		}
	}

	CameraPose = NewCameraPose;
	if (OnUpdateCamera(DeltaTime, LastFrameCameraPose, NewCameraPose, NewCameraPose))
	{
		CameraPose = NewCameraPose;
	}
	LastFrameCameraPose = CameraPose;
	
	return CameraPose;
}


