// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraCameraNodeBase.h"

#include "Cameras/ComposableCameraCameraBase.h"

void UComposableCameraCameraNodeBase::Initialize(AComposableCameraCameraBase* InOwningCamera, AComposableCameraPlayerCamaraManager* InPlayerCameraManager)
{
	OwningCamera = InOwningCamera;
	OwningPlayerCameraManager = InPlayerCameraManager;
	OnInitialize();
}

void UComposableCameraCameraNodeBase::TickNode(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	OnTickNode(DeltaTime, CurrentCameraPose, OutCameraPose);
}

void UComposableCameraCameraNodeBase::BeginPlayNode()
{
	OnBeginPlayNode();
}

FGameplayTag UComposableCameraCameraNodeBase::GetOwningCameraTag() const
{
	return OwningCamera ? OwningCamera->CameraTag : FGameplayTag::EmptyTag;
}

TArray<UComposableCameraPoseContextBase*> UComposableCameraCameraNodeBase::GetOwningCameraPoseContexts() const
{
	TArray<UComposableCameraPoseContextBase*> Contexts;
	ContextClassToContextMap.GenerateValueArray(Contexts);
	return Contexts;
}

UComposableCameraPoseContextBase* UComposableCameraCameraNodeBase::GetOwningCameraPoseContextByClass(
	TSubclassOf<UComposableCameraPoseContextBase> ContextClass) const
{
	return *ContextClassToContextMap.Find(ContextClass);
}

