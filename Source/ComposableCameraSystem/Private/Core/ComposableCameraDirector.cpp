// Copyright Sulley. All rights reserved.

#include "Core/ComposableCameraDirector.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "GameplayTagContainer.h"
#include "Transitions/ComposableCameraTransitionBase.h"
#include "Core/ComposableCameraEvaluationTree.h"
#include "DataAssets/ComposableCameraTransitionDataAsset.h"
#include "Kismet/GameplayStatics.h"

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

AComposableCameraCameraBase* UComposableCameraDirector::ActivateNewCamera(
AComposableCameraPlayerCamaraManager* PlayerCameraManager,
	TSubclassOf<AComposableCameraCameraBase> CameraClass,
	UComposableCameraTransitionDataAsset* TransitionDataAsset,
	FTransform InitialTransform,
	UComposableCameraNodeInitializerDataAsset* NodeInitializerDataAsset, 
	bool bIsTransient,
	float LifeTime,
	FOnCameraFinishConstructed OnPreBeginplayEvent)
{
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

void UComposableCameraDirector::OnActivateNewCamera(AComposableCameraCameraBase* NewCamera, UComposableCameraTransitionBase* Transition)
{
	EvaluationTree->OnActivateNewCamera(NewCamera, Transition);
	RunningCamera = NewCamera;
}
