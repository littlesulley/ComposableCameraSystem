// Copyright Sulley. All rights reserved.

#include "Cameras/ComposableCameraCameraBase.h"

#include "Actions/ComposableCameraActionBase.h"
#include "Camera/CameraComponent.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "DataAssets/ComposableCameraNodeInitializerDataAsset.h"
#include "Modifiers/ComposableCameraModifierBase.h"
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

void AComposableCameraCameraBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	OnPreTick.Clear();
	OnPostTick.Clear();
	OnActionPreTick.Clear();
	OnActionPostTick.Clear();
}

void AComposableCameraCameraBase::Initialize(AComposableCameraPlayerCameraManager* Manager, UComposableCameraNodeInitializerDataAsset* NodeInitializerDataAsset)
{
	TArray<UComposableCameraCameraNodeBase*> Initializers = NodeInitializerDataAsset ? NodeInitializerDataAsset->NodeParameterInitializers : TArray<UComposableCameraCameraNodeBase*>();
	
	CameraManager = Manager;
	for (UComposableCameraCameraNodeBase* Node : CameraNodes)
	{
		if (Node)
		{
			Node->Initialize(this,  Manager, Initializers);

			// Register node delegates.
			OnPreTick.AddUObject(Node, &UComposableCameraCameraNodeBase::OnPreTick);
			OnPostTick.AddUObject(Node, &UComposableCameraCameraNodeBase::OnPostTick);
		}
	}

	Manager->BindCameraActionsForNewCamera(this);
}

void AComposableCameraCameraBase::ApplyModifiers(const T_NodeModifier& Modifiers)
{
	for (UComposableCameraCameraNodeBase* Node : CameraNodes)
	{
		if (!Node)
		{
			continue;
		}
		
		if (const FModifierEntry* Modifier = Modifiers.Find(Node->GetClass()))
		{
			if (Modifier->Modifier)
			{
				Modifier->Modifier->ApplyModifier(Node);
			}
		}
	}
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
	FComposableCameraPose NewCameraPose = CameraPose;

	// Execute pre-tick actions.
	OnActionPreTick.Broadcast(DeltaTime, NewCameraPose, NewCameraPose);

	// Do something before camera tick begins.
	OnPreTick.Broadcast(DeltaTime, NewCameraPose, NewCameraPose);

	// Tick each node by order.
	for (UComposableCameraCameraNodeBase* Node : CameraNodes)
	{
		if (Node)
		{
			Node->TickNode(DeltaTime, NewCameraPose, NewCameraPose);
		}
	}

	// Do something when camera tick finishes.
	OnPostTick.Broadcast(DeltaTime, NewCameraPose, NewCameraPose);

	// Execute post-tick actions.
	OnActionPostTick.Broadcast(DeltaTime, NewCameraPose, NewCameraPose);
	
	// Cache camera pose.
	LastFrameCameraPose = CameraPose;
	CameraPose = NewCameraPose;

	// Override camera pose by blueprint.
	if (OnUpdateCamera(DeltaTime, LastFrameCameraPose, NewCameraPose, NewCameraPose))
	{
		CameraPose = NewCameraPose;
	}

	// Update remaining life time if transient.
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


