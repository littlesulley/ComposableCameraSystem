// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraCameraNodeBase.h"

#include "Cameras/ComposableCameraCameraBase.h"

void UComposableCameraCameraNodeBase::Initialize(AComposableCameraCameraBase* InOwningCamera, AComposableCameraPlayerCamaraManager* InPlayerCameraManager, TArray<UComposableCameraCameraNodeBase*>& Initializers)
{
	OwningCamera = InOwningCamera;
	OwningPlayerCameraManager = InPlayerCameraManager;

	for (UComposableCameraCameraNodeBase* Initializer : Initializers)
	{
		if (Initializer && Initializer->StaticClass() == this->StaticClass())
		{
			ReceiveInitializerNode(Initializer);
		}
	}
	
	OnInitialize();
}

void UComposableCameraCameraNodeBase::TickNode(float DeltaTime, const FComposableCameraPose CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	OnTickNode(DeltaTime, CurrentCameraPose, OutCameraPose);
}

void UComposableCameraCameraNodeBase::BeginPlayNode(const FComposableCameraPose& CurrentCameraPose)
{
	OnBeginPlayNode(CurrentCameraPose);
}

FGameplayTag UComposableCameraCameraNodeBase::GetOwningCameraTag() const
{
	return OwningCamera ? OwningCamera->CameraTag : FGameplayTag::EmptyTag;
}
