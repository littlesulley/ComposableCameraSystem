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

		EvaluationTree->OnActivateNewCamera(NewCamera, Transition, ActivationParams.bFreezeSourceCamera);
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

		EvaluationTree->OnActivateNewCamera(NewCamera, Transition, ActivationParams.bFreezeSourceCamera);
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

		// Wire up the reference leaf: source Director is referenced live (or frozen) in the evaluation tree.
		EvaluationTree->OnActivateNewCameraWithReferenceSource(NewCamera, Transition, SourceDirector, ActivationParams.bFreezeSourceCamera);
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

		if (OutTransition)
		{
			*OutTransition = Transition;
		}

		EvaluationTree->OnActivateNewCameraWithReferenceSource(NewCamera, Transition, SourceDirector, ActivationParams.bFreezeSourceCamera);
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

AComposableCameraCameraBase* UComposableCameraDirector::ResumeCurrentCameraWithReferenceSource(
	AComposableCameraPlayerCameraManager* PlayerCameraManager,
	UComposableCameraTransitionBase* TransitionInstance,
	UComposableCameraDirector* SourceDirector,
	bool bFreezeSourceCamera)
{
	check(SourceDirector);

	if (!EvaluationTree || !EvaluationTree->HasActiveCamera())
	{
		// Nothing to resume — caller must take the ActivateNew path.
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("ResumeCurrentCameraWithReferenceSource: Director has no running camera to resume. Use ActivateNewCameraWithReferenceSource instead."));
		return nullptr;
	}

	// Configure the transition's InitParams from SourceDirector's blended
	// output so transitions like Inertialized get the right initial
	// velocity — source context's render output is exactly what the user
	// was just seeing, so the new pop transition blends FROM that into
	// our existing tree's output.
	if (TransitionInstance)
	{
		FComposableCameraTransitionInitParams TransInitParams;
		TransInitParams.CurrentSourcePose  = SourceDirector->GetLastEvaluatedPose();
		TransInitParams.PreviousSourcePose = SourceDirector->GetPreviousEvaluatedPose();
		TransInitParams.DeltaTime          = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;

		TransitionInstance->TransitionEnabled(TransInitParams);
		TransitionInstance->ResetTransitionState();
	}

	EvaluationTree->OnResumeCurrentTreeWithReferenceSource(
		TransitionInstance, SourceDirector, bFreezeSourceCamera);

	// RunningCamera unchanged — it was already the correct camera before
	// this call and remains so after the tree is rewrapped.
	return RunningCamera;
}

DECLARE_CYCLE_STAT(TEXT("Director Evaluate"), STAT_CCS_Director_Evaluate, STATGROUP_CCS);

FComposableCameraPose UComposableCameraDirector::Evaluate(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_CCS_Director_Evaluate);
	TRACE_CPUPROFILER_EVENT_SCOPE(CCS_Director_Evaluate);

	// No reentrancy guard is needed here. Inter-context ReferenceLeaves
	// now hold a TSharedPtr SNAPSHOT of the source tree rather than a
	// live pointer to the source Director — they re-evaluate the captured
	// subtree directly, never calling back into Director::Evaluate. The
	// resulting reachable graph is a pure DAG (same original leaf may be
	// reached via multiple paths; per-node memoization at the wrapper
	// layer collapses duplicate work), so Director::Evaluate is invoked
	// at most once per director per frame — by the ContextStack on the
	// single active director. Pending-destroy directors are never ticked
	// via Evaluate; their trees only contribute through the snapshot
	// TSharedPtrs held by the active tree.
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

void UComposableCameraDirector::BuildDebugSnapshot(FComposableCameraContextSnapshot& OutSnapshot) const
{
	// Running camera display name: type asset → gameplay tag → UObject name → "(none)".
	if (RunningCamera)
	{
		if (const UComposableCameraTypeAsset* TA = RunningCamera->SourceTypeAsset.Get())
		{
			OutSnapshot.RunningCameraDisplay = TA->GetName();
		}
		else if (RunningCamera->CameraTag.IsValid())
		{
			OutSnapshot.RunningCameraDisplay = RunningCamera->CameraTag.ToString();
		}
		else
		{
			OutSnapshot.RunningCameraDisplay = RunningCamera->GetName();
		}
	}
	else
	{
		OutSnapshot.RunningCameraDisplay = TEXT("(none)");
	}

	OutSnapshot.LastPose = LastEvaluatedPose;

	OutSnapshot.TreeNodes.Reset();
	if (EvaluationTree)
	{
		EvaluationTree->BuildDebugSnapshot(OutSnapshot.TreeNodes);
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
