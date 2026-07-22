// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MeshCamera/ComposableCameraMeshSurfaceTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "ComposableCameraMeshWorldSubsystem.generated.h"

class AComposableCameraMeshSurfaceStorageActor;
class AComposableCameraPlayerCameraManager;
class APlayerController;
class UComposableCameraMeshProfile;
class UComposableCameraNodeModifierDataAsset;

/** Queries loaded local surface documents and applies their Profile effects. */
UCLASS()
class COMPOSABLECAMERASYSTEM_API UComposableCameraMeshWorldSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	void RegisterStorageActor(AComposableCameraMeshSurfaceStorageActor* StorageActor);
	void UnregisterStorageActor(AComposableCameraMeshSurfaceStorageActor* StorageActor);

	/** Blueprint convenience query: returns the top list-order Layer on the nearest floor. */
	UFUNCTION(BlueprintCallable, Category = "Composable Camera System|Mesh Camera")
	bool QueryMeshLayer(
		const FVector& WorldPosition,
		FComposableCameraMeshLayerQueryResult& OutResult,
		double MaxQueryDistance = 300.0,
		double SameSurfaceTolerance = 5.0) const;

	/** C++ query used by runtime reconciliation. Returns every Layer on the nearest floor. */
	bool QueryMeshLayers(
		const FVector& WorldPosition,
		FComposableCameraMeshLayerQueryResults& OutResults,
		double MaxQueryDistance = 300.0,
		double SameSurfaceTolerance = 5.0) const;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FActiveLayerState
	{
		TWeakObjectPtr<AComposableCameraMeshSurfaceStorageActor> StorageActor;
		FGuid LayerId;
		FName LayerName = NAME_None;
		TWeakObjectPtr<UComposableCameraMeshProfile> Profile;
		TArray<TWeakObjectPtr<UComposableCameraNodeModifierDataAsset>, TInlineAllocator<4>> ModifierInstances;
		/** One temporary Context per active Camera-bearing Layer. */
		FName OwnedCameraContextName = NAME_None;
	};

	struct FPlayerLayerState
	{
		TWeakObjectPtr<APlayerController> PlayerController;
		TWeakObjectPtr<AComposableCameraPlayerCameraManager> CameraManager;
		/** Entry order. Last Camera-bearing entry owns the top temporary Context. */
		TArray<FActiveLayerState, TInlineAllocator<16>> ActiveLayers;
		uint64 LastSeenFrame = 0;
	};

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

	TArray<TWeakObjectPtr<AComposableCameraMeshSurfaceStorageActor>> StorageActors;
	TArray<FPlayerLayerState> PlayerStates;
	uint64 TickSerial = 0;
};
