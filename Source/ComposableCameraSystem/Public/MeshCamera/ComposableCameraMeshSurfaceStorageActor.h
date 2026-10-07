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

	virtual void PostLoad() override;
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
	virtual void PostEditUndo() override;
	virtual void PostEditUndo(TSharedPtr<ITransactionObjectAnnotation> TransactionAnnotation) override;
#endif

#if WITH_EDITORONLY_DATA
	const FComposableCameraMeshSurfaceAuthoringData& GetAuthoringData() const { return AuthoringData; }
	const FComposableCameraMeshSurfaceEditorPreview& GetEditorPreview() const { return EditorPreview; }
	void SetEditorPreview(FComposableCameraMeshSurfaceEditorPreview&& InPreview) { EditorPreview = MoveTemp(InPreview); }
	/** Content identity, independent of actor lifetime or array counts. Never serialized. */
	uint64 GetEditorDataRevision() const { return EditorDataRevision; }

	/** Copies tool source. Metadata reuses query acceleration and, when coverage policy is unchanged, saved preview. */
	void SetAuthoringData(
		const TArray<FComposableCameraMeshLayerDefinition>& InLayers,
		const FComposableCameraMeshSurfaceAuthoringData& InAuthoringData);

	void RebuildRuntimeData();
#endif

private:
	void RebuildQueryResources(bool bRebuildSpatialIndex = true);

	UPROPERTY()
	TArray<FComposableCameraMeshLayerDefinition> LayerDefinitions;

	UPROPERTY()
	FComposableCameraMeshSurfaceRuntimeData RuntimeData;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	FComposableCameraMeshSurfaceAuthoringData AuthoringData;
	/** Disposable resolved coverage baked by Save. Stripped from cooked packages. */
	UPROPERTY()
	FComposableCameraMeshSurfaceEditorPreview EditorPreview;
	uint64 EditorDataRevision = 0;
#endif
};
