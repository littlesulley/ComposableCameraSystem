// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/HitResult.h"
#include "MeshCamera/ComposableCameraMeshSurfaceTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/SoftObjectPath.h"
#include "ComposableCameraMeshWorldSubsystem.generated.h"

class AComposableCameraMeshSurfaceStorageActor;
class AComposableCameraPlayerCameraManager;
class APlayerController;
class UComposableCameraMeshProfile;
class UComposableCameraNodeModifierDataAsset;
class UComposableCameraActionBase;
class UComposableCameraPatchHandle;
class UComposableCameraPatchManager;
struct FStreamableHandle;

/** Explicit surface queries and caller-driven Profile reconciliation. Never polls players or Pawns. */
UCLASS(BlueprintType)
class COMPOSABLECAMERASYSTEM_API UComposableCameraMeshWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void RegisterStorageActor(AComposableCameraMeshSurfaceStorageActor* StorageActor);
	void UnregisterStorageActor(AComposableCameraMeshSurfaceStorageActor* StorageActor);

	/** Read-only query. Returns the top Layer matching the caller's valid blocking GroundHit. Never discovers ground or reads a Pawn. */
	UFUNCTION(BlueprintCallable, Category = "Composable Camera System|Mesh Camera")
	bool QueryMeshLayer(
		const FHitResult& GroundHit,
		const FComposableCameraMeshGroundQueryParams& QueryParams,
		FComposableCameraMeshLayerQueryResult& OutResult) const;

	/** Read-only query at GroundHit.ImpactPoint XY. Returns enabled Layers within SurfaceTolerance of ground, top row first. */
	UFUNCTION(BlueprintCallable, Category = "Composable Camera System|Mesh Camera")
	bool QueryMeshLayers(
		const FHitResult& GroundHit,
		const FComposableCameraMeshGroundQueryParams& QueryParams,
		TArray<FComposableCameraMeshLayerQueryResult>& OutResults) const;

	/** C++ query with inline result storage for repeated business-side calls. */
	bool QueryMeshLayersInline(
		const FHitResult& GroundHit,
		const FComposableCameraMeshGroundQueryParams& QueryParams,
		FComposableCameraMeshLayerQueryResults& OutResults) const;

	/** Query the supplied ground and reconcile one local player. Invalid/missed ground also exits previous Layers. Retry on later calls while assets load. */
	UFUNCTION(BlueprintCallable, Category = "Composable Camera System|Mesh Camera")
	bool UpdateMeshLayers(
		APlayerController* PlayerController,
		const FHitResult& GroundHit,
		const FComposableCameraMeshGroundQueryParams& QueryParams);

	/** Stop using Mesh Layers for this player. Removes only effects owned by this subsystem. */
	UFUNCTION(BlueprintCallable, Category = "Composable Camera System|Mesh Camera")
	void ClearMeshLayers(APlayerController* PlayerController);

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	friend class FComposableCameraMeshProfileDispatchTest;
	friend class FComposableCameraMeshProfilePreloadTest;
	friend class FComposableCameraMeshManualUpdateTest;
	struct FProfilePreloadState
	{
		TWeakObjectPtr<UComposableCameraMeshProfile> Profile;
		TArray<FSoftObjectPath, TInlineAllocator<2>> AssetPaths;
		/** Manual GC mirror also covers already-loaded transient test/PIE assets. */
		TArray<TObjectPtr<UObject>, TInlineAllocator<2>> LoadedAssets;
		TSharedPtr<FStreamableHandle> Handle;
		bool bResolved = false;
	};
	struct FActiveLayerState
	{
		TWeakObjectPtr<AComposableCameraMeshSurfaceStorageActor> StorageActor;
		FGuid LayerId;
		FName LayerName = NAME_None;
		TWeakObjectPtr<UComposableCameraMeshProfile> Profile;
		TArray<TWeakObjectPtr<UComposableCameraNodeModifierDataAsset>, TInlineAllocator<4>> ModifierInstances;
		/** One temporary Context per active Camera-bearing Layer. */
		FName OwnedCameraContextName = NAME_None;
		TWeakObjectPtr<UComposableCameraActionBase> ActionInstance;
		TWeakObjectPtr<UComposableCameraPatchManager> PatchManager;
		/** Manually GC-tracked by AddReferencedObjects; the Patch instance owns only a weak handle. */
		TObjectPtr<UComposableCameraPatchHandle> PatchHandle = nullptr;
	};

	struct FPlayerLayerState
	{
		TWeakObjectPtr<APlayerController> PlayerController;
		TWeakObjectPtr<AComposableCameraPlayerCameraManager> CameraManager;
		/** Entry order. Last Camera-bearing entry owns the top temporary Context. */
		TArray<FActiveLayerState, TInlineAllocator<16>> ActiveLayers;
	};

	UFUNCTION()
	void OnPlayerOwnerEndPlay(AActor* Actor, EEndPlayReason::Type EndPlayReason);
	void UnbindPlayerOwners(FPlayerLayerState& State);

	void UpdatePlayerLayers(
		FPlayerLayerState& State,
		APlayerController* PlayerController,
		AComposableCameraPlayerCameraManager* CameraManager,
		TConstArrayView<FComposableCameraMeshLayerQueryResult> CurrentLayers);

	void EnterLayer(
		FPlayerLayerState& State,
		AComposableCameraPlayerCameraManager* CameraManager,
		const FComposableCameraMeshLayerQueryResult& Layer);

	void ExitLayer(
		FActiveLayerState& LayerState,
		AComposableCameraPlayerCameraManager* CameraManager);

	void RemoveStateEffects(FPlayerLayerState& State);
	FProfilePreloadState& EnsureProfilePreload(UComposableCameraMeshProfile* Profile);
	bool AreProfileAssetsReady(UComposableCameraMeshProfile* Profile);
	void RefreshProfilePreloads();

	TArray<TWeakObjectPtr<AComposableCameraMeshSurfaceStorageActor>> StorageActors;
	TArray<FPlayerLayerState> PlayerStates;
	TArray<FProfilePreloadState> ProfilePreloads;
};
