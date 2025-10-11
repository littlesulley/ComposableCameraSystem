// Copyright Sulley. All rights reserved.

#include "Core/ComposableCameraPlayerCamaraManager.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "ComposableCameraSystemModule.h"
#include "Camera/CameraComponent.h"
#include "Core/ComposableCameraDirector.h"
#include "Core/ComposableCameraModifierManager.h"
#include "DataAssets/ComposableCameraTransitionDataAsset.h"
#include "Transitions/ComposableCameraTransitionBase.h"
#include "Utils/ComposableCameraProjectSettings.h"

class UComposableCameraTransitionBase;

AComposableCameraPlayerCamaraManager::AComposableCameraPlayerCamaraManager(const  FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Director = CreateDefaultSubobject<UComposableCameraDirector>(TEXT("Director"));
	ModifierManager = CreateDefaultSubobject<UComposableCameraModifierManager>(TEXT("ModifierManager"));
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

AComposableCameraCameraBase* AComposableCameraPlayerCamaraManager::CreateNewCamera(
	AComposableCameraPlayerCamaraManager* PlayerCameraManager, TSubclassOf<AComposableCameraCameraBase> CameraClass,
	const FComposableCameraActivateParams& ActivationParams)
{
	if (CameraClass == nullptr)
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT(
			"Camera class is null."));
		return nullptr;
	}
	if (!IsValid(CameraClass))
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT(
			"Camera class %s is not valid."),
			*CameraClass->StaticClass()->GetName());
		return nullptr;
	}
	
	AComposableCameraCameraBase* NewCamera = Director->CreateNewCamera(
		PlayerCameraManager, CameraClass, ActivationParams);
	
	return NewCamera;
}

AComposableCameraCameraBase* AComposableCameraPlayerCamaraManager::ActivateNewCamera(
	AComposableCameraPlayerCamaraManager* PlayerCameraManager,
	TSubclassOf<AComposableCameraCameraBase> CameraClass,
	UComposableCameraTransitionDataAsset* Transition,
	const FComposableCameraActivateParams& ActivationParams,
	FOnCameraFinishConstructed OnPreBeginplayEvent)
{
	if (CameraClass == nullptr)
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT(
			"Camera class is null."));
		return RunningCamera;
	}
	if (!IsValid(CameraClass))
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT(
			"Camera class %s is not valid."),
			*CameraClass->StaticClass()->GetName());
		return RunningCamera;
	}
	
	if (!Transition || !Transition->Transition)
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT(
			"Transition is not valid. Will use camera cut."));
	}
	
	AComposableCameraCameraBase* NewCamera = Director->ActivateNewCamera(
		PlayerCameraManager, CameraClass, Transition, ActivationParams, OnPreBeginplayEvent);
	if (NewCamera)
	{
		RunningCamera = NewCamera;
		RefreshCameraChain();
	}
	else
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT(
			"Activating new camera of class %s failed, returning the currently running camera."),
			*CameraClass->StaticClass()->GetName());
	}

	return RunningCamera;
}

void AComposableCameraPlayerCamaraManager::AddModifier(UComposableCameraNodeModifierDataAsset* ModifierAsset)
{
}

void AComposableCameraPlayerCamaraManager::RemoveModifier(UComposableCameraNodeModifierDataAsset* ModifierAsset)
{
}

void AComposableCameraPlayerCamaraManager::ResumeCamera(AComposableCameraCameraBase* ResumeCamera,
	UComposableCameraTransitionBase* Transition, bool bPreserveCameraPose)
{
	FTransform InitialTransform {};
	if (bPreserveCameraPose)
	{
		InitialTransform.SetLocation(GetCameraLocation());
		InitialTransform.SetRotation(GetCameraRotation().Quaternion());
	}
	else
	{
		InitialTransform.SetLocation(ResumeCamera->CameraPose.Position);
		InitialTransform.SetRotation(ResumeCamera->CameraPose.Rotation.Quaternion());
	}
	
	RunningCamera = Director->ResumeCamera(ResumeCamera, Transition, InitialTransform);
}

FMinimalViewInfo AComposableCameraPlayerCamaraManager::GetCameraViewFromCameraPose(const FComposableCameraPose& OutPose) const
{
	FMinimalViewInfo DesiredView = GetCameraCacheView();

	DesiredView.Location = OutPose.Position;
	DesiredView.Rotation = OutPose.Rotation;
	DesiredView.FOV = OutPose.FieldOfView;
	DesiredView.DesiredFOV = OutPose.FieldOfView;

	if (RunningCamera)
	{
		UCameraComponent* CameraComponent = RunningCamera->GetCameraComponent();
		DesiredView.AspectRatio = CameraComponent->AspectRatio;
		DesiredView.bConstrainAspectRatio = CameraComponent->bConstrainAspectRatio;
		DesiredView.AspectRatioAxisConstraint = CameraComponent->AspectRatioAxisConstraint;
		DesiredView.ProjectionMode = CameraComponent->ProjectionMode;
		DesiredView.OrthoWidth = CameraComponent->OrthoWidth;
		DesiredView.OrthoNearClipPlane = CameraComponent->OrthoNearClipPlane;
		DesiredView.OrthoFarClipPlane = CameraComponent->OrthoFarClipPlane;
		DesiredView.PostProcessSettings = CameraComponent->PostProcessSettings;
		DesiredView.PostProcessBlendWeight = CameraComponent->PostProcessBlendWeight;
	}
	
	return DesiredView;
}

void AComposableCameraPlayerCamaraManager::DoUpdateCamera(float DeltaTime)
{
	Super::DoUpdateCamera(DeltaTime);

	// Must call FillCameraCache, since the call to Super::DoUpdateCamera will override the true camera view.
	FillCameraCache(LastDesiredView);
	
	FComposableCameraPose OutPose = Director->Evaluate(DeltaTime);
	FMinimalViewInfo DesiredView = GetCameraViewFromCameraPose(OutPose);
	CurrentCameraPose = OutPose;

	if (RunningCamera)
	{
		RunningCamera->SetActorLocation(DesiredView.Location);
		RunningCamera->SetActorRotation(DesiredView.Rotation);
		RunningCamera->GetCameraComponent()->FieldOfView = DesiredView.FOV;
	}

	if (bSyncToControlRotation)
	{
		GetOwningPlayerController()->SetControlRotation(DesiredView.Rotation);
	}

	LastDesiredView = DesiredView;
	FillCameraCache(DesiredView);
}

void AComposableCameraPlayerCamaraManager::RefreshCameraChain() const
{
	const UComposableCameraProjectSettings* Settings = GetDefault<UComposableCameraProjectSettings>();
	const int32 MaxCameraChainLength = Settings->MaxCameraChainCleanupDepth;

	int CurrentCameraChainLength = 0;
	AComposableCameraCameraBase* CurrentCamera = RunningCamera;

	while (CurrentCamera->ParentPendingCamera && CurrentCameraChainLength + 1 < MaxCameraChainLength)
	{
		++CurrentCameraChainLength;
		CurrentCamera = CurrentCamera->ParentPendingCamera.Get();
	}

	// Recursively destroy camera above the position.
	CurrentCamera = CurrentCamera->ParentPendingCamera.Get();
	while (CurrentCamera)
	{
		AComposableCameraCameraBase* ParentCamera = CurrentCamera->ParentPendingCamera.Get();
		CurrentCamera->ParentPendingCamera = nullptr;
		CurrentCamera->Destroy();
		CurrentCamera = ParentCamera;
	}
}

