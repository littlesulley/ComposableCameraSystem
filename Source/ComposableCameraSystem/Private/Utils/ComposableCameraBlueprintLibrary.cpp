// Copyright Sulley. All rights reserved.

#include "Utils/ComposableCameraBlueprintLibrary.h"

#include "Cameras/ComposableCameraCameraBase.h"
#include "Core/ComposableCameraPlayerCamaraManager.h"
#include "DataAssets/ComposableCameraTransitionDataAsset.h"
#include "Kismet/GameplayStatics.h"

AComposableCameraCameraBase* UComposableCameraBlueprintLibrary::ActivateComposableCameraByClass(
	const UObject* WorldContextObject,
	AComposableCameraPlayerCamaraManager* PlayerCameraManager,
	TSubclassOf<AComposableCameraCameraBase> CameraClass,
	UComposableCameraTransitionDataAsset* TransitionDataAsset,
	FComposableCameraActivateParams ActivationParams,
	bool bNewInstance,
	FOnCameraFinishConstructed OnPreBeginplayEvent)
{
	if (PlayerCameraManager)
	{
		// Return the current running camera, if (1) class not matching, (2) current running camera is not transient,
		// (3) incoming camera is not transient, and (4) not spawning a new instance.
		if (PlayerCameraManager->GetRunningCamera()->StaticClass() == CameraClass->StaticClass() &&
			!PlayerCameraManager->GetRunningCamera()->IsTransient() &&
			!ActivationParams.bIsTransient &&
			!bNewInstance)
		{
			return PlayerCameraManager->GetRunningCamera();
		}

		AComposableCameraCameraBase* NewCamera = PlayerCameraManager->ActivateNewCamera(
			PlayerCameraManager, CameraClass, TransitionDataAsset, ActivationParams, OnPreBeginplayEvent);
		
		return NewCamera; 
	}

	return nullptr;
}

void UComposableCameraBlueprintLibrary::TerminateCurrentCamera(const UObject* WorldContextObject, AComposableCameraPlayerCamaraManager* PlayerCameraManager,
		UComposableCameraTransitionDataAsset* TransitionDataAsset, bool bPreserveCameraPose)
{
	if (!PlayerCameraManager || !PlayerCameraManager->RunningCamera || !PlayerCameraManager->RunningCamera->ParentPendingCamera)
	{
		return;
	}

	AComposableCameraCameraBase* ResumeCamera = PlayerCameraManager->RunningCamera->ParentPendingCamera.Get();

	UComposableCameraTransitionBase* Transition = nullptr;
	if (!TransitionDataAsset || !TransitionDataAsset->Transition)
	{
		Transition = ResumeCamera->DefaultTransition ? DuplicateObject(ResumeCamera->DefaultTransition, PlayerCameraManager) : nullptr;
	}
	else
	{
		Transition = DuplicateObject(TransitionDataAsset->Transition, PlayerCameraManager);
	}

	PlayerCameraManager->ResumeCamera(ResumeCamera, Transition, bPreserveCameraPose);
}

void UComposableCameraBlueprintLibrary::AddModifier(const UObject* WorldContextObject,
	AComposableCameraPlayerCamaraManager* PlayerCameraManager, UComposableCameraNodeModifierDataAsset* ModifierAsset)
{
	if (PlayerCameraManager)
	{
		PlayerCameraManager->AddModifier(ModifierAsset);
	}
}

void UComposableCameraBlueprintLibrary::RemoveModifier(const UObject* WorldContextObject,
	AComposableCameraPlayerCamaraManager* PlayerCameraManager, UComposableCameraNodeModifierDataAsset* ModifierAsset)
{
	if (PlayerCameraManager)
	{
		PlayerCameraManager->RemoveModifier(ModifierAsset);
	}
}

AComposableCameraPlayerCamaraManager* UComposableCameraBlueprintLibrary::GetComposableCameraPlayerCameraManager(
	const UObject* WorldContextObject, int Index)
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(WorldContextObject, Index);
	return Cast<AComposableCameraPlayerCamaraManager>(PC->PlayerCameraManager);
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
