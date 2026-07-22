// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MeshCamera/ComposableCameraMeshSurfaceTypes.h"
#include "ComposableCameraMeshSurfaceStorageActor.generated.h"

/**
 * Invisible, tool-owned Level data container. Users never place or edit it.
 * Its transform anchors all surface data to the owning Level / Level Instance.
 */
UCLASS(NotPlaceable, hidecategories = (Actor, Collision, Cooking, DataLayers, HLOD, Input, LevelInstance, Networking, Physics, Rendering, Replication, WorldPartition))
class COMPOSABLECAMERASYSTEM_API AComposableCameraMeshSurfaceStorageActor : public AActor
{
	GENERATED_BODY()

public:
	AComposableCameraMeshSurfaceStorageActor();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	const TArray<FComposableCameraMeshLayerDefinition>& GetLayers() const { return LayerDefinitions; }
	const FComposableCameraMeshSurfaceRuntimeData& GetRuntimeData() const { return RuntimeData; }

	bool QueryLayer(
		const FVector& WorldPosition,
		double MaxWorldDistance,
		double SameSurfaceTolerance,
		FComposableCameraMeshLayerQueryResult& OutResult) const;

	/** Returns all enabled Layers covering the nearest floor surface. */
	bool QueryLayers(
		const FVector& WorldPosition,
		double MaxWorldDistance,
		double SameSurfaceTolerance,
		FComposableCameraMeshLayerQueryResults& OutResults) const;

#if WITH_EDITOR
	virtual bool IsListedInSceneOutliner() const override { return false; }
#endif

#if WITH_EDITORONLY_DATA
	const FComposableCameraMeshSurfaceAuthoringData& GetAuthoringData() const { return AuthoringData; }

	/** Copies tool source, then rebuilds disposable cooked query data. */
	void SetAuthoringData(
		const TArray<FComposableCameraMeshLayerDefinition>& InLayers,
		const FComposableCameraMeshSurfaceAuthoringData& InAuthoringData);

	void RebuildRuntimeData();
#endif

private:
	UPROPERTY()
	TArray<FComposableCameraMeshLayerDefinition> LayerDefinitions;

	UPROPERTY()
	FComposableCameraMeshSurfaceRuntimeData RuntimeData;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	FComposableCameraMeshSurfaceAuthoringData AuthoringData;
#endif
};
