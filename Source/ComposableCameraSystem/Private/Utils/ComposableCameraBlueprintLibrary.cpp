// Copyright Sulley. All rights reserved.

#include "Utils/ComposableCameraBlueprintLibrary.h"

#include "Cameras/ComposableCameraCameraBase.h"
#include "ComposableCameraSystemModule.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "DataAssets/ComposableCameraTransitionDataAsset.h"
#include "Kismet/GameplayStatics.h"

AComposableCameraCameraBase* UComposableCameraBlueprintLibrary::CreateComposableCameraByClass(
	const UObject* WorldContextObject,
	AComposableCameraPlayerCameraManager* PlayerCameraManager,
	TSubclassOf<AComposableCameraCameraBase> CameraClass,
	FComposableCameraActivateParams ActivationParams)
{
	if (PlayerCameraManager)
	{
		AComposableCameraCameraBase* NewCamera = PlayerCameraManager->CreateNewCamera(CameraClass, ActivationParams);
		
		return NewCamera; 
	}

	return nullptr;
}

AComposableCameraCameraBase* UComposableCameraBlueprintLibrary::ActivateComposableCameraByClass(
	const UObject* WorldContextObject,
	AComposableCameraPlayerCameraManager* PlayerCameraManager,
	TSubclassOf<AComposableCameraCameraBase> CameraClass,
	FName ContextName,
	UComposableCameraTransitionDataAsset* TransitionDataAsset,
	FComposableCameraActivateParams ActivationParams,
	bool bNewInstance,
	FOnCameraFinishConstructed OnPreBeginplayEvent)
{
	if (PlayerCameraManager)
	{
		auto RunningCamera = PlayerCameraManager->GetRunningCamera();
		
		// Return the current running camera, if (1) class is matching, (2) current running camera is not transient,
		// (3) incoming camera is not transient, and (4) not spawning a new instance.
		if (RunningCamera &&
			RunningCamera->StaticClass() == CameraClass->StaticClass() &&
			!RunningCamera->IsTransient() &&
			!ActivationParams.bIsTransient &&
			!bNewInstance)
		{
			return RunningCamera;
		}

		AComposableCameraCameraBase* NewCamera = PlayerCameraManager->ActivateNewCamera(
			CameraClass,
			TransitionDataAsset,
			ActivationParams,
			OnPreBeginplayEvent,
			ContextName);

		return NewCamera;
	}

	return nullptr;
}

void UComposableCameraBlueprintLibrary::TerminateCurrentCamera(
	const UObject* WorldContextObject,
	AComposableCameraPlayerCameraManager* PlayerCameraManager,
	UComposableCameraTransitionDataAsset* TransitionOverride,
	FComposableCameraActivateParams ActivationParams)
{
	if (PlayerCameraManager)
	{
		PlayerCameraManager->TerminateCurrentCamera(TransitionOverride, ActivationParams);
	}
}

void UComposableCameraBlueprintLibrary::PopCameraContext(
	const UObject* WorldContextObject,
	AComposableCameraPlayerCameraManager* PlayerCameraManager,
	FName ContextName,
	UComposableCameraTransitionDataAsset* TransitionOverride,
	FComposableCameraActivateParams ActivationParams)
{
	if (PlayerCameraManager)
	{
		PlayerCameraManager->PopCameraContext(ContextName, TransitionOverride, ActivationParams);
	}
}

int32 UComposableCameraBlueprintLibrary::GetCameraContextStackDepth(
	const UObject* WorldContextObject,
	AComposableCameraPlayerCameraManager* PlayerCameraManager)
{
	if (PlayerCameraManager)
	{
		return PlayerCameraManager->GetContextStackDepth();
	}
	return 0;
}

FName UComposableCameraBlueprintLibrary::GetActiveContextName(
	const UObject* WorldContextObject,
	AComposableCameraPlayerCameraManager* PlayerCameraManager)
{
	if (PlayerCameraManager)
	{
		return PlayerCameraManager->GetActiveContextName();
	}
	return NAME_None;
}

void UComposableCameraBlueprintLibrary::AddModifier(const UObject* WorldContextObject,
	AComposableCameraPlayerCameraManager* PlayerCameraManager, UComposableCameraNodeModifierDataAsset* ModifierAsset)
{
	if (PlayerCameraManager)
	{
		PlayerCameraManager->AddModifier(ModifierAsset);
	}
}

void UComposableCameraBlueprintLibrary::RemoveModifier(const UObject* WorldContextObject,
	AComposableCameraPlayerCameraManager* PlayerCameraManager, UComposableCameraNodeModifierDataAsset* ModifierAsset)
{
	if (PlayerCameraManager)
	{
		PlayerCameraManager->RemoveModifier(ModifierAsset);
	}
}

UComposableCameraActionBase* UComposableCameraBlueprintLibrary::AddAction(const UObject* WorldContextObject,
	AComposableCameraPlayerCameraManager* PlayerCameraManager, TSubclassOf<UComposableCameraActionBase> ActionClass, bool bOnlyForCurrentCamera)
{
	if (PlayerCameraManager)
	{
		return PlayerCameraManager->AddCameraAction(ActionClass, bOnlyForCurrentCamera);
	}

	return nullptr;
}

void UComposableCameraBlueprintLibrary::ExpireAction(const UObject* WorldContextObject,
	AComposableCameraPlayerCameraManager* PlayerCameraManager, TSubclassOf<UComposableCameraActionBase> ActionClass)
{
	if (PlayerCameraManager)
	{
		PlayerCameraManager->ExpireCameraAction(ActionClass);
	}
}

AComposableCameraPlayerCameraManager* UComposableCameraBlueprintLibrary::GetComposableCameraPlayerCameraManager(
	const UObject* WorldContextObject, int Index)
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(WorldContextObject, Index);
	return Cast<AComposableCameraPlayerCameraManager>(PC->PlayerCameraManager);
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
