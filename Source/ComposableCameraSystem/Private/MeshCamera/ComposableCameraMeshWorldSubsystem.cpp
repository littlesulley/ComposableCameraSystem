// Copyright 2026 Sulley. All Rights Reserved.

#include "MeshCamera/ComposableCameraMeshWorldSubsystem.h"

#include "Core/ComposableCameraContextStack.h"
#include "Core/ComposableCameraDirector.h"
#include "Core/ComposableCameraParameterBlock.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "DataAssets/ComposableCameraMeshProfile.h"
#include "DataAssets/ComposableCameraActionTypeAsset.h"
#include "DataAssets/ComposableCameraPatchTypeAsset.h"
#include "DataAssets/ComposableCameraModifierDataAsset.h"
#include "DataAssets/ComposableCameraTransitionDataAsset.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "MeshCamera/ComposableCameraMeshProfileState.h"
#include "MeshCamera/ComposableCameraMeshSurfaceStorageActor.h"
#include "Patches/ComposableCameraPatchHandle.h"
#include "Patches/ComposableCameraPatchManager.h"
#include "Actions/ComposableCameraActionBase.h"
#include "UObject/GCObject.h"

void UComposableCameraMeshWorldSubsystem::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UComposableCameraMeshWorldSubsystem* Subsystem = CastChecked<UComposableCameraMeshWorldSubsystem>(InThis);
	for (FPlayerLayerState& State : Subsystem->PlayerStates)
	{
		for (FActiveLayerState& Layer : State.ActiveLayers)
		{
			Collector.AddReferencedObject(Layer.PatchHandle);
		}
	}
	Super::AddReferencedObjects(InThis, Collector);
}

void UComposableCameraMeshWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	StorageActors.Reserve(16);
	PlayerStates.Reserve(4);
}

void UComposableCameraMeshWorldSubsystem::Deinitialize()
{
	// World teardown owns both PCMs and their duplicated modifier instances.
	// Do not reactivate cameras from OnModifierChanged while actors are ending play.
	PlayerStates.Reset();
	StorageActors.Reset();

	Super::Deinitialize();
}

bool UComposableCameraMeshWorldSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UComposableCameraMeshWorldSubsystem::RegisterStorageActor(
	AComposableCameraMeshSurfaceStorageActor* StorageActor)
{
	if (StorageActor)
	{
		StorageActors.AddUnique(StorageActor);
	}
}

void UComposableCameraMeshWorldSubsystem::UnregisterStorageActor(
	AComposableCameraMeshSurfaceStorageActor* StorageActor)
{
	StorageActors.RemoveAllSwap(
		[StorageActor](const TWeakObjectPtr<AComposableCameraMeshSurfaceStorageActor>& Candidate)
		{
			return Candidate.Get() == StorageActor;
		},
		EAllowShrinking::No);
}

bool UComposableCameraMeshWorldSubsystem::QueryMeshLayer(
	const FVector& WorldPosition,
	FComposableCameraMeshLayerQueryResult& OutResult,
	double MaxQueryDistance,
	double SameSurfaceTolerance) const
{
	OutResult = FComposableCameraMeshLayerQueryResult();
	FComposableCameraMeshLayerQueryResults Results;
	if (!QueryMeshLayers(
		WorldPosition,
		Results,
		MaxQueryDistance,
		SameSurfaceTolerance))
	{
		return false;
	}
	OutResult = Results[0];
	return true;
}

bool UComposableCameraMeshWorldSubsystem::QueryMeshLayers(
	const FVector& WorldPosition,
	FComposableCameraMeshLayerQueryResults& OutResults,
	double MaxQueryDistance,
	double SameSurfaceTolerance) const
{
	OutResults.Reset();
	double BestVerticalDistance = MaxQueryDistance;
	for (const TWeakObjectPtr<AComposableCameraMeshSurfaceStorageActor>& StorageActorPtr : StorageActors)
	{
		const AComposableCameraMeshSurfaceStorageActor* StorageActor = StorageActorPtr.Get();
		if (!StorageActor)
		{
			continue;
		}

		FComposableCameraMeshLayerQueryResults Candidates;
		if (!StorageActor->QueryLayers(
			WorldPosition,
			MaxQueryDistance,
			SameSurfaceTolerance,
			Candidates))
		{
			continue;
		}

		const double CandidateDistance = Candidates[0].VerticalDistance;
		if (OutResults.IsEmpty()
			|| CandidateDistance < BestVerticalDistance - SameSurfaceTolerance)
		{
			OutResults.Reset();
			BestVerticalDistance = CandidateDistance;
		}
		else if (FMath::Abs(CandidateDistance - BestVerticalDistance)
			> SameSurfaceTolerance)
		{
			continue;
		}

		for (const FComposableCameraMeshLayerQueryResult& Candidate : Candidates)
		{
			const bool bAlreadyPresent = OutResults.ContainsByPredicate(
				[&Candidate](const FComposableCameraMeshLayerQueryResult& Existing)
				{
					return Existing.StorageActor.Get() == Candidate.StorageActor.Get()
						&& Existing.LayerId == Candidate.LayerId;
				});
			if (!bAlreadyPresent)
			{
				OutResults.Add(Candidate);
			}
		}
	}

	OutResults.Sort(
		[](const FComposableCameraMeshLayerQueryResult& A,
			const FComposableCameraMeshLayerQueryResult& B)
		{
			if (A.LayerOrder != B.LayerOrder)
			{
				return A.LayerOrder < B.LayerOrder;
			}
			return reinterpret_cast<UPTRINT>(A.StorageActor.Get())
				< reinterpret_cast<UPTRINT>(B.StorageActor.Get());
		});
	return !OutResults.IsEmpty();
}

void UComposableCameraMeshWorldSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	++TickSerial;

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator)
	{
		APlayerController* PlayerController = Iterator->Get();
		if (!PlayerController || !PlayerController->IsLocalController())
		{
			continue;
		}

		AComposableCameraPlayerCameraManager* CameraManager =
			Cast<AComposableCameraPlayerCameraManager>(PlayerController->PlayerCameraManager);
		if (!CameraManager)
		{
			continue;
		}

		FPlayerLayerState* State = PlayerStates.FindByPredicate(
			[PlayerController](const FPlayerLayerState& Candidate)
			{
				return Candidate.PlayerController.Get() == PlayerController;
			});
		if (!State)
		{
			State = &PlayerStates.AddDefaulted_GetRef();
			State->PlayerController = PlayerController;
		}
		State->LastSeenFrame = TickSerial;

		FComposableCameraMeshLayerQueryResults CurrentLayers;
		if (const APawn* Pawn = PlayerController->GetPawn())
		{
			QueryMeshLayers(Pawn->GetActorLocation(), CurrentLayers);
		}

		UpdatePlayerLayers(*State, PlayerController, CameraManager, CurrentLayers);
	}

	for (int32 StateIndex = PlayerStates.Num() - 1; StateIndex >= 0; --StateIndex)
	{
		if (PlayerStates[StateIndex].LastSeenFrame != TickSerial)
		{
			RemoveStateEffects(PlayerStates[StateIndex]);
			PlayerStates.RemoveAtSwap(StateIndex, EAllowShrinking::No);
		}
	}

	StorageActors.RemoveAllSwap(
		[](const TWeakObjectPtr<AComposableCameraMeshSurfaceStorageActor>& Actor)
		{
			return !Actor.IsValid();
		},
		EAllowShrinking::No);
}

TStatId UComposableCameraMeshWorldSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UComposableCameraMeshWorldSubsystem, STATGROUP_Tickables);
}

void UComposableCameraMeshWorldSubsystem::UpdatePlayerLayers(
	FPlayerLayerState& State,
	APlayerController* PlayerController,
	AComposableCameraPlayerCameraManager* CameraManager,
	TConstArrayView<FComposableCameraMeshLayerQueryResult> CurrentLayers)
{
	AComposableCameraPlayerCameraManager* PreviousCameraManager = State.CameraManager.Get();
	if (PreviousCameraManager && PreviousCameraManager != CameraManager)
	{
		RemoveStateEffects(State);
	}

	if (!CameraManager)
	{
		State.ActiveLayers.Reset();
		State.PlayerController = PlayerController;
		State.CameraManager.Reset();
		return;
	}

	State.PlayerController = PlayerController;
	State.CameraManager = CameraManager;

	// Exit in reverse entry order so nested temporary Contexts pop top-down.
	for (int32 ActiveIndex = State.ActiveLayers.Num() - 1; ActiveIndex >= 0; --ActiveIndex)
	{
		const FActiveLayerState& ActiveLayer = State.ActiveLayers[ActiveIndex];
		const bool bStillInside = CurrentLayers.ContainsByPredicate(
			[&ActiveLayer](const FComposableCameraMeshLayerQueryResult& CurrentLayer)
			{
				return CurrentLayer.StorageActor.Get() == ActiveLayer.StorageActor.Get()
					&& CurrentLayer.LayerId == ActiveLayer.LayerId;
			});
		if (!bStillInside)
		{
			ExitLayer(State.ActiveLayers[ActiveIndex], CameraManager);
			State.ActiveLayers.RemoveAt(ActiveIndex, EAllowShrinking::No);
		}
	}

	// Multiple Layers can first appear on the same tick (spawn inside nesting).
	// Enter bottom list rows first so the top row receives the top Context.
	for (int32 CurrentIndex = CurrentLayers.Num() - 1; CurrentIndex >= 0; --CurrentIndex)
	{
		const FComposableCameraMeshLayerQueryResult& CurrentLayer = CurrentLayers[CurrentIndex];
		const bool bAlreadyActive = State.ActiveLayers.ContainsByPredicate(
			[&CurrentLayer](const FActiveLayerState& ActiveLayer)
			{
				return ActiveLayer.StorageActor.Get() == CurrentLayer.StorageActor.Get()
					&& ActiveLayer.LayerId == CurrentLayer.LayerId;
			});
		if (!bAlreadyActive)
		{
			EnterLayer(State, CameraManager, CurrentLayer);
		}
	}
}

void UComposableCameraMeshWorldSubsystem::EnterLayer(
	FPlayerLayerState& State,
	AComposableCameraPlayerCameraManager* CameraManager,
	const FComposableCameraMeshLayerQueryResult& Layer)
{
	FActiveLayerState& LayerState = State.ActiveLayers.AddDefaulted_GetRef();
	LayerState.StorageActor = Layer.StorageActor.Get();
	LayerState.LayerId = Layer.LayerId;
	LayerState.LayerName = Layer.LayerName;
	LayerState.Profile = Layer.Profile.Get();

	UComposableCameraMeshProfile* Profile = Layer.Profile.Get();
	if (!CameraManager || !Profile)
	{
		return;
	}
	if (Profile->NeedsTypeSelection())
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT("Mesh Layer '%s' skipped: legacy mixed Profile '%s' needs Type confirmation."),
			*Layer.LayerName.ToString(), *Profile->GetPathName());
		return;
	}

	switch (Profile->Type)
	{
	case EComposableCameraMeshProfileType::CameraType:
		break;
	case EComposableCameraMeshProfileType::Modifier:
	{
		TArray<UComposableCameraNodeModifierDataAsset*, TInlineAllocator<4>> NewModifiers;
		NewModifiers.Reserve(Profile->ModifierAssets.Num());
		for (UComposableCameraNodeModifierDataAsset* SourceAsset : Profile->ModifierAssets)
		{
			if (IsValid(SourceAsset))
			{
				UComposableCameraNodeModifierDataAsset* Instance = DuplicateObject<UComposableCameraNodeModifierDataAsset>(SourceAsset, CameraManager);
				if (Instance)
				{
					NewModifiers.Add(Instance);
					LayerState.ModifierInstances.Add(Instance);
				}
			}
		}
		if (!NewModifiers.IsEmpty())
		{
			CameraManager->ReplaceModifiers(TConstArrayView<UComposableCameraNodeModifierDataAsset*>(), NewModifiers, true);
		}
		return;
	}
	case EComposableCameraMeshProfileType::Action:
	{
		if (UComposableCameraActionTypeAsset* Asset = Profile->Action.ActionAsset.LoadSynchronous())
		{
			FComposableCameraParameterBlock Parameters;
			Profile->Action.BuildParameterBlock(*Asset, State.PlayerController.Get(), CameraManager, Layer.StorageActor.Get(), Parameters);
			LayerState.ActionInstance = CameraManager->AddCameraActionFromAsset(Asset, Parameters, Profile->Action.bOnlyForCurrentCamera);
		}
		else if (!Profile->Action.ActionAsset.IsNull())
		{
			UE_LOG(LogComposableCameraSystem, Warning, TEXT("Mesh Layer '%s' could not load ActionAsset '%s'."),
				*Layer.LayerName.ToString(), *Profile->Action.ActionAsset.ToSoftObjectPath().ToString());
		}
		return;
	}
	case EComposableCameraMeshProfileType::Patch:
	{
		const UComposableCameraContextStack* Stack = CameraManager->GetContextStack();
		UComposableCameraDirector* Director = Stack ? Stack->GetActiveDirector() : nullptr;
		UComposableCameraPatchManager* Manager = Director ? Director->GetPatchManager() : nullptr;
		if (UComposableCameraPatchTypeAsset* Asset = Profile->Patch.PatchAsset.LoadSynchronous(); Asset && Manager)
		{
			FComposableCameraParameterBlock Parameters;
			Profile->Patch.BuildParameterBlock(*Asset, Parameters);
			LayerState.PatchHandle = Manager->AddPatch(Asset, Profile->Patch.ActivationParams, Parameters);
			LayerState.PatchManager = Manager;
		}
		else if (!Profile->Patch.PatchAsset.IsNull())
		{
			UE_LOG(LogComposableCameraSystem, Warning, TEXT("Mesh Layer '%s' could not activate PatchAsset '%s': asset or active PatchManager unavailable."),
				*Layer.LayerName.ToString(), *Profile->Patch.PatchAsset.ToSoftObjectPath().ToString());
		}
		return;
	}
	default:
		UE_LOG(LogComposableCameraSystem, Warning, TEXT("Mesh Profile '%s' has an invalid Type."), *Profile->GetPathName());
		return;
	}

	UComposableCameraTypeAsset* CameraType = Profile->Camera.CameraType.IsNull()
		? nullptr
		: Profile->Camera.CameraType.LoadSynchronous();
	if (!CameraType || CameraType->IsA<UComposableCameraPatchTypeAsset>())
	{
		if (!Profile->Camera.CameraType.IsNull())
		{
			UE_LOG(LogComposableCameraSystem, Warning, TEXT(
				"Mesh Layer '%s' requires a valid CameraType asset (Patch assets use the Patch Profile Type): '%s'."),
				*Layer.LayerName.ToString(),
				*Profile->Camera.CameraType.ToSoftObjectPath().ToString());
		}
		return;
	}

	FComposableCameraParameterBlock CameraParameters;
	Profile->Camera.BuildParameterBlock(
		*CameraType,
		CameraParameters,
		FString::Printf(TEXT("Mesh Layer '%s'"), *Layer.LayerName.ToString()));
	FComposableCameraActivateParams ActivationParams = Profile->Camera.ActivationParams;
	ActivationParams.bIsTransient = false;
	ActivationParams.LifeTime = -1.0f;
	UComposableCameraTransitionDataAsset* TransitionOverride =
		Profile->Camera.TransitionOverride.IsNull()
			? nullptr
			: Profile->Camera.TransitionOverride.LoadSynchronous();

	const FName ContextHint(*FString::Printf(
		TEXT("Mesh_%s_%s"),
		*Layer.LayerName.ToString(),
		*Layer.LayerId.ToString(EGuidFormats::Short)));
	AComposableCameraCameraBase* PreviousCamera = CameraManager->GetRunningCamera();
	if (!UE::ComposableCameras::Mesh::ShouldCreateTemporaryCameraContext(
		true,
		!LayerState.OwnedCameraContextName.IsNone()))
	{
		return;
	}
	AComposableCameraCameraBase* ActivatedCamera =
		CameraManager->ActivateNewCameraFromTypeAssetInTemporaryContext(
			CameraType,
			TransitionOverride,
			ActivationParams,
			CameraParameters,
			ContextHint,
			LayerState.OwnedCameraContextName);
	if (!ActivatedCamera || ActivatedCamera == PreviousCamera
		|| LayerState.OwnedCameraContextName.IsNone())
	{
		LayerState.OwnedCameraContextName = NAME_None;
	}
}

void UComposableCameraMeshWorldSubsystem::ExitLayer(
	FActiveLayerState& LayerState,
	AComposableCameraPlayerCameraManager* CameraManager)
{
	if (CameraManager)
	{
		if (UComposableCameraActionBase* Action = LayerState.ActionInstance.Get())
		{
			CameraManager->RemoveCameraAction(Action);
		}
	}
	if (UComposableCameraPatchManager* Manager = LayerState.PatchManager.Get())
	{
		Manager->ExpirePatch(LayerState.PatchHandle);
	}
	LayerState.ActionInstance.Reset();
	LayerState.PatchHandle = nullptr;
	LayerState.PatchManager.Reset();

	if (!CameraManager)
	{
		LayerState.ModifierInstances.Reset();
		LayerState.OwnedCameraContextName = NAME_None;
		return;
	}

	TArray<UComposableCameraNodeModifierDataAsset*, TInlineAllocator<4>> RemovedModifiers;
	RemovedModifiers.Reserve(LayerState.ModifierInstances.Num());
	for (const TWeakObjectPtr<UComposableCameraNodeModifierDataAsset>& ModifierPtr :
		LayerState.ModifierInstances)
	{
		if (UComposableCameraNodeModifierDataAsset* Modifier = ModifierPtr.Get())
		{
			RemovedModifiers.Add(Modifier);
		}
	}
	if (!RemovedModifiers.IsEmpty())
	{
		CameraManager->ReplaceModifiers(
			RemovedModifiers,
			TConstArrayView<UComposableCameraNodeModifierDataAsset*>(),
			false);
	}

	bool bPoppedActiveCameraContext = false;
	const FName ContextName = LayerState.OwnedCameraContextName;
	if (!ContextName.IsNone())
	{
		const UComposableCameraContextStack* ContextStack = CameraManager->GetContextStack();
		if (ContextStack && ContextStack->GetDirectorForContext(ContextName))
		{
			bPoppedActiveCameraContext =
				CameraManager->GetActiveContextName() == ContextName;
			CameraManager->PopCameraContext(ContextName);
		}
	}

	// An active pop resumes the exact lower camera state that existed before
	// this Layer entered. Other exits change the current camera's modifier set.
	if (UE::ComposableCameras::Mesh::ShouldSyncModifierSelectionAfterLayerExit(
		!RemovedModifiers.IsEmpty(),
		bPoppedActiveCameraContext))
	{
		// The lower camera already has the correct pre-entry properties. Only
		// drop stale EffectiveModifiers references; rebuilding would reset it.
		CameraManager->RefreshEffectiveModifierSelection();
	}
	else if (UE::ComposableCameras::Mesh::ShouldRefreshModifiersAfterLayerExit(
		!RemovedModifiers.IsEmpty(),
		bPoppedActiveCameraContext))
	{
		CameraManager->OnModifierChanged();
	}

	LayerState.ModifierInstances.Reset();
	LayerState.OwnedCameraContextName = NAME_None;
	LayerState.Profile.Reset();
	LayerState.StorageActor.Reset();
}

void UComposableCameraMeshWorldSubsystem::RemoveStateEffects(FPlayerLayerState& State)
{
	AComposableCameraPlayerCameraManager* CameraManager = State.CameraManager.Get();
	for (int32 LayerIndex = State.ActiveLayers.Num() - 1; LayerIndex >= 0; --LayerIndex)
	{
		ExitLayer(State.ActiveLayers[LayerIndex], CameraManager);
	}
	State.ActiveLayers.Reset();
	State.PlayerController.Reset();
	State.CameraManager.Reset();
}
