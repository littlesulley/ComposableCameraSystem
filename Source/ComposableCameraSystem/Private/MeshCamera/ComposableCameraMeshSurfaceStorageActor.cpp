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

void AComposableCameraMeshSurfaceStorageActor::PostLoad()
{
	Super::PostLoad();
	RuntimeData.RebuildSpatialIndex();
}

void AComposableCameraMeshSurfaceStorageActor::RebuildQueryResources(bool bRebuildSpatialIndex)
{
	if (bRebuildSpatialIndex) { RuntimeData.RebuildSpatialIndex(); }
	if (HasActorBegunPlay())
	{
		if (UWorld* World = GetWorld())
		{
			if (UComposableCameraMeshWorldSubsystem* Subsystem = World->GetSubsystem<UComposableCameraMeshWorldSubsystem>())
			{
				Subsystem->RegisterStorageActor(this);
			}
		}
	}
}

#if WITH_EDITOR
void AComposableCameraMeshSurfaceStorageActor::PostEditUndo()
{
	Super::PostEditUndo();
#if WITH_EDITORONLY_DATA
	++EditorDataRevision;
#endif
	RebuildQueryResources();
}

void AComposableCameraMeshSurfaceStorageActor::PostEditUndo(TSharedPtr<ITransactionObjectAnnotation> TransactionAnnotation)
{
	Super::PostEditUndo(TransactionAnnotation);
#if WITH_EDITORONLY_DATA
	++EditorDataRevision;
#endif
	RebuildQueryResources();
}
#endif

void AComposableCameraMeshSurfaceStorageActor::BeginPlay()
{
	Super::BeginPlay();
	if (!RuntimeData.HasSpatialIndex()) RuntimeData.RebuildSpatialIndex();

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
	TArray<double, TInlineAllocator<16>> LayerRayDistances;
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
		LocalRayDistance,
		nullptr,
		&LayerRayDistances))
	{
		return false;
	}

	for (int32 ResultIndex = 0; ResultIndex < LayerIndices.Num(); ++ResultIndex)
	{
		const int32 LayerIndex = LayerIndices[ResultIndex];
		const FVector WorldSurfacePosition = ActorTransform.TransformPosition(
			LocalOrigin + LocalDirection * LayerRayDistances[ResultIndex]);
		const FComposableCameraMeshLayerDefinition& Layer = LayerDefinitions[LayerIndex];
		FComposableCameraMeshLayerQueryResult& Result = OutResults.AddDefaulted_GetRef();
		Result.LayerId = Layer.LayerId;
		Result.LayerName = Layer.Name;
		Result.LayerOrder = LayerIndex;
		Result.Profile = Layer.Profile;
		Result.SurfacePosition = WorldSurfacePosition;
		Result.VerticalDistance = FVector::Distance(WorldPosition, WorldSurfacePosition);
		Result.StorageActor = const_cast<AComposableCameraMeshSurfaceStorageActor*>(this);
	}
	return !OutResults.IsEmpty();
}

#if WITH_EDITORONLY_DATA
void AComposableCameraMeshSurfaceStorageActor::SetAuthoringData(
	const TArray<FComposableCameraMeshLayerDefinition>& InLayers,
	const FComposableCameraMeshSurfaceAuthoringData& InAuthoringData)
{
	// Compare once at an explicit mutation boundary, never in Query/Tick. Shape controls/ownership do not affect BVH.
	const bool bSameGeometry = AuthoringData.Vertices == InAuthoringData.Vertices
		&& AuthoringData.Indices == InAuthoringData.Indices && AuthoringData.TriangleLayerIds == InAuthoringData.TriangleLayerIds;
	bool bSameRows = LayerDefinitions.Num() == InLayers.Num(), bSameCoveragePolicy = bSameRows;
	for (int32 Index = 0; bSameRows && Index < InLayers.Num(); ++Index)
	{
		bSameRows = LayerDefinitions[Index].LayerId == InLayers[Index].LayerId;
		bSameCoveragePolicy &= bSameRows && LayerDefinitions[Index].bEnabled == InLayers[Index].bEnabled;
	}
	const bool bReuseQuery = InAuthoringData.IsConsistent() && bSameGeometry && bSameRows && (RuntimeData.HasSpatialIndex()
		|| (AuthoringData.Indices.IsEmpty() && RuntimeData.Indices.IsEmpty() && RuntimeData.TriangleLayerIndices.IsEmpty()));
	LayerDefinitions = InLayers;
	if (bSameGeometry)
	{
		AuthoringData.TriangleShapeIds = InAuthoringData.TriangleShapeIds;
		AuthoringData.Shapes = InAuthoringData.Shapes;
	}
	else { AuthoringData = InAuthoringData; }
	if (bReuseQuery)
	{
		++EditorDataRevision;
		if (!bSameCoveragePolicy) { EditorPreview = FComposableCameraMeshSurfaceEditorPreview(); }
		RebuildQueryResources(false); // Retain registration semantics for editor/PIE callers.
	}
	else { RebuildRuntimeData(); }
}

void AComposableCameraMeshSurfaceStorageActor::RebuildRuntimeData()
{
	++EditorDataRevision;
	EditorPreview = FComposableCameraMeshSurfaceEditorPreview();
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
	TArray<int32> VertexRemap; VertexRemap.Init(INDEX_NONE, AuthoringData.Vertices.Num());

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

		// Keep source sharing. Expanding every corner tripled dense indexed meshes in saved/cooked packages.
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const int32 SourceVertex = AuthoringData.Indices[SourceOffset + Corner];
			int32& TargetVertex = VertexRemap[SourceVertex];
			if (TargetVertex == INDEX_NONE) { TargetVertex = RuntimeData.Vertices.Add(AuthoringData.Vertices[SourceVertex]); }
			RuntimeData.Indices.Add(TargetVertex);
		}
		RuntimeData.TriangleLayerIndices.Add(*LayerIndex);

	}
	RebuildQueryResources();
}
#endif
