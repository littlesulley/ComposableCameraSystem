// Copyright 2026 Sulley. All Rights Reserved.

#include "MeshCamera/ComposableCameraMeshSurfaceStorageActor.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "MeshCamera/ComposableCameraMeshWorldSubsystem.h"

AComposableCameraMeshSurfaceStorageActor::AComposableCameraMeshSurfaceStorageActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetActorHiddenInGame(true);
	SetCanBeDamaged(false);

	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("MeshSurfaceRoot"));
	SetRootComponent(SceneRoot);
}

void AComposableCameraMeshSurfaceStorageActor::BeginPlay()
{
	Super::BeginPlay();

	if (UComposableCameraMeshWorldSubsystem* Subsystem =
		GetWorld()->GetSubsystem<UComposableCameraMeshWorldSubsystem>())
	{
		Subsystem->RegisterStorageActor(this);
	}
}

void AComposableCameraMeshSurfaceStorageActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UComposableCameraMeshWorldSubsystem* Subsystem =
			World->GetSubsystem<UComposableCameraMeshWorldSubsystem>())
		{
			Subsystem->UnregisterStorageActor(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

bool AComposableCameraMeshSurfaceStorageActor::QueryLayer(
	const FVector& WorldPosition,
	double MaxWorldDistance,
	double SameSurfaceTolerance,
	FComposableCameraMeshLayerQueryResult& OutResult) const
{
	FComposableCameraMeshLayerQueryResults Results;
	if (!QueryLayers(
		WorldPosition,
		MaxWorldDistance,
		SameSurfaceTolerance,
		Results))
	{
		return false;
	}
	OutResult = Results[0];
	return true;
}

bool AComposableCameraMeshSurfaceStorageActor::QueryLayers(
	const FVector& WorldPosition,
	double MaxWorldDistance,
	double SameSurfaceTolerance,
	FComposableCameraMeshLayerQueryResults& OutResults) const
{
	OutResults.Reset();
	const FTransform ActorTransform = GetActorTransform();
	const FVector LocalOrigin = ActorTransform.InverseTransformPosition(WorldPosition);
	const FVector LocalDirection = ActorTransform.InverseTransformVector(FVector::DownVector).GetSafeNormal();
	const double WorldUnitsPerLocalUnit = ActorTransform.TransformVector(LocalDirection).Length();
	if (WorldUnitsPerLocalUnit <= UE_DOUBLE_SMALL_NUMBER)
	{
		return false;
	}

	TArray<int32, TInlineAllocator<16>> LayerIndices;
	FVector LocalSurfacePosition = FVector::ZeroVector;
	double LocalRayDistance = 0.0;
	if (!RuntimeData.QueryLocalRayLayers(
		LocalOrigin,
		LocalDirection,
		MaxWorldDistance / WorldUnitsPerLocalUnit,
		SameSurfaceTolerance / WorldUnitsPerLocalUnit,
		LayerDefinitions,
		LayerIndices,
		LocalSurfacePosition,
		LocalRayDistance))
	{
		return false;
	}

	const FVector WorldSurfacePosition = ActorTransform.TransformPosition(LocalSurfacePosition);
	const double VerticalDistance = FVector::Distance(WorldPosition, WorldSurfacePosition);
	for (const int32 LayerIndex : LayerIndices)
	{
		const FComposableCameraMeshLayerDefinition& Layer = LayerDefinitions[LayerIndex];
		FComposableCameraMeshLayerQueryResult& Result = OutResults.AddDefaulted_GetRef();
		Result.LayerId = Layer.LayerId;
		Result.LayerName = Layer.Name;
		Result.LayerOrder = LayerIndex;
		Result.Profile = Layer.Profile;
		Result.SurfacePosition = WorldSurfacePosition;
		Result.VerticalDistance = VerticalDistance;
		Result.StorageActor = const_cast<AComposableCameraMeshSurfaceStorageActor*>(this);
	}
	return !OutResults.IsEmpty();
}

#if WITH_EDITORONLY_DATA
void AComposableCameraMeshSurfaceStorageActor::SetAuthoringData(
	const TArray<FComposableCameraMeshLayerDefinition>& InLayers,
	const FComposableCameraMeshSurfaceAuthoringData& InAuthoringData)
{
	LayerDefinitions = InLayers;
	AuthoringData = InAuthoringData;
	RebuildRuntimeData();
}

void AComposableCameraMeshSurfaceStorageActor::RebuildRuntimeData()
{
	RuntimeData.Reset();
	if (!AuthoringData.IsConsistent())
	{
		return;
	}

	TMap<FGuid, int32> LayerIndicesById;
	LayerIndicesById.Reserve(LayerDefinitions.Num());
	for (int32 LayerIndex = 0; LayerIndex < LayerDefinitions.Num(); ++LayerIndex)
	{
		if (LayerDefinitions[LayerIndex].LayerId.IsValid())
		{
			LayerIndicesById.Add(LayerDefinitions[LayerIndex].LayerId, LayerIndex);
		}
	}

	const int32 TriangleCount = AuthoringData.Indices.Num() / 3;
	RuntimeData.Vertices.Reserve(AuthoringData.Vertices.Num());
	RuntimeData.Indices.Reserve(AuthoringData.Indices.Num());
	RuntimeData.TriangleLayerIndices.Reserve(TriangleCount);

	for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
	{
		const int32* LayerIndex = LayerIndicesById.Find(AuthoringData.TriangleLayerIds[TriangleIndex]);
		if (!LayerIndex)
		{
			continue;
		}

		const int32 SourceOffset = TriangleIndex * 3;
		const int32 SourceIndex0 = AuthoringData.Indices[SourceOffset];
		const int32 SourceIndex1 = AuthoringData.Indices[SourceOffset + 1];
		const int32 SourceIndex2 = AuthoringData.Indices[SourceOffset + 2];
		if (!AuthoringData.Vertices.IsValidIndex(SourceIndex0)
			|| !AuthoringData.Vertices.IsValidIndex(SourceIndex1)
			|| !AuthoringData.Vertices.IsValidIndex(SourceIndex2))
		{
			continue;
		}

		const int32 TargetIndex0 = RuntimeData.Vertices.Add(AuthoringData.Vertices[SourceIndex0]);
		const int32 TargetIndex1 = RuntimeData.Vertices.Add(AuthoringData.Vertices[SourceIndex1]);
		const int32 TargetIndex2 = RuntimeData.Vertices.Add(AuthoringData.Vertices[SourceIndex2]);
		RuntimeData.Indices.Add(TargetIndex0);
		RuntimeData.Indices.Add(TargetIndex1);
		RuntimeData.Indices.Add(TargetIndex2);
		RuntimeData.TriangleLayerIndices.Add(*LayerIndex);

		RuntimeData.LocalBounds += FVector(RuntimeData.Vertices[TargetIndex0]);
		RuntimeData.LocalBounds += FVector(RuntimeData.Vertices[TargetIndex1]);
		RuntimeData.LocalBounds += FVector(RuntimeData.Vertices[TargetIndex2]);
	}
}
#endif
