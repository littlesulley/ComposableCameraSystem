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
	bool bNewInstance,
	FOnCameraFinishConstructed OnPreBeginplayEvent)
{
	FTransform InitialTransform = ActivationParams.InitialTransform;
	UComposableCameraNodeInitializerDataAsset* NodeInitializerDataAsset = ActivationParams.NodeInitializerDataTable;
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
			PlayerCameraManager, CameraClass, TransitionParams, InitialTransform, NodeInitializerDataAsset, bIsTransient, LifeTime, OnPreBeginplayEvent);
		
		return NewCamera; 
	}

	return nullptr;
}

void UComposableCameraBlueprintLibrary::TerminateCurrentCamera(const UObject* WorldContextObject, AComposableCameraPlayerCamaraManager* PlayerCameraManager,
		FComposableCameraTransitionParams TransitionParams, bool bPreserveCameraPose)
{
	if (!PlayerCameraManager || !PlayerCameraManager->RunningCamera)
	{
		return;
	}

	AComposableCameraCameraBase* CurrentCamera = PlayerCameraManager->RunningCamera;
	AComposableCameraCameraBase* ResumeCamera = CurrentCamera->ParentPendingCamera;
	
	while (ResumeCamera)
	{
		if (ResumeCamera->IsFinished())
		{
			AComposableCameraCameraBase* PendingKillCamera = ResumeCamera;
			CurrentCamera->ParentPendingCamera = ResumeCamera->ParentPendingCamera;
			ResumeCamera = ResumeCamera->ParentPendingCamera;
			PendingKillCamera->ParentPendingCamera = nullptr;
			PendingKillCamera->Destroy();
		}
		else
		{
			break;
		}
	}
	
	if (!ResumeCamera)
	{
		return;
	}
	
	FComposableCameraTransitionParams TransitionParameters = TransitionParams;
	if (!TransitionParams.TransitionClass)
	{
		TransitionParameters.TransitionClass = ResumeCamera->DefaultTransition ? ResumeCamera->DefaultTransition->StaticClass() : nullptr;
		TransitionParameters.TransitionTime = ResumeCamera->DefaultTransition ? ResumeCamera->DefaultTransitionTime : 0.f;
	}

	PlayerCameraManager->ResumeCamera(ResumeCamera, TransitionParameters, bPreserveCameraPose);
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
