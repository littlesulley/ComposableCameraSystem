// Copyright Sulley. All rights reserved.

#include "Core/ComposableCameraPlayerCamaraManager.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "ComposableCameraSystemModule.h"
#include "Core/ComposableCameraDirector.h"

AComposableCameraPlayerCamaraManager::AComposableCameraPlayerCamaraManager(const  FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Director = CreateDefaultSubobject<UComposableCameraDirector>(TEXT("Director"));
}

void AComposableCameraPlayerCamaraManager::BeginPlay()
{
	Super::BeginPlay();
}

void AComposableCameraPlayerCamaraManager::InitializeFor(APlayerController* PlayerController)
{
	Super::InitializeFor(PlayerController);
}

void AComposableCameraPlayerCamaraManager::SetViewTarget(AActor* NewViewTarget,
	FViewTargetTransitionParams TransitionParams)
{
	Super::SetViewTarget(NewViewTarget, TransitionParams);
}

void AComposableCameraPlayerCamaraManager::ProcessViewRotation(float DeltaTime, FRotator& OutViewRotation,
	FRotator& OutDeltaRot)
{
	Super::ProcessViewRotation(DeltaTime, OutViewRotation, OutDeltaRot);
}

AComposableCameraCameraBase* AComposableCameraPlayerCamaraManager::ActivateNewCamera(
	TSubclassOf<AComposableCameraCameraBase> CameraClass, UDataTable* NodeInitializerDataTable,
	FGameplayTagContainer NodeInitializerTags, bool bIsTransient, float LifeTime)
{
	if (!IsValid(CameraClass))
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT(
			"Camera class %s is not valid."),
			*CameraClass->StaticClass()->GetName());
	}
	
	AComposableCameraCameraBase* NewCamera = Director->ActivateNewCamera(
		CameraClass, NodeInitializerDataTable, NodeInitializerTags, bIsTransient, LifeTime);
	if (NewCamera)
	{
		RunningCamera = NewCamera;
	}
	else
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT(
			"Activating new camera of class %s failed, returning the currently running camera."),
			*CameraClass->StaticClass()->GetName());
	}

	return RunningCamera;
}

void AComposableCameraPlayerCamaraManager::DoUpdateCamera(float DeltaTime)
{
	Super::DoUpdateCamera(DeltaTime);
}

