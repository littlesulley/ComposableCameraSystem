// Copyright Sulley. All rights reserved.

#include "Cameras/ComposableCameraCameraBase.h"

#include "Camera/CameraComponent.h"
#include "Core/ComposableCameraPlayerCamaraManager.h"
#include "DataAssets/ComposableCameraNodeInitializerDataAsset.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"

AComposableCameraCameraBase::AComposableCameraCameraBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	GetCameraComponent()->bConstrainAspectRatio = false;
}

void AComposableCameraCameraBase::BeginPlay()
{
	Super::BeginPlay();
	
	BeginPlayCamera(CameraManager->GetCurrentCameraPose());
}

void AComposableCameraCameraBase::Initialize(AComposableCameraPlayerCamaraManager* Manager, UComposableCameraNodeInitializerDataAsset* NodeInitializerDataAsset)
{
	TArray<UComposableCameraCameraNodeBase*> Initializers = NodeInitializerDataAsset ? NodeInitializerDataAsset->NodeParameterInitializers : TArray<UComposableCameraCameraNodeBase*>();
	
	CameraManager = Manager;
	for (UComposableCameraCameraNodeBase* Node : CameraNodes)
	{
		if (Node)
		{
			Node->Initialize(this,  Manager, Initializers);
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

	LastFrameCameraPose = CameraPose;
	CameraPose = NewCameraPose;
	
	if (OnUpdateCamera(DeltaTime, LastFrameCameraPose, NewCameraPose, NewCameraPose))
	{
		CameraPose = NewCameraPose;
	}

	if (bIsTransient)
	{
		RemainingLifeTime = FMath::Max(0.f, RemainingLifeTime - DeltaTime);
	}
	
	return CameraPose;
}

UComposableCameraCameraNodeBase* AComposableCameraCameraBase::GetNodeByClass(
	TSubclassOf<UComposableCameraCameraNodeBase> NodeClass)
{
	UComposableCameraCameraNodeBase* Node = nullptr;

	for (UComposableCameraCameraNodeBase* OwningNode : CameraNodes)
	{
		if (OwningNode && OwningNode->GetClass() == NodeClass)
		{
			Node = OwningNode;
			break;
		}
	}

	return Node;
}


