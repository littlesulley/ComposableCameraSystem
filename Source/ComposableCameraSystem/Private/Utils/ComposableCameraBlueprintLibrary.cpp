// Copyright Sulley. All rights reserved.

#include "Utils/ComposableCameraBlueprintLibrary.h"

#include "Cameras/ComposableCameraCameraBase.h"
#include "Core/ComposableCameraPlayerCamaraManager.h"
#include "Kismet/GameplayStatics.h"

AComposableCameraCameraBase* UComposableCameraBlueprintLibrary::ActivateComposableCameraByClass(
	const UObject* WorldContextObject,
	AComposableCameraPlayerCamaraManager* PlayerCameraManager,
	TSubclassOf<AComposableCameraCameraBase> CameraClass,
	FComposableCameraTransitionParams TransitionParams,
	FComposableCameraActivateParams ActivationParams,
	bool bNewInstance)
{
	UDataTable* NodeInitializerDataTable = ActivationParams.NodeInitializerDataTable;
	FGameplayTagContainer NodeInitializerTags = ActivationParams.NodeInitializerTags;
	bool bIsTransient = ActivationParams.bIsTransient;
	float LifeTime = ActivationParams.LifeTime;
	
	if (PlayerCameraManager)
	{
		// Return the current running camera, if (1) class not matching, (2) current running camera is not transient,
		// (3) incoming camera is not transient, and (4) not spawning a new instance.
		if (PlayerCameraManager->GetRunningCamera()->StaticClass() == CameraClass->StaticClass() &&
			!PlayerCameraManager->GetRunningCamera()->IsTransient() &&
			!bIsTransient &&
			!bNewInstance)
		{
			return PlayerCameraManager->GetRunningCamera();
		}

		AComposableCameraCameraBase* NewCamera = PlayerCameraManager->ActivateNewCamera(
			PlayerCameraManager, CameraClass, TransitionParams, NodeInitializerDataTable, NodeInitializerTags, bIsTransient, LifeTime);
		
		return NewCamera; 
	}

	return nullptr;
}

FVector UComposableCameraBlueprintLibrary::MakeLiteralVector(FVector Value)
{
	return Value;
}

FVector4 UComposableCameraBlueprintLibrary::MakeLiteralVector4(FVector4 Value)
{
	return Value;
}

FVector2D UComposableCameraBlueprintLibrary::MakeLiteralVector2D(FVector2D Value)
{
	return Value;
}

FRotator UComposableCameraBlueprintLibrary::MakeLiteralRotator(FRotator Value)
{
	return Value;
}

FTransform UComposableCameraBlueprintLibrary::MakeLiteralTransform(FTransform Value)
{
	return Value;
}
