// Copyright Sulley. All rights reserved.

#include "ComposableCameraSystemModule.h"

#include "Core/ComposableCameraDirector.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "Transitions/ComposableCameraTransitionBase.h"
#include "Core/ComposableCameraEvaluationTree.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "DataAssets/ComposableCameraTransitionDataAsset.h"

UComposableCameraDirector::UComposableCameraDirector(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// CreateDefaultSubobject works when called from the owning actor's constructor.
	// For dynamically created Directors (e.g., from context stack PushContext),
	// PostInitProperties handles initialization.
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		EvaluationTree = NewObject<UComposableCameraEvaluationTree>(this, TEXT("ComposableCameraEvaluationTree"));
	}
}

AComposableCameraCameraBase* UComposableCameraDirector::ResumeCamera(AComposableCameraCameraBase* InResumeCamera,
	UComposableCameraTransitionBase* Transition, const FTransform& Transform)
{
	if (Transition && RunningCamera)
	{
		ForceCameraPoses(InResumeCamera, Transform);

		Transition = DuplicateObject(Transition, this);

		FComposableCameraTransitionInitParams TransInitParams;
		TransInitParams.CurrentSourcePose = LastEvaluatedPose;
		TransInitParams.PreviousSourcePose = PreviousEvaluatedPose;
		TransInitParams.DeltaTime = GetWorld()->GetDeltaSeconds();
		Transition->TransitionEnabled(TransInitParams);
		Transition->ResetTransitionState();
	}

	EvaluationTree->OnActivateNewCamera(InResumeCamera, Transition);
	RunningCamera = InResumeCamera;

	return RunningCamera;
}

AComposableCameraCameraBase* UComposableCameraDirector::CreateNewCamera(
	AComposableCameraPlayerCameraManager* PlayerCameraManager, TSubclassOf<AComposableCameraCameraBase> CameraClass,
	const FComposableCameraActivateParams& ActivationParams)
{
	bool bPreserveCameraPose = ActivationParams.bPreserveCameraPose;
	FTransform InitialTransform = ActivationParams.InitialTransform;
	bool bUseInitialTransformRotation = ActivationParams.bUseInitialTransformRotation;
	UComposableCameraNodeInitializerDataAsset* NodeInitializerDataAsset = ActivationParams.NodeInitializerDataAsset; 
	bool bIsTransient = ActivationParams.bIsTransient;
	float LifeTime = ActivationParams.LifeTime;

	if (bPreserveCameraPose && RunningCamera)
	{
		InitialTransform.SetLocation(RunningCamera->GetOwningPlayerCameraManager()->GetCameraLocation());
		InitialTransform.SetRotation(RunningCamera->GetOwningPlayerCameraManager()->GetCameraRotation().Quaternion());
	}

	if (bUseInitialTransformRotation)
	{
		InitialTransform.SetRotation(ActivationParams.InitialTransform.GetRotation());
	}
	
	if (UWorld* World = GetWorld())
	{
		AComposableCameraCameraBase* NewCamera = World->SpawnActorDeferred<AComposableCameraCameraBase>(CameraClass, InitialTransform);
		
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
	AComposableCameraPlayerCameraManager* PlayerCameraManager,
	TSubclassOf<AComposableCameraCameraBase> CameraClass,
	UComposableCameraTransitionDataAsset* TransitionDataAsset,
	const FComposableCameraActivateParams& ActivationParams,
	FOnCameraFinishConstructed OnPreBeginplayEvent)
{
	bool bPreserveCameraPose = ActivationParams.bPreserveCameraPose;
	FTransform InitialTransform = ActivationParams.InitialTransform;
	bool bUseInitialTransformRotation = ActivationParams.bUseInitialTransformRotation;
	UComposableCameraNodeInitializerDataAsset* NodeInitializerDataAsset = ActivationParams.NodeInitializerDataAsset; 
	bool bIsTransient = ActivationParams.bIsTransient;
	float LifeTime = ActivationParams.LifeTime;

	if (bPreserveCameraPose)
	{
		InitialTransform.SetLocation(PlayerCameraManager->GetCameraLocation());
		InitialTransform.SetRotation(PlayerCameraManager->GetCameraRotation().Quaternion());
	}

	if (bUseInitialTransformRotation)
	{
		InitialTransform.SetRotation(ActivationParams.InitialTransform.GetRotation());
	}
	
	if (UWorld* World = GetWorld())
	{
		AComposableCameraCameraBase* NewCamera = World->SpawnActorDeferred<AComposableCameraCameraBase>(CameraClass, InitialTransform);

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

		// Initialization order when creating a new camera:
		//    Construct (SpawnActorDeferred)
		// -> Initialize (Initialize, node initializers applied here)
		// -> Modifiers (ApplyModifiers, effective modifiers)
		// -> PreBeginPlay (OnPreBeginplayEvent, custom user logic)
		// -> BeginPlay (FinishSpawning).
		NewCamera->Initialize(PlayerCameraManager, NodeInitializerDataAsset);
		PlayerCameraManager->ApplyModifiers(NewCamera, true);
		OnPreBeginplayEvent.ExecuteIfBound(NewCamera);
		NewCamera->FinishSpawning(InitialTransform);

		UComposableCameraTransitionBase* Transition = nullptr;
		if (TransitionDataAsset && TransitionDataAsset->Transition && RunningCamera)
		{
			FComposableCameraTransitionInitParams TransInitParams;
			TransInitParams.CurrentSourcePose = LastEvaluatedPose;
			TransInitParams.PreviousSourcePose = PreviousEvaluatedPose;
			TransInitParams.DeltaTime = GetWorld()->GetDeltaSeconds();
			
			Transition = DuplicateObject(TransitionDataAsset->Transition, this);
			Transition->TransitionEnabled(TransInitParams);
			Transition->ResetTransitionState();
		}

		EvaluationTree->OnActivateNewCamera(NewCamera, Transition);
		RunningCamera = NewCamera;
	}

	return RunningCamera;
}

AComposableCameraCameraBase* UComposableCameraDirector::ActivateNewCameraWithReferenceSource(
	AComposableCameraPlayerCameraManager* PlayerCameraManager,
	TSubclassOf<AComposableCameraCameraBase> CameraClass,
	UComposableCameraTransitionDataAsset* TransitionDataAsset,
	const FComposableCameraActivateParams& ActivationParams,
	FOnCameraFinishConstructed OnPreBeginplayEvent,
	UComposableCameraDirector* SourceDirector,
	UComposableCameraTransitionBase** OutTransition)
{
	check(SourceDirector);

	bool bPreserveCameraPose = ActivationParams.bPreserveCameraPose;
	FTransform InitialTransform = ActivationParams.InitialTransform;
	bool bUseInitialTransformRotation = ActivationParams.bUseInitialTransformRotation;
	UComposableCameraNodeInitializerDataAsset* NodeInitializerDataAsset = ActivationParams.NodeInitializerDataAsset;
	bool bIsTransient = ActivationParams.bIsTransient;
	float LifeTime = ActivationParams.LifeTime;

	if (bPreserveCameraPose)
	{
		// Preserve from the source context's current camera pose via the PlayerCameraManager.
		InitialTransform.SetLocation(PlayerCameraManager->GetCameraLocation());
		InitialTransform.SetRotation(PlayerCameraManager->GetCameraRotation().Quaternion());
	}

	if (bUseInitialTransformRotation)
	{
		InitialTransform.SetRotation(ActivationParams.InitialTransform.GetRotation());
	}

	if (UWorld* World = GetWorld())
	{
		AComposableCameraCameraBase* NewCamera = World->SpawnActorDeferred<AComposableCameraCameraBase>(CameraClass, InitialTransform);

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
		PlayerCameraManager->ApplyModifiers(NewCamera, true);
		OnPreBeginplayEvent.ExecuteIfBound(NewCamera);
		NewCamera->FinishSpawning(InitialTransform);

		UComposableCameraTransitionBase* Transition = nullptr;
		if (TransitionDataAsset && TransitionDataAsset->Transition)
		{
			// Use the source Director's last evaluated (blended) pose as the transition source.
			// This is what the player was actually seeing before the context switch.
			FComposableCameraTransitionInitParams TransInitParams;
			TransInitParams.CurrentSourcePose = SourceDirector->GetLastEvaluatedPose();
			TransInitParams.PreviousSourcePose = SourceDirector->GetPreviousEvaluatedPose();
			TransInitParams.DeltaTime = GetWorld()->GetDeltaSeconds();
			
			Transition = DuplicateObject(TransitionDataAsset->Transition, this);
			Transition->TransitionEnabled(TransInitParams);
			Transition->ResetTransitionState();
		}

		if (OutTransition)
		{
			*OutTransition = Transition;
		}

		// Wire up the reference leaf: source Director is referenced live in the evaluation tree.
		EvaluationTree->OnActivateNewCameraWithReferenceSource(NewCamera, Transition, SourceDirector);
		RunningCamera = NewCamera;
	}

	return RunningCamera;
}

AComposableCameraCameraBase* UComposableCameraDirector::ReactivateCurrentCamera(
	AComposableCameraPlayerCameraManager* PlayerCameraManager,
	TSubclassOf<AComposableCameraCameraBase> CameraClass,
	UComposableCameraTransitionBase* Transition,
	UComposableCameraNodeInitializerDataAsset* NodeInitializerDataAsset,
	const FOnCameraFinishConstructed& OnPreBeginplayEvent)
{
	if (!RunningCamera || !PlayerCameraManager)
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT("ReactivateCurrentCamera: no running camera or no PlayerCameraManager."));
		return RunningCamera;
	}

	FTransform InitialTransform {
		PlayerCameraManager->GetCameraRotation().Quaternion(),
		PlayerCameraManager->GetCameraLocation()
	};

	if (UWorld* World = GetWorld())
	{
		
		AComposableCameraCameraBase* NewCamera = World->SpawnActorDeferred<AComposableCameraCameraBase>(CameraClass, InitialTransform);
		NewCamera->bIsTransient = false;
		NewCamera->LifeTime = -1.f;
		NewCamera->RemainingLifeTime = -1.f;
		
		ForceCameraPoses(NewCamera, InitialTransform);
		
		NewCamera->Initialize(PlayerCameraManager, NodeInitializerDataAsset);
		PlayerCameraManager->ApplyModifiers(NewCamera);
		OnPreBeginplayEvent.ExecuteIfBound(NewCamera);
		NewCamera->FinishSpawning(InitialTransform);

		if (Transition)
		{
			Transition = DuplicateObject(Transition, this);

			FComposableCameraTransitionInitParams TransInitParams;
			TransInitParams.CurrentSourcePose = LastEvaluatedPose;
			TransInitParams.PreviousSourcePose = PreviousEvaluatedPose;
			TransInitParams.DeltaTime = GetWorld()->GetDeltaSeconds();
			Transition->TransitionEnabled(TransInitParams);
			Transition->ResetTransitionState();
		}

		EvaluationTree->OnActivateNewCamera(NewCamera, Transition);
		RunningCamera = NewCamera;
	}

	return RunningCamera;
}

FComposableCameraPose UComposableCameraDirector::Evaluate(float DeltaTime)
{
	PreviousEvaluatedPose = LastEvaluatedPose;
	LastEvaluatedPose = EvaluationTree->Evaluate(DeltaTime);
	// Sync RunningCamera — the tree may have changed it during collapse.
	RunningCamera = EvaluationTree->GetRunningCamera();
	return LastEvaluatedPose;
}

void UComposableCameraDirector::DestroyAllCameras()
{
	if (EvaluationTree)
	{
		EvaluationTree->DestroyAll();
	}
	RunningCamera = nullptr;
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