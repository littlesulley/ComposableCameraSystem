// Copyright Sulley. All rights reserved.

#include "Core/ComposableCameraDirector.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "GameplayTagContainer.h"

UComposableCameraDirector::UComposableCameraDirector(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	
}

AComposableCameraCameraBase* UComposableCameraDirector::ActivateNewCamera(
	TSubclassOf<AComposableCameraCameraBase> CameraClass, UDataTable* NodeInitializerDataTable,
	FGameplayTagContainer NodeInitializerTags, bool bIsTransient, float LifeTime)
{
	if (UWorld* World = GetWorld())
	{
		AComposableCameraCameraBase* NewCamera;

		if (bIsTransient)
		{
			NewCamera = NewObject<AComposableCameraCameraBase>(RunningCamera, CameraClass);
			NewCamera->ParentPendingCamera = RunningCamera;
			NewCamera->bIsTransient = true;
			NewCamera->LifeTime = LifeTime;
			NewCamera->RemainingLifeTime = LifeTime;
		}
		else
		{
			NewCamera = NewObject<AComposableCameraCameraBase>(this, CameraClass);
			NewCamera->ParentPendingCamera = nullptr;
			NewCamera->bIsTransient = false;
			NewCamera->LifeTime = -1.f;
			NewCamera->RemainingLifeTime = -1.f;
		}

		RunningCamera = NewCamera;
	}

	return RunningCamera;
}
