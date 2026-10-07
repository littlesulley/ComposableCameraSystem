// Copyright 2026 Sulley. All Rights Reserved.

#include "MeshCamera/ComposableCameraMeshWorldSubsystem.h"

#include "Components/PrimitiveComponent.h"
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
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "GameFramework/PlayerController.h"
#include "MeshCamera/ComposableCameraMeshProfileState.h"
#include "MeshCamera/ComposableCameraMeshSurfaceStorageActor.h"
#include "Patches/ComposableCameraPatchHandle.h"
#include "Patches/ComposableCameraPatchManager.h"
#include "Actions/ComposableCameraActionBase.h"
#include "UObject/GCObject.h"

#if !UE_BUILD_SHIPPING
#include "ComposableCameraSystemModule.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#endif

namespace
{
#if !UE_BUILD_SHIPPING
	TWeakObjectPtr<UWorld> MeshLayerQueryDebugWorld;

	bool ConsumeMeshLayerQueryDebug(UWorld* World)
	{
		if (!MeshLayerQueryDebugWorld.IsValid() || MeshLayerQueryDebugWorld.Get() != World) return false;
		MeshLayerQueryDebugWorld.Reset();
		return true;
	}

	void ArmMeshLayerQueryDebug(UWorld* World)
	{
		MeshLayerQueryDebugWorld.Reset();
		if (!IsValid(World) || (World->WorldType != EWorldType::Game && World->WorldType != EWorldType::PIE))
		{
			UE_LOG(LogComposableCameraSystem, Display,
				TEXT("Mesh Layer Query: run CCS.MeshLayers.DebugNextQuery in the PIE game console."));
			return;
		}
		MeshLayerQueryDebugWorld = World;
		UE_LOG(LogComposableCameraSystem, Display,
			TEXT("Mesh Layer Query: Armed World=%s. Waiting for the next business Query or Update; no query is run by this command."),
			*World->GetPathName());
	}

	FAutoConsoleCommandWithWorld MeshLayerQueryDebugCommand(
		TEXT("CCS.MeshLayers.DebugNextQuery"),
		TEXT("Log the next actual Mesh Layer Query/Update in this Game/PIE world once: supplied ground hit, tolerance, registered documents and saved heights. Does not query or apply effects itself."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&ArmMeshLayerQueryDebug));

	void DumpMeshLayerQuery(UWorld* World, const FHitResult& GroundHit,
		const FComposableCameraMeshGroundQueryParams& Params,
		TConstArrayView<TWeakObjectPtr<AComposableCameraMeshSurfaceStorageActor>> Registered,
		TConstArrayView<FComposableCameraMeshLayerQueryResult> Results, const TCHAR* Reason)
	{
		int32 LiveRegistered = 0;
		for (const auto& Storage : Registered) { if (Storage.IsValid()) ++LiveRegistered; }
		const UPrimitiveComponent* Component = GroundHit.GetComponent();
		UE_LOG(LogComposableCameraSystem, Display,
			TEXT("Mesh Layer Query: Reason=%s World=%s Ground=%s Blocking=%d Penetrating=%d Component=%s Tolerance=%.3f Registered=%d Matches=%d"),
			Reason, *GetPathNameSafe(World), *GroundHit.ImpactPoint.ToString(), GroundHit.bBlockingHit,
			GroundHit.bStartPenetrating, *GetPathNameSafe(Component), Params.SurfaceTolerance, LiveRegistered, Results.Num());
		if (!World || World->bIsTearingDown || GroundHit.ImpactPoint.ContainsNaN()) return;
		// Debug-only discovery never registers or rebuilds a document. The wide
		// sample explains rejected heights; it must not change actual membership.
		const double ProbeRange = FMath::IsFinite(Params.SurfaceTolerance)
			? FMath::Clamp(Params.SurfaceTolerance, 300.0, 10000.0) : 300.0;
		for (TActorIterator<AComposableCameraMeshSurfaceStorageActor> It(World); It; ++It)
		{
			auto& Storage = **It;
			int32 Enabled = 0;
			for (const auto& Layer : Storage.GetLayers()) { if (Layer.bEnabled) ++Enabled; }
			FComposableCameraMeshLayerQueryResults Native;
			const bool bNativeHit = Storage.QueryLayers(GroundHit.ImpactPoint + FVector::UpVector * ProbeRange,
				2.0 * ProbeRange, 0.0, Native);
			const double NativeZ = bNativeHit ? Native[0].SurfacePosition.Z : 0.0;
			UE_LOG(LogComposableCameraSystem, Display,
				TEXT("Mesh Layer Query: Document=%s Registered=%d BegunPlay=%d Layers=%d Enabled=%d Triangles=%d NativeHit=%d NearestEnabledSourceZ=%.3f DeltaValid=%d SourceMinusGroundCm=%.3f ProbeRange=%.3f"),
				*Storage.GetPathName(), Registered.Contains(TWeakObjectPtr<AComposableCameraMeshSurfaceStorageActor>(&Storage)),
				Storage.HasActorBegunPlay(), Storage.GetLayers().Num(), Enabled, Storage.GetRuntimeData().Indices.Num() / 3,
				bNativeHit, NativeZ, bNativeHit && GroundHit.IsValidBlockingHit(),
				bNativeHit ? NativeZ - GroundHit.ImpactPoint.Z : 0.0, ProbeRange);
		}
		for (const auto& Result : Results)
		{
			UE_LOG(LogComposableCameraSystem, Display,
				TEXT("Mesh Layer Query: Match Document=%s Layer=%s Order=%d SurfaceZ=%.3f SourceMinusGroundCm=%.3f Profile=%s"),
				*GetPathNameSafe(Result.StorageActor.Get()), *Result.LayerName.ToString(), Result.LayerOrder,
				Result.SurfacePosition.Z, Result.SurfacePosition.Z - GroundHit.ImpactPoint.Z, *GetPathNameSafe(Result.Profile.Get()));
		}
	}
#endif

	void GetMeshProfileAssetPaths(const UComposableCameraMeshProfile* Profile,
		TArray<const FSoftObjectPath*, TInlineAllocator<2>>& OutPaths)
	{
		if (!Profile || Profile->NeedsTypeSelection()) return;
		auto AddPath = [&OutPaths](const FSoftObjectPath& Path)
		{
			if (!Path.IsNull() && !OutPaths.ContainsByPredicate(
				[&Path](const FSoftObjectPath* Existing) { return *Existing == Path; })) OutPaths.Add(&Path);
		};
		switch (Profile->Type)
		{
		case EComposableCameraMeshProfileType::CameraType:
			AddPath(Profile->Camera.CameraType.ToSoftObjectPath());
			AddPath(Profile->Camera.TransitionOverride.ToSoftObjectPath());
			break;
		case EComposableCameraMeshProfileType::Action:
			AddPath(Profile->Action.ActionAsset.ToSoftObjectPath());
			break;
		case EComposableCameraMeshProfileType::Patch:
			AddPath(Profile->Patch.PatchAsset.ToSoftObjectPath());
			break;
		case EComposableCameraMeshProfileType::Modifier:
			// Modifier templates are already hard references on the Profile.
			break;
		default:
			break;
		}
	}
}

void UComposableCameraMeshWorldSubsystem::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UComposableCameraMeshWorldSubsystem* Subsystem = CastChecked<UComposableCameraMeshWorldSubsystem>(InThis);
	for (FProfilePreloadState& Preload : Subsystem->ProfilePreloads)
	{
		for (TObjectPtr<UObject>& Asset : Preload.LoadedAssets) Collector.AddReferencedObject(Asset);
	}
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
	ProfilePreloads.Reserve(16);
}

void UComposableCameraMeshWorldSubsystem::Deinitialize()
{
	// World teardown owns both PCMs and their duplicated modifier instances.
	// Do not reactivate cameras from OnModifierChanged while actors are ending play.
	for (auto& State : PlayerStates) UnbindPlayerOwners(State);
	PlayerStates.Reset();
	StorageActors.Reset();
	for (FProfilePreloadState& Preload : ProfilePreloads)
	{
		if (Preload.Handle) Preload.Handle->CancelHandle();
	}
	ProfilePreloads.Reset();

	Super::Deinitialize();
}

bool UComposableCameraMeshWorldSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UComposableCameraMeshWorldSubsystem::RegisterStorageActor(
	AComposableCameraMeshSurfaceStorageActor* StorageActor)
{
	if (IsValid(StorageActor) && StorageActor->GetWorld() == GetWorld())
	{
		StorageActors.AddUnique(StorageActor);
		RefreshProfilePreloads();
	}
}

void UComposableCameraMeshWorldSubsystem::UnregisterStorageActor(
	AComposableCameraMeshSurfaceStorageActor* StorageActor)
{
	if (!StorageActor) return;
	const TWeakObjectPtr<AComposableCameraMeshSurfaceStorageActor> RemovedStorage(StorageActor);
	const UWorld* World = GetWorld();
	for (auto& State : PlayerStates)
	{
		for (int32 Index = State.ActiveLayers.Num() - 1; Index >= 0; --Index)
		{
			if (State.ActiveLayers[Index].StorageActor != RemovedStorage) continue;
			// Teardown must not reactivate an already-ending camera. Normal Level
			// unload still releases this document's effects without another Update.
			if (World && !World->bIsTearingDown) ExitLayer(State.ActiveLayers[Index], State.CameraManager.Get());
			State.ActiveLayers.RemoveAt(Index, EAllowShrinking::No);
		}
	}
	StorageActors.RemoveAllSwap(
		[RemovedStorage](const TWeakObjectPtr<AComposableCameraMeshSurfaceStorageActor>& Candidate)
		{
			return Candidate == RemovedStorage;
		},
		EAllowShrinking::No);
	RefreshProfilePreloads();
}

UComposableCameraMeshWorldSubsystem::FProfilePreloadState&
UComposableCameraMeshWorldSubsystem::EnsureProfilePreload(UComposableCameraMeshProfile* Profile)
{
	FProfilePreloadState* Preload = ProfilePreloads.FindByPredicate(
		[Profile](const FProfilePreloadState& Candidate) { return Candidate.Profile.Get() == Profile; });
	if (!Preload)
	{
		Preload = &ProfilePreloads.AddDefaulted_GetRef();
		Preload->Profile = Profile;
	}
	// Borrow path references while polling, so even a subobject path's FString
	// is copied only on configuration edges, never on each pending frame.
	TArray<const FSoftObjectPath*, TInlineAllocator<2>> Paths;
	GetMeshProfileAssetPaths(Profile, Paths);
	bool bPathsChanged = Preload->AssetPaths.Num() != Paths.Num();
	for (int32 Index = 0; !bPathsChanged && Index < Paths.Num(); ++Index)
	{
		bPathsChanged = Preload->AssetPaths[Index] != *Paths[Index];
	}
	if (bPathsChanged)
	{
		if (Preload->Handle) Preload->Handle->CancelHandle();
		Preload->Handle.Reset();
		Preload->LoadedAssets.Reset();
		Preload->AssetPaths.Reset();
		for (const FSoftObjectPath* Path : Paths) Preload->AssetPaths.Add(*Path);
		Preload->bResolved = false;
		bool bAllLoaded = true;
		for (const FSoftObjectPath& Path : Preload->AssetPaths)
		{
			if (UObject* Asset = Path.ResolveObject()) Preload->LoadedAssets.AddUnique(Asset);
			else bAllLoaded = false;
		}
		if (!bAllLoaded)
		{
			// No callback captures actors or player state. Membership is checked
			// again on the next explicit Update before entry; the handle retains assets.
			Preload->Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
				Preload->AssetPaths, FStreamableDelegate(), FStreamableManager::DefaultAsyncLoadPriority,
				false, false, TEXT("CCS Mesh Profile"));
		}
	}
	if (!Preload->bResolved && (!Preload->Handle || Preload->Handle->HasLoadCompleted()
		|| Preload->Handle->WasCanceled()))
	{
		for (const FSoftObjectPath& Path : Preload->AssetPaths)
		{
			if (UObject* Asset = Path.ResolveObject()) Preload->LoadedAssets.AddUnique(Asset);
		}
		// A failed load completes too: EnterLayer reports the missing asset once
		// and keeps its normal one-entry semantics rather than retrying every call.
		Preload->bResolved = true;
	}
	return *Preload;
}

bool UComposableCameraMeshWorldSubsystem::AreProfileAssetsReady(UComposableCameraMeshProfile* Profile)
{
	return !Profile || EnsureProfilePreload(Profile).bResolved;
}

void UComposableCameraMeshWorldSubsystem::RefreshProfilePreloads()
{
	// This subsystem applies effects only to local players. Dedicated servers
	// may still query geometry, but should not stream presentation assets.
	if (const UWorld* World = GetWorld(); World && World->GetNetMode() == NM_DedicatedServer) return;
	for (const auto& StoragePtr : StorageActors)
	{
		if (const auto* Storage = StoragePtr.Get())
		{
			for (const auto& Layer : Storage->GetLayers())
			{
				if (Layer.bEnabled && Layer.Profile) EnsureProfilePreload(Layer.Profile.Get());
			}
		}
	}
	for (int32 Index = ProfilePreloads.Num() - 1; Index >= 0; --Index)
	{
		const auto& Preload = ProfilePreloads[Index];
		const bool bStillUsed = Preload.Profile.IsValid() && StorageActors.ContainsByPredicate(
			[&Preload](const auto& StoragePtr)
			{
				const auto* Storage = StoragePtr.Get();
				return Storage && Storage->GetLayers().ContainsByPredicate(
					[&Preload](const auto& Layer) { return Layer.bEnabled && Layer.Profile.Get() == Preload.Profile.Get(); });
			});
		if (!bStillUsed)
		{
			if (Preload.Handle) Preload.Handle->CancelHandle();
			ProfilePreloads.RemoveAtSwap(Index, EAllowShrinking::No);
		}
	}
}

bool UComposableCameraMeshWorldSubsystem::QueryMeshLayer(
	const FHitResult& GroundHit,
	const FComposableCameraMeshGroundQueryParams& QueryParams,
	FComposableCameraMeshLayerQueryResult& OutResult) const
{
	OutResult = FComposableCameraMeshLayerQueryResult();
	FComposableCameraMeshLayerQueryResults Results;
	if (!QueryMeshLayersInline(GroundHit, QueryParams, Results)) return false;
	OutResult = Results[0];
	return true;
}

bool UComposableCameraMeshWorldSubsystem::QueryMeshLayers(
	const FHitResult& GroundHit,
	const FComposableCameraMeshGroundQueryParams& QueryParams,
	TArray<FComposableCameraMeshLayerQueryResult>& OutResults) const
{
	OutResults.Reset();
	FComposableCameraMeshLayerQueryResults Results;
	if (!QueryMeshLayersInline(GroundHit, QueryParams, Results)) return false;
	OutResults.Append(Results.GetData(), Results.Num());
	return true;
}

bool UComposableCameraMeshWorldSubsystem::QueryMeshLayersInline(
	const FHitResult& GroundHit,
	const FComposableCameraMeshGroundQueryParams& QueryParams,
	FComposableCameraMeshLayerQueryResults& OutResults) const
{
	OutResults.Reset();
	UWorld* World = GetWorld();
#if !UE_BUILD_SHIPPING
	const bool bDebugQuery = ConsumeMeshLayerQueryDebug(World);
#endif
	auto Finish = [&](const TCHAR* Reason)
	{
#if !UE_BUILD_SHIPPING
		if (bDebugQuery) DumpMeshLayerQuery(World, GroundHit, QueryParams,
			StorageActors, MakeArrayView(OutResults), Reason);
#endif
		return !OutResults.IsEmpty();
	};
	if (!World || World->bIsTearingDown) return Finish(TEXT("WorldUnavailable"));
	if (GroundHit.bStartPenetrating) return Finish(TEXT("StartPenetrating"));
	if (!GroundHit.IsValidBlockingHit()) return Finish(TEXT("InvalidGroundHit"));
	const UPrimitiveComponent* Component = GroundHit.GetComponent();
	if (!IsValid(Component) || (Component->GetOwner() && Component->GetOwner()->IsActorBeingDestroyed()))
		return Finish(TEXT("GroundComponentUnavailable"));
	if (Component->GetWorld() != World) return Finish(TEXT("GroundWorldMismatch"));
	if (GroundHit.ImpactPoint.ContainsNaN()
		|| !FMath::IsFinite(QueryParams.SurfaceTolerance) || QueryParams.SurfaceTolerance < 0.0)
		return Finish(TEXT("InvalidNumericInput"));

	// The business-provided ground fixes XY and the only admissible height band.
	// No scene trace, Pawn lookup or search below this band can choose another floor.
	const double RayLength = 2.0 * QueryParams.SurfaceTolerance;
	const FVector RayOrigin = GroundHit.ImpactPoint + FVector::UpVector * QueryParams.SurfaceTolerance;
	const FVector RayEnd = GroundHit.ImpactPoint - FVector::UpVector * QueryParams.SurfaceTolerance;
	if (!FMath::IsFinite(RayLength) || RayOrigin.ContainsNaN() || RayEnd.ContainsNaN())
		return Finish(TEXT("InvalidNumericInput"));
	bool bHasStorage = false;
	for (const auto& StoragePtr : StorageActors)
	{
		const auto* Storage = StoragePtr.Get();
		if (!IsValid(Storage) || Storage->IsActorBeingDestroyed()) continue;
		bHasStorage = true;
		FComposableCameraMeshLayerQueryResults Candidates;
		// Group the entire bounded segment, not a band below its highest Layer.
		// Each returned Layer retains its own exact nearest triangle intersection.
		if (!Storage->QueryLayers(RayOrigin, RayLength, RayLength, Candidates)) continue;
		for (auto& Candidate : Candidates)
		{
			const double HeightDelta = Candidate.SurfacePosition.Z - GroundHit.ImpactPoint.Z;
			if (!FMath::IsFinite(HeightDelta) || FMath::Abs(HeightDelta) > QueryParams.SurfaceTolerance) continue;
			Candidate.VerticalDistance = FMath::Abs(HeightDelta);
			if (!OutResults.ContainsByPredicate([&Candidate](const auto& Existing)
			{
				return Existing.StorageActor == Candidate.StorageActor && Existing.LayerId == Candidate.LayerId;
			})) OutResults.Add(Candidate);
		}
	}
	OutResults.Sort([](const auto& A, const auto& B)
	{
		if (A.LayerOrder != B.LayerOrder) return A.LayerOrder < B.LayerOrder;
		return reinterpret_cast<UPTRINT>(A.StorageActor.Get()) < reinterpret_cast<UPTRINT>(B.StorageActor.Get());
	});
	return Finish(!OutResults.IsEmpty() ? TEXT("MatchedGround")
		: bHasStorage ? TEXT("NoLayerOnGround") : TEXT("NoRegisteredDocuments"));
}

bool UComposableCameraMeshWorldSubsystem::UpdateMeshLayers(
	APlayerController* PlayerController,
	const FHitResult& GroundHit,
	const FComposableCameraMeshGroundQueryParams& QueryParams)
{
	UWorld* World = GetWorld();
	AComposableCameraPlayerCameraManager* CameraManager = IsValid(PlayerController)
		? Cast<AComposableCameraPlayerCameraManager>(PlayerController->PlayerCameraManager) : nullptr;
	if (!World || World->bIsTearingDown || !IsValid(PlayerController)
		|| PlayerController->IsActorBeingDestroyed() || PlayerController->GetWorld() != World || !PlayerController->IsLocalController()
		|| !IsValid(CameraManager) || CameraManager->IsActorBeingDestroyed() || CameraManager->GetWorld() != World)
	{
#if !UE_BUILD_SHIPPING
		if (ConsumeMeshLayerQueryDebug(World))
		{
			DumpMeshLayerQuery(World, GroundHit, QueryParams,
				StorageActors, {}, TEXT("InvalidUpdateOwner"));
			UE_LOG(LogComposableCameraSystem, Display, TEXT("Mesh Layer Query: Update PlayerController=%s CameraManager=%s Local=%d"),
				*GetPathNameSafe(PlayerController), *GetPathNameSafe(CameraManager),
				IsValid(PlayerController) && PlayerController->IsLocalController());
		}
#endif
		ClearMeshLayers(PlayerController);
		return false;
	}
	FComposableCameraMeshLayerQueryResults CurrentLayers;
	const bool bFound = QueryMeshLayersInline(GroundHit, QueryParams, CurrentLayers);
	FPlayerLayerState* State = PlayerStates.FindByPredicate(
		[PlayerController](const FPlayerLayerState& Candidate) { return Candidate.PlayerController.Get() == PlayerController; });
	if (!State)
	{
		if (!bFound) return false;
		// State allocation is a player-ownership edge, never stable polling work.
		State = &PlayerStates.AddDefaulted_GetRef();
		State->PlayerController = PlayerController;
	}
	UpdatePlayerLayers(*State, PlayerController, CameraManager, CurrentLayers);
	PlayerController->OnEndPlay.AddUniqueDynamic(this, &UComposableCameraMeshWorldSubsystem::OnPlayerOwnerEndPlay);
	CameraManager->OnEndPlay.AddUniqueDynamic(this, &UComposableCameraMeshWorldSubsystem::OnPlayerOwnerEndPlay);
	return bFound;
}

void UComposableCameraMeshWorldSubsystem::ClearMeshLayers(APlayerController* PlayerController)
{
	const TWeakObjectPtr<APlayerController> Owner(PlayerController);
	for (int32 Index = PlayerStates.Num() - 1; Index >= 0; --Index)
	{
		if (PlayerStates[Index].PlayerController != Owner) continue;
		const UWorld* World = GetWorld();
		if (World && !World->bIsTearingDown) RemoveStateEffects(PlayerStates[Index]);
		else UnbindPlayerOwners(PlayerStates[Index]);
		PlayerStates.RemoveAtSwap(Index, EAllowShrinking::No);
	}
}

void UComposableCameraMeshWorldSubsystem::UnbindPlayerOwners(FPlayerLayerState& State)
{
	if (auto* Player = State.PlayerController.Get())
		Player->OnEndPlay.RemoveDynamic(this, &UComposableCameraMeshWorldSubsystem::OnPlayerOwnerEndPlay);
	if (auto* Manager = State.CameraManager.Get())
		Manager->OnEndPlay.RemoveDynamic(this, &UComposableCameraMeshWorldSubsystem::OnPlayerOwnerEndPlay);
}

void UComposableCameraMeshWorldSubsystem::OnPlayerOwnerEndPlay(AActor* Actor, EEndPlayReason::Type /*EndPlayReason*/)
{
	const TWeakObjectPtr<AActor> Owner(Actor);
	for (int32 Index = PlayerStates.Num() - 1; Index >= 0; --Index)
	{
		auto& State = PlayerStates[Index];
		const bool bManagerEnding = TWeakObjectPtr<AActor>(State.CameraManager) == Owner;
		if (TWeakObjectPtr<AActor>(State.PlayerController) != Owner && !bManagerEnding) continue;
		const UWorld* World = GetWorld();
		if (World && !World->bIsTearingDown)
		{
			if (bManagerEnding)
			{
				// The ended manager owns its own Action/Camera/Modifier lifetime.
				// Do not pop/reactivate it, even on non-destruction EndPlay.
				UnbindPlayerOwners(State);
				State.CameraManager.Reset();
			}
			RemoveStateEffects(State);
		}
		else UnbindPlayerOwners(State);
		PlayerStates.RemoveAtSwap(Index, EAllowShrinking::No);
	}
}

void UComposableCameraMeshWorldSubsystem::UpdatePlayerLayers(
	FPlayerLayerState& State,
	APlayerController* PlayerController,
	AComposableCameraPlayerCameraManager* CameraManager,
	TConstArrayView<FComposableCameraMeshLayerQueryResult> CurrentLayers)
{
	// Compare weak ownership identities as well as live actors: an already-dead
	// previous manager must not carry its recorded membership into the new one.
	if (State.CameraManager != TWeakObjectPtr<AComposableCameraPlayerCameraManager>(CameraManager))
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

	// Multiple Layers can first appear on the same explicit Update.
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
			// Preserve bottom-to-top entry order when overlapping Profiles load
			// at different speeds. Pending Layers have no owned effects yet.
			if (!AreProfileAssetsReady(CurrentLayer.Profile.Get())) break;
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
		if (UComposableCameraActionTypeAsset* Asset = Profile->Action.ActionAsset.Get())
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
		if (UComposableCameraPatchTypeAsset* Asset = Profile->Patch.PatchAsset.Get(); Asset && Manager)
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
		: Profile->Camera.CameraType.Get();
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
			: Profile->Camera.TransitionOverride.Get();

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
	UnbindPlayerOwners(State);
	if (CameraManager && CameraManager->IsActorBeingDestroyed()) CameraManager = nullptr;
	for (int32 LayerIndex = State.ActiveLayers.Num() - 1; LayerIndex >= 0; --LayerIndex)
	{
		ExitLayer(State.ActiveLayers[LayerIndex], CameraManager);
	}
	State.ActiveLayers.Reset();
	State.PlayerController.Reset();
	State.CameraManager.Reset();
}
