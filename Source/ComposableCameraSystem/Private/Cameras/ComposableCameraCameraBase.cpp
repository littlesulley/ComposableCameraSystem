// Copyright Sulley. All rights reserved.

#include "Cameras/ComposableCameraCameraBase.h"

UComposableCameraCameraBase::UComposableCameraCameraBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UComposableCameraCameraBase::Initialize(AComposableCameraPlayerCamaraManager* Manager)
{
	CameraManager = Manager;
}

void UComposableCameraCameraBase::TickCamera(float DeltaTime)
{
	CameraPose = OnTickCamera(DeltaTime);
}

void UComposableCameraCameraBase::BeginPlayCamera()
{
	OnBeginPlayCamera();
}
