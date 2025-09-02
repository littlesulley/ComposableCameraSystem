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
	TArray<TSubclassOf<UComposableCameraPoseContextBase>> ContextClasses;
	
	for (UComposableCameraCameraNodeBase* Node : CameraNodes)
	{
		Node->Initialize(this, CameraManager);
		ContextClasses.Append(Node->GetRequiredContextClasses());
	}

	GenerateContextClassToContextMap(ContextClasses);
	DistributeContextsToNodes();
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

TArray<UComposableCameraPoseContextBase*> AComposableCameraCameraBase::GetAllContexts() const
{
	TArray<UComposableCameraPoseContextBase*> Contexts;
	ContextClassToContextMap.GenerateValueArray(Contexts);
	return Contexts;
}

TArray<TSubclassOf<UComposableCameraPoseContextBase>> AComposableCameraCameraBase::GetAllContextClasses() const
{
	TArray<TSubclassOf<UComposableCameraPoseContextBase>> ContextClasses;
	ContextClassToContextMap.GenerateKeyArray(ContextClasses);
	return ContextClasses;
}

UComposableCameraPoseContextBase* AComposableCameraCameraBase::GetContextByClass(TSubclassOf<UComposableCameraPoseContextBase> ContextClass) const
{
	return *ContextClassToContextMap.Find(ContextClass);
}

bool AComposableCameraCameraBase::HasContextClass(const TSubclassOf<UComposableCameraPoseContextBase>& ContextClass) const
{
	return ContextClassToContextMap.Contains(ContextClass);
}

void AComposableCameraCameraBase::GenerateContextClassToContextMap(const TArray<TSubclassOf<UComposableCameraPoseContextBase>>& ContextClasses)
{
	for (auto ContextClass : ContextClasses)
	{
		if (!ContextClassToContextMap.Contains(ContextClass))
		{
			ContextClassToContextMap.Add(ContextClass, NewObject<UComposableCameraPoseContextBase>(this, ContextClass->GetClass()));
		}
	}
}

void AComposableCameraCameraBase::DistributeContextsToNodes()
{
	for (auto* Node : CameraNodes)
	{
		for (auto ContextClass : Node->GetRequiredContextClasses())
		{
			if (ContextClassToContextMap.Contains(ContextClass))
			{
				Node->AddContextClass(ContextClass, *ContextClassToContextMap.Find(ContextClass));
			}
		}
	}
}

