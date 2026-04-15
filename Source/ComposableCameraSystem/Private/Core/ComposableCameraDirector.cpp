// Copyright Sulley. All rights reserved.

#include "Core/ComposableCameraDirector.h"

#include "ComposableCameraSystemModule.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
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
		
		NewCamera->Initialize(PlayerCameraManager);
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
		// -> PreBeginPlay (OnPreBeginplayEvent — for type-asset cameras this
		//    is OnTypeAssetCameraConstructed, which populates CameraNodes)
		// -> Modifiers (ApplyModifiers — must run after nodes exist)
		// -> BeginPlay (FinishSpawning).
		NewCamera->Initialize(PlayerCameraManager);
		OnPreBeginplayEvent.ExecuteIfBound(NewCamera);
		PlayerCameraManager->ApplyModifiers(NewCamera, true);
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

AComposableCameraCameraBase* UComposableCameraDirector::ActivateNewCamera(
	AComposableCameraPlayerCameraManager* PlayerCameraManager,
	TSubclassOf<AComposableCameraCameraBase> CameraClass,
	UComposableCameraTransitionBase* TransitionInstance,
	const FComposableCameraActivateParams& ActivationParams,
	FOnCameraFinishConstructed OnPreBeginplayEvent)
{
	bool bPreserveCameraPose = ActivationParams.bPreserveCameraPose;
	FTransform InitialTransform = ActivationParams.InitialTransform;
	bool bUseInitialTransformRotation = ActivationParams.bUseInitialTransformRotation;
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

		NewCamera->Initialize(PlayerCameraManager);
		OnPreBeginplayEvent.ExecuteIfBound(NewCamera);
		PlayerCameraManager->ApplyModifiers(NewCamera, true);
		NewCamera->FinishSpawning(InitialTransform);

		UComposableCameraTransitionBase* Transition = nullptr;
		if (TransitionInstance && RunningCamera)
		{
			FComposableCameraTransitionInitParams TransInitParams;
			TransInitParams.CurrentSourcePose = LastEvaluatedPose;
			TransInitParams.PreviousSourcePose = PreviousEvaluatedPose;
			TransInitParams.DeltaTime = GetWorld()->GetDeltaSeconds();

			Transition = DuplicateObject(TransitionInstance, this);
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

		NewCamera->Initialize(PlayerCameraManager);
		OnPreBeginplayEvent.ExecuteIfBound(NewCamera);
		PlayerCameraManager->ApplyModifiers(NewCamera, true);
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

AComposableCameraCameraBase* UComposableCameraDirector::ActivateNewCameraWithReferenceSource(
	AComposableCameraPlayerCameraManager* PlayerCameraManager,
	TSubclassOf<AComposableCameraCameraBase> CameraClass,
	UComposableCameraTransitionBase* TransitionInstance,
	const FComposableCameraActivateParams& ActivationParams,
	FOnCameraFinishConstructed OnPreBeginplayEvent,
	UComposableCameraDirector* SourceDirector)
{
	check(SourceDirector);

	bool bPreserveCameraPose = ActivationParams.bPreserveCameraPose;
	FTransform InitialTransform = ActivationParams.InitialTransform;
	bool bUseInitialTransformRotation = ActivationParams.bUseInitialTransformRotation;
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

		NewCamera->Initialize(PlayerCameraManager);
		OnPreBeginplayEvent.ExecuteIfBound(NewCamera);
		PlayerCameraManager->ApplyModifiers(NewCamera, true);
		NewCamera->FinishSpawning(InitialTransform);

		UComposableCameraTransitionBase* Transition = nullptr;
		if (TransitionInstance)
		{
			FComposableCameraTransitionInitParams TransInitParams;
			TransInitParams.CurrentSourcePose = SourceDirector->GetLastEvaluatedPose();
			TransInitParams.PreviousSourcePose = SourceDirector->GetPreviousEvaluatedPose();
			TransInitParams.DeltaTime = GetWorld()->GetDeltaSeconds();

			Transition = DuplicateObject(TransitionInstance, this);
			Transition->TransitionEnabled(TransInitParams);
			Transition->ResetTransitionState();
		}

		EvaluationTree->OnActivateNewCameraWithReferenceSource(NewCamera, Transition, SourceDirector);
		RunningCamera = NewCamera;
	}

	return RunningCamera;
}

AComposableCameraCameraBase* UComposableCameraDirector::ReactivateCurrentCamera(
	AComposableCameraPlayerCameraManager* PlayerCameraManager,
	TSubclassOf<AComposableCameraCameraBase> CameraClass,
	UComposableCameraTransitionBase* Transition,	const FOnCameraFinishConstructed& OnPreBeginplayEvent)
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
		
		NewCamera->Initialize(PlayerCameraManager);
		OnPreBeginplayEvent.ExecuteIfBound(NewCamera);
		PlayerCameraManager->ApplyModifiers(NewCamera);
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

void UComposableCameraDirector::BuildDebugString(TStringBuilder<1024>& OutString, int32 IndentLevel) const
{
	const FString Indent = FString::ChrN(IndentLevel * 4, ' ');

	{
		FString CameraLabel;
		if (RunningCamera)
		{
			if (const UComposableCameraTypeAsset* TA = RunningCamera->SourceTypeAsset.Get())
			{
				CameraLabel = TA->GetName();
			}
			else if (RunningCamera->CameraTag.IsValid())
			{
				CameraLabel = RunningCamera->CameraTag.ToString();
			}
			else
			{
				CameraLabel = RunningCamera->GetName();
			}
		}
		else
		{
			CameraLabel = TEXT("(none)");
		}
		OutString.Appendf(TEXT("%sRunning Camera: %s\n"), *Indent, *CameraLabel);
	}
	OutString.Appendf(TEXT("%sLast Pose: %s  Rot: %s  FOV: %.1f\n"), *Indent,
		*LastEvaluatedPose.Position.ToCompactString(),
		*LastEvaluatedPose.Rotation.ToCompactString(),
		LastEvaluatedPose.GetEffectiveFieldOfView());
	OutString.Appendf(TEXT("%sEvaluation Tree:\n"), *Indent);

	if (EvaluationTree)
	{
		EvaluationTree->BuildDebugString(OutString, IndentLevel + 1);
	}
	else
	{
		OutString.Appendf(TEXT("%s    (no tree)\n"), *Indent);
	}
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
