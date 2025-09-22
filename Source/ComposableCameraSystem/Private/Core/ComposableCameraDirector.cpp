// Copyright Sulley. All rights reserved.

#include "Core/ComposableCameraDirector.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "GameplayTagContainer.h"
#include "Transitions/ComposableCameraTransitionBase.h"
#include "Core/ComposableCameraEvaluationTree.h"
#include "Kismet/GameplayStatics.h"

UComposableCameraDirector::UComposableCameraDirector(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	EvaluationTree = CreateDefaultSubobject<UComposableCameraEvaluationTree>("ComposableCameraEvaluationTree");	
}

AComposableCameraCameraBase* UComposableCameraDirector::ActivateNewCamera(
AComposableCameraPlayerCamaraManager* PlayerCameraManager,
	TSubclassOf<AComposableCameraCameraBase> CameraClass,
	FComposableCameraTransitionParams TransitionParams,
	UDataTable* NodeInitializerDataTable, // @TODO
	FGameplayTagContainer NodeInitializerTags, // @TODO
	bool bIsTransient,
	float LifeTime,
	FOnCameraFinishConstructed OnPreBeginplayEvent)
{
	if (UWorld* World = GetWorld())
	{
		AComposableCameraCameraBase* NewCamera;

		if (bIsTransient)
		{
			NewCamera = World->SpawnActorDeferred<AComposableCameraCameraBase>(CameraClass, FTransform{});
			NewCamera->ParentPendingCamera = RunningCamera;
			NewCamera->bIsTransient = true;
			NewCamera->LifeTime = LifeTime;
			NewCamera->RemainingLifeTime = LifeTime;
			NewCamera->bIsRunning = true;
			RunningCamera->bIsRunning = false;
			NewCamera->Initialize(PlayerCameraManager);
			OnPreBeginplayEvent.ExecuteIfBound(NewCamera);
			NewCamera->FinishSpawning(FTransform{});
		}
		else
		{
			NewCamera = World->SpawnActorDeferred<AComposableCameraCameraBase>(CameraClass, FTransform{});
			NewCamera->ParentPendingCamera = nullptr;
			NewCamera->bIsTransient = false;
			NewCamera->LifeTime = -1.f;
			NewCamera->RemainingLifeTime = -1.f;
			NewCamera->bIsRunning = true;
			NewCamera->Initialize(PlayerCameraManager);
			OnPreBeginplayEvent.ExecuteIfBound(NewCamera);
			NewCamera->FinishSpawning(FTransform{});
		}

		UComposableCameraTransitionBase* Transition = nullptr;
		if (TransitionParams.TransitionClass)
		{
			Transition = NewObject<UComposableCameraTransitionBase>(this, TransitionParams.TransitionClass);
			Transition->TransitionEnabled(CurrentCameraPose, TransitionParams.TransitionTime);
		}
		
		OnActivateNewCamera(NewCamera, Transition);
	}

	return RunningCamera;
}

FComposableCameraPose UComposableCameraDirector::Evaluate(float DeltaTime)
{
	CurrentCameraPose = EvaluationTree->Evaluate(DeltaTime);
	return CurrentCameraPose;
}

void UComposableCameraDirector::OnActivateNewCamera(AComposableCameraCameraBase* NewCamera, UComposableCameraTransitionBase* Transition)
{
	EvaluationTree->OnActivateNewCamera(NewCamera, Transition);
	RunningCamera = NewCamera;
}
