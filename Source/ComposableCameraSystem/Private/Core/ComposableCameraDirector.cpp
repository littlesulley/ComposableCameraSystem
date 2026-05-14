// Copyright Sulley. All rights reserved.

#include "Core/ComposableCameraDirector.h"

#include "ComposableCameraSystemModule.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Transitions/ComposableCameraTransitionBase.h"
#include "Core/ComposableCameraEvaluationTree.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "DataAssets/ComposableCameraTransitionDataAsset.h"
#include "Engine/World.h"
#include "Patches/ComposableCameraPatchManager.h"

namespace
{
	/** `GetDeltaSeconds` with an explicit null-World guard. The Director
	 *  outer always has a World during normal play, but multiple eval
	 *  paths (notably `ResumeCamera` and the activation variants) build
	 *  `FComposableCameraTransitionInitParams` even during late teardown,
	 *  test-only Director construction, or commandlet runs where
	 *  `GetWorld()` legitimately returns null. A bare
	 *  `GetWorld()->GetDeltaSeconds()` would null-deref. Returning 0.f on
	 *  null is the documented "no time has elapsed" zero-duration fallback
	 *  every transition's `TransitionEnabled` already tolerates. */
	float SafeDeltaSeconds(const UWorld* World)
	{
		return World ? World->GetDeltaSeconds() : 0.f;
	}

	/** Wrap `DuplicateObject<UComposableCameraTransitionBase>` with the null-check
	 *  every Director transition path needs. `DuplicateObject` legitimately returns
	 *  nullptr on archetype lookup failure, RF_NeedLoad-during-PostLoad collisions,
	 *  GC interference, and the source object being mid-destruction. Director
	 *  paths used to call `Transition->TransitionEnabled(...)` immediately after
	 *  the dup, crashing instead of falling through to a hard cut.
	 *
	 *  Returns nullptr on failure (logged with the call site). Callers should
	 *  treat that as "no transition" -`EvaluationTree::OnActivateNewCamera`
	 *  already handles a null Transition by hard-cutting on the next eval
	 *  frame, which is the documented degradation path. */
	UComposableCameraTransitionBase* DuplicateTransitionOrNull(
		UComposableCameraTransitionBase* SourceTransition,
		UObject* Outer,
		const TCHAR* CallSite)
	{
		if (!SourceTransition)
		{
			return nullptr;
		}
		UComposableCameraTransitionBase* Duplicated = DuplicateObject(SourceTransition, Outer);
		if (!Duplicated)
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("%s: DuplicateObject returned null for transition '%s'. Falling back to hard cut."),
				CallSite, *GetNameSafe(SourceTransition));
		}
		return Duplicated;
	}

	/** Wrap `World->SpawnActorDeferred<AComposableCameraCameraBase>` with the
	 *  null-checks every call site needs. SpawnActorDeferred legitimately
	 *  returns nullptr on class-load failure, world teardown, blocked spawn
	 *  collision queries, and a handful of other late-init-time conditions - every Director activation path used to dereference the result
	 *  immediately to write `bIsTransient` / `LifeTime` / etc., crashing on
	 *  null instead of falling through to the PCM's outer fallback. The PCM
	 *  caller's null-handling contract still applies; this helper just makes
	 *  it reachable. Logs once per failure with enough context to diagnose
	 *  (which CameraClass, why null) so the silent-crash tail is gone. */
	AComposableCameraCameraBase* SpawnCameraCheckedOrNull(
		UWorld* World,
		TSubclassOf<AComposableCameraCameraBase> CameraClass,
		const FTransform& InitialTransform,
		const TCHAR* CallSite)
	{
		if (!World)
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("%s: cannot spawn camera -World is null."), CallSite);
			return nullptr;
		}
		if (!*CameraClass)
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("%s: cannot spawn camera -CameraClass is null."), CallSite);
			return nullptr;
		}
		AComposableCameraCameraBase* Spawned =
			World->SpawnActorDeferred<AComposableCameraCameraBase>(CameraClass, InitialTransform);
		if (!Spawned)
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("%s: SpawnActorDeferred returned null for class '%s'."),
				CallSite, *GetNameSafe(*CameraClass));
		}
		return Spawned;
	}
}

UComposableCameraDirector::UComposableCameraDirector(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// CreateDefaultSubobject works when called from the owning actor's constructor.
	// For dynamically created Directors (e.g., from context stack PushContext),
	// PostInitProperties handles initialization.
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		EvaluationTree = NewObject<UComposableCameraEvaluationTree>(this, TEXT("ComposableCameraEvaluationTree"));
		PatchManager = NewObject<UComposableCameraPatchManager>(this, TEXT("ComposableCameraPatchManager"));
	}
}

void UComposableCameraDirector::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UComposableCameraDirector* This = CastChecked<UComposableCameraDirector>(InThis);
	// `LastEvaluatedPose` / `PreviousEvaluatedPose` are non-UPROPERTY fields
	// (they're owned by the C++ class body, not surfaced through reflection)
	// so the GC's reflection walk does not see UObject references inside
	// their embedded FPostProcessSettings. Color-grading materials, vignette
	// textures, weighted-blendable assets. Walk them explicitly via the
	// pose USTRUCT's reflected property graph so a material that is only
	// referenced through a cached director pose can't get collected mid-
	// blend and leave a dangling TObjectPtr behind.
	Collector.AddPropertyReferencesWithStructARO(
		FComposableCameraPose::StaticStruct(),
		&This->LastEvaluatedPose);
	Collector.AddPropertyReferencesWithStructARO(
		FComposableCameraPose::StaticStruct(),
		&This->PreviousEvaluatedPose);
	Super::AddReferencedObjects(InThis, Collector);
}

AComposableCameraCameraBase* UComposableCameraDirector::ResumeCamera(AComposableCameraCameraBase* InResumeCamera,
	UComposableCameraTransitionBase* Transition, const FTransform& Transform)
{
	if (!IsValid(InResumeCamera))
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT(
			"Director::ResumeCamera: input camera is null or invalid. Aborting to avoid installing a null camera leaf."));
		return RunningCamera;
	}

	if (!EvaluationTree)
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT(
			"Director::ResumeCamera: EvaluationTree is null. Aborting."));
		return RunningCamera;
	}

	if (Transition && RunningCamera)
	{
		ForceCameraPoses(InResumeCamera, Transform);

		Transition = DuplicateTransitionOrNull(Transition, this, TEXT("Director::ResumeCamera"));
		if (Transition)
		{
			FComposableCameraTransitionInitParams TransInitParams;
			TransInitParams.CurrentSourcePose = LastEvaluatedPose;
			TransInitParams.PreviousSourcePose = PreviousEvaluatedPose;
			TransInitParams.DeltaTime = SafeDeltaSeconds(GetWorld());
			Transition->TransitionEnabled(TransInitParams);
			Transition->ResetTransitionState();
		}
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
		AComposableCameraCameraBase* NewCamera = SpawnCameraCheckedOrNull(
			World, CameraClass, InitialTransform, TEXT("Director::CreateNewCamera"));
		if (!NewCamera)
		{
			return nullptr;
		}

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
		AComposableCameraCameraBase* NewCamera = SpawnCameraCheckedOrNull(
			World, CameraClass, InitialTransform, TEXT("Director::ActivateNewCamera (DataAsset)"));
		if (!NewCamera)
		{
			return RunningCamera;
		}

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
		// ->Initialize (Initialize, node initializers applied here)
		// ->PreBeginPlay (OnPreBeginplayEvent. For type-asset cameras this
		//    is OnTypeAssetCameraConstructed, which populates CameraNodes)
		// ->Modifiers (ApplyModifiers. Must run after nodes exist)
		// ->BeginPlay (FinishSpawning).
		NewCamera->Initialize(PlayerCameraManager);
		OnPreBeginplayEvent.ExecuteIfBound(NewCamera);
		PlayerCameraManager->ApplyModifiers(NewCamera, true);
		NewCamera->FinishSpawning(InitialTransform);

		UComposableCameraTransitionBase* Transition = nullptr;
		if (TransitionDataAsset && TransitionDataAsset->Transition && RunningCamera)
		{
			Transition = DuplicateTransitionOrNull(TransitionDataAsset->Transition, this,
				TEXT("Director::ActivateNewCamera (DataAsset)"));
			if (Transition)
			{
				FComposableCameraTransitionInitParams TransInitParams;
				TransInitParams.CurrentSourcePose = LastEvaluatedPose;
				TransInitParams.PreviousSourcePose = PreviousEvaluatedPose;
				TransInitParams.DeltaTime = SafeDeltaSeconds(GetWorld());

				Transition->TransitionEnabled(TransInitParams);
				Transition->ResetTransitionState();
			}
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
		AComposableCameraCameraBase* NewCamera = SpawnCameraCheckedOrNull(
			World, CameraClass, InitialTransform, TEXT("Director::ActivateNewCamera (Instance)"));
		if (!NewCamera)
		{
			return RunningCamera;
		}

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
			Transition = DuplicateTransitionOrNull(TransitionInstance, this,
				TEXT("Director::ActivateNewCamera (Instance)"));
			if (Transition)
			{
				FComposableCameraTransitionInitParams TransInitParams;
				TransInitParams.CurrentSourcePose = LastEvaluatedPose;
				TransInitParams.PreviousSourcePose = PreviousEvaluatedPose;
				TransInitParams.DeltaTime = SafeDeltaSeconds(GetWorld());

				Transition->TransitionEnabled(TransInitParams);
				Transition->ResetTransitionState();
			}
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
	if (!ensureMsgf(SourceDirector, TEXT(
		"Director::ActivateNewCameraWithReferenceSource (DataAsset): SourceDirector is null. "
		"Inter-context activation requires a valid source director.")))
	{
		return RunningCamera;
	}

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
		AComposableCameraCameraBase* NewCamera = SpawnCameraCheckedOrNull(
			World, CameraClass, InitialTransform, TEXT("Director::ActivateNewCameraWithReferenceSource (DataAsset)"));
		if (!NewCamera)
		{
			return RunningCamera;
		}

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
			Transition = DuplicateTransitionOrNull(TransitionDataAsset->Transition, this,
				TEXT("Director::ActivateNewCameraWithReferenceSource (DataAsset)"));
			if (Transition)
			{
				// Use the source Director's last evaluated (blended) pose as the transition source.
				// This is what the player was actually seeing before the context switch.
				FComposableCameraTransitionInitParams TransInitParams;
				TransInitParams.CurrentSourcePose = SourceDirector->GetLastEvaluatedPose();
				TransInitParams.PreviousSourcePose = SourceDirector->GetPreviousEvaluatedPose();
				TransInitParams.DeltaTime = SafeDeltaSeconds(GetWorld());

				Transition->TransitionEnabled(TransInitParams);
				Transition->ResetTransitionState();
			}
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
	if (!ensureMsgf(SourceDirector, TEXT(
		"Director::ActivateNewCameraWithReferenceSource (Instance): SourceDirector is null. "
		"Inter-context activation requires a valid source director.")))
	{
		return RunningCamera;
	}

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
		AComposableCameraCameraBase* NewCamera = SpawnCameraCheckedOrNull(
			World, CameraClass, InitialTransform, TEXT("Director::ActivateNewCameraWithReferenceSource (Instance)"));
		if (!NewCamera)
		{
			return RunningCamera;
		}

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
			Transition = DuplicateTransitionOrNull(TransitionInstance, this,
				TEXT("Director::ActivateNewCameraWithReferenceSource (Instance)"));
			if (Transition)
			{
				FComposableCameraTransitionInitParams TransInitParams;
				TransInitParams.CurrentSourcePose = SourceDirector->GetLastEvaluatedPose();
				TransInitParams.PreviousSourcePose = SourceDirector->GetPreviousEvaluatedPose();
				TransInitParams.DeltaTime = SafeDeltaSeconds(GetWorld());

				Transition->TransitionEnabled(TransInitParams);
				Transition->ResetTransitionState();
			}
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
		AComposableCameraCameraBase* NewCamera = SpawnCameraCheckedOrNull(
			World, CameraClass, InitialTransform, TEXT("Director::ReactivateCurrentCamera"));
		if (!NewCamera)
		{
			return RunningCamera;
		}

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
			Transition = DuplicateTransitionOrNull(Transition, this,
				TEXT("Director::ReactivateCurrentCamera"));
			if (Transition)
			{
				FComposableCameraTransitionInitParams TransInitParams;
				TransInitParams.CurrentSourcePose = LastEvaluatedPose;
				TransInitParams.PreviousSourcePose = PreviousEvaluatedPose;
				TransInitParams.DeltaTime = SafeDeltaSeconds(GetWorld());
				Transition->TransitionEnabled(TransInitParams);
				Transition->ResetTransitionState();
			}
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
	if (!ensureMsgf(SourceDirector, TEXT(
		"Director::ResumeCurrentCameraWithReferenceSource: SourceDirector is null. "
		"Pop-resume requires a valid source director to reference as blend source.")))
	{
		return nullptr;
	}

	if (!EvaluationTree || !EvaluationTree->HasActiveCamera())
	{
		// Nothing to resume. Caller must take the ActivateNew path.
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("ResumeCurrentCameraWithReferenceSource: Director has no running camera to resume. Use ActivateNewCameraWithReferenceSource instead."));
		return nullptr;
	}

	// Configure the transition's InitParams from SourceDirector's blended
	// output so transitions like Inertialized get the right initial
	// velocity. Source context's render output is exactly what the user
	// was just seeing, so the new pop transition blends FROM that into
	// our existing tree's output.
	if (TransitionInstance)
	{
		FComposableCameraTransitionInitParams TransInitParams;
		TransInitParams.CurrentSourcePose  = SourceDirector->GetLastEvaluatedPose();
		TransInitParams.PreviousSourcePose = SourceDirector->GetPreviousEvaluatedPose();
		TransInitParams.DeltaTime          = SafeDeltaSeconds(GetWorld());

		TransitionInstance->TransitionEnabled(TransInitParams);
		TransitionInstance->ResetTransitionState();
	}

	EvaluationTree->OnResumeCurrentTreeWithReferenceSource(
		TransitionInstance, SourceDirector, bFreezeSourceCamera);

	// RunningCamera unchanged. It was already the correct camera before
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
	// live pointer to the source Director. They re-evaluate the captured
	// subtree directly, never calling back into Director::Evaluate. The
	// resulting reachable graph is a pure DAG (same original leaf may be
	// reached via multiple paths; per-node memoization at the wrapper
	// layer collapses duplicate work), so Director::Evaluate is invoked
	// at most once per director per frame. By the ContextStack on the
	// single active director. Pending-destroy directors are never ticked
	// via Evaluate; their trees only contribute through the snapshot
	// TSharedPtrs held by the active tree.
	PreviousEvaluatedPose = LastEvaluatedPose;
	const FComposableCameraPose TreePose = EvaluationTree->Evaluate(DeltaTime);

	// Apply the patch overlay pass after the EvaluationTree produces its blended
	// pose. PatchManager.Apply iterates active patches sorted by (LayerIndex,
	// PushSequence) and chains each patch's evaluator on top. See
	// UComposableCameraPatchManager::Apply. A null PatchManager would be a
	// construction-time bug, but the guard keeps test-only Director paths
	// (NewObject<UComposableCameraDirector>() with no Outer) safe.
	LastEvaluatedPose = PatchManager
		? PatchManager->Apply(DeltaTime, TreePose)
		: TreePose;

	// Sync RunningCamera. The tree may have changed it during collapse.
	RunningCamera = EvaluationTree->GetRunningCamera();
	return LastEvaluatedPose;
}

void UComposableCameraDirector::DestroyAllCameras()
{
	if (EvaluationTree)
	{
		EvaluationTree->DestroyAll();
	}
	if (PatchManager)
	{
		PatchManager->DestroyAll();
	}
	RunningCamera = nullptr;
}

void UComposableCameraDirector::BuildDebugSnapshot(FComposableCameraContextSnapshot& OutSnapshot) const
{
	// Running camera display name: type asset ->gameplay tag ->UObject name ->"(none)".
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

	OutSnapshot.Patches.Reset();
	if (PatchManager)
	{
		PatchManager->BuildDebugSnapshot(OutSnapshot.Patches);
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
