// Copyright Sulley. All rights reserved.

#include "Core/ComposableCameraDirector.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "Transitions/ComposableCameraTransitionBase.h"
#include "Core/ComposableCameraEvaluationTree.h"
#include "Core/ComposableCameraPlayerCamaraManager.h"
#include "DataAssets/ComposableCameraTransitionDataAsset.h"

UComposableCameraDirector::UComposableCameraDirector(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	EvaluationTree = CreateDefaultSubobject<UComposableCameraEvaluationTree>("ComposableCameraEvaluationTree");	
}

AComposableCameraCameraBase* UComposableCameraDirector::ResumeCamera(AComposableCameraCameraBase* ResumeCamera,
	UComposableCameraTransitionBase* Transition, const FTransform& Transform)
{
	ResumeCamera->bIsRunning = true;
	RunningCamera->bIsRunning = false;
	
	if (Transition && RunningCamera)
	{
		ForceCameraPoses(ResumeCamera, Transform);
		
		Transition = DuplicateObject(Transition, this);
		Transition->TransitionEnabled(RunningCamera, ResumeCamera, RunningCamera->GetCameraPose());
		Transition->ResetTransitionState();
		Transition->OnTransitionFinishesDelegate.AddLambda(
			[SourceCamera = RunningCamera]()
			{
				if (SourceCamera)
				{
					SourceCamera->ParentPendingCamera = nullptr;
					SourceCamera->Destroy();
				}
			}
		);
	}
		
	OnActivateNewCamera(ResumeCamera, Transition);

	return RunningCamera;
}

AComposableCameraCameraBase* UComposableCameraDirector::CreateNewCamera(
	AComposableCameraPlayerCamaraManager* PlayerCameraManager, TSubclassOf<AComposableCameraCameraBase> CameraClass,
	const FComposableCameraActivateParams& ActivationParams)
{
	bool bPreserveCameraPose = ActivationParams.bPreserveCameraPose;
	FTransform InitialTransform = ActivationParams.InitialTransform;
	UComposableCameraNodeInitializerDataAsset* NodeInitializerDataAsset = ActivationParams.NodeInitializerDataAsset; 
	bool bIsTransient = ActivationParams.bIsTransient;
	float LifeTime = ActivationParams.LifeTime;

	if (bPreserveCameraPose && RunningCamera)
	{
		InitialTransform.SetLocation(RunningCamera->GetOwningPlayerCameraManager()->GetCameraLocation());
		InitialTransform.SetRotation(RunningCamera->GetOwningPlayerCameraManager()->GetCameraRotation().Quaternion());
	}
	
	if (UWorld* World = GetWorld())
	{
		AComposableCameraCameraBase* NewCamera = World->SpawnActorDeferred<AComposableCameraCameraBase>(CameraClass, InitialTransform);
		NewCamera->bIsRunning = true;
		
		if (bIsTransient)
		{
			NewCamera->bIsTransient = true;
			NewCamera->LifeTime = LifeTime;
			NewCamera->RemainingLifeTime = LifeTime;
		}
		else
		{
			NewCamera->bIsTransient = false;
			NewCamera->LifeTime = -1.f;
			NewCamera->RemainingLifeTime = -1.f;
		}
		
		ForceCameraPoses(NewCamera, InitialTransform);
		
		NewCamera->Initialize(PlayerCameraManager, NodeInitializerDataAsset);
		NewCamera->FinishSpawning(InitialTransform);

		return NewCamera;
	}

	return nullptr;
}

AComposableCameraCameraBase* UComposableCameraDirector::ActivateNewCamera(
AComposableCameraPlayerCamaraManager* PlayerCameraManager,
	TSubclassOf<AComposableCameraCameraBase> CameraClass,
	UComposableCameraTransitionDataAsset* TransitionDataAsset,
	const FComposableCameraActivateParams& ActivationParams,
	FOnCameraFinishConstructed OnPreBeginplayEvent)
{
	bool bPreserveCameraPose = ActivationParams.bPreserveCameraPose;
	FTransform InitialTransform = ActivationParams.InitialTransform;
	UComposableCameraNodeInitializerDataAsset* NodeInitializerDataAsset = ActivationParams.NodeInitializerDataAsset; 
	bool bIsTransient = ActivationParams.bIsTransient;
	float LifeTime = ActivationParams.LifeTime;

	if (bPreserveCameraPose && RunningCamera)
	{
		InitialTransform.SetLocation(RunningCamera->GetOwningPlayerCameraManager()->GetCameraLocation());
		InitialTransform.SetRotation(RunningCamera->GetOwningPlayerCameraManager()->GetCameraRotation().Quaternion());
	}
	
	if (UWorld* World = GetWorld())
	{
		AComposableCameraCameraBase* NewCamera = World->SpawnActorDeferred<AComposableCameraCameraBase>(CameraClass, InitialTransform);
		NewCamera->ParentPendingCamera = RunningCamera;
		NewCamera->bIsRunning = true;
		if (RunningCamera)
		{
			RunningCamera->bIsRunning = false;
		}
		
		if (bIsTransient)
		{
			NewCamera->bIsTransient = true;
			NewCamera->LifeTime = LifeTime;
			NewCamera->RemainingLifeTime = LifeTime;
		}
		else
		{
			NewCamera->bIsTransient = false;
			NewCamera->LifeTime = -1.f;
			NewCamera->RemainingLifeTime = -1.f;
		}
		
		ForceCameraPoses(NewCamera, InitialTransform);
		
		NewCamera->Initialize(PlayerCameraManager, NodeInitializerDataAsset);
		OnPreBeginplayEvent.ExecuteIfBound(NewCamera);
		NewCamera->FinishSpawning(InitialTransform);

		UComposableCameraTransitionBase* Transition = nullptr;
		if (TransitionDataAsset && TransitionDataAsset->Transition && RunningCamera)
		{
			Transition = DuplicateObject(TransitionDataAsset->Transition, this);
			Transition->TransitionEnabled(RunningCamera, NewCamera, RunningCamera->GetCameraPose());
			Transition->ResetTransitionState();
		}
		
		OnActivateNewCamera(NewCamera, Transition);
	}

	return RunningCamera;
}

FComposableCameraPose UComposableCameraDirector::Evaluate(float DeltaTime) const
{
	return EvaluationTree->Evaluate(DeltaTime);
}

void UComposableCameraDirector::ForceCameraPoses(AComposableCameraCameraBase* Camera, const FTransform& Transform)
{
	if (Camera)
	{
		Camera->CameraPose.Position = Transform.GetLocation();
		Camera->CameraPose.Rotation = Transform.GetRotation().Rotator();
		Camera->LastFrameCameraPose.Position = Transform.GetLocation();
		Camera->LastFrameCameraPose.Rotation = Transform.GetRotation().Rotator();
	}
}

void UComposableCameraDirector::OnActivateNewCamera(AComposableCameraCameraBase* NewCamera, UComposableCameraTransitionBase* Transition)
{
	EvaluationTree->OnActivateNewCamera(NewCamera, Transition);
	RunningCamera = NewCamera;
}
