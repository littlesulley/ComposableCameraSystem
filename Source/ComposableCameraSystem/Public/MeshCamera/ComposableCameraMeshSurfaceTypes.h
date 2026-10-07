// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "ComposableCameraMeshSurfaceTypes.generated.h"

class UComposableCameraMeshProfile;
class AComposableCameraMeshSurfaceStorageActor;

USTRUCT(BlueprintType)
struct COMPOSABLECAMERASYSTEM_API FComposableCameraMeshLayerDefinition
{
	GENERATED_BODY()

	/** Stable authoring identity. Generated and maintained by the mesh-layer tool. */
	UPROPERTY()
	FGuid LayerId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	FName Name = TEXT("Mesh Layer");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	TObjectPtr<UComposableCameraMeshProfile> Profile;

	/** Channel used to pick and project this Layer's authoring surface. Existing geometry is not reprojected on change. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer", meta = (DisplayName = "Channel"))
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	FLinearColor DebugColor = FLinearColor(0.1f, 0.65f, 1.0f, 0.5f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	bool bEnabled = true;
};

/** Cooked, replaceable query output. It is not a StaticMesh or collision asset. */
USTRUCT()
struct COMPOSABLECAMERASYSTEM_API FComposableCameraMeshSurfaceRuntimeData
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FVector3f> Vertices;

	UPROPERTY()
	TArray<int32> Indices;

	/** One layer-array index per triangle. */
	UPROPERTY()
	TArray<int32> TriangleLayerIndices;

	UPROPERTY()
	FBox LocalBounds = FBox(ForceInit);

	void Reset();
	bool IsConsistent() const;

	/** Build disposable acceleration after geometry changes, never during query. */
	void RebuildSpatialIndex();
	bool HasSpatialIndex() const;

	/**
	 * Finds every enabled Layer on the first surface along a local-space ray.
	 * Results follow Layer array order (top row first).
	 * Optional OutTriangleTests counts exact predicates for pruning diagnostics.
	 * Optional OutLayerRayDistances reports each Layer's own nearest hit, aligned with OutLayerIndices.
	 */
	bool QueryLocalRayLayers(
		const FVector& LocalOrigin,
		const FVector& LocalDirection,
		double MaxDistance,
		double SameSurfaceTolerance,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers,
		TArray<int32, TInlineAllocator<16>>& OutLayerIndices,
		FVector& OutLocalSurfacePosition,
		double& OutRayDistance,
		int32* OutTriangleTests = nullptr,
		TArray<double, TInlineAllocator<16>>* OutLayerRayDistances = nullptr) const;

private:
	struct FSpatialNode
	{
		FBox Bounds = FBox(ForceInit);
		int32 FirstTriangle = 0;
		int32 NumTriangles = 0;
		/** Preorder index immediately after this subtree; enables stackless query. */
		int32 EscapeIndex = 0;
	};

	int32 BuildSpatialNode(int32 FirstTriangle, int32 NumTriangles,
		TConstArrayView<FBox> TriangleBounds);

	// Native-only caches. Existing saved/cooked triangle fields remain unchanged.
	TArray<FSpatialNode> SpatialNodes;
	TArray<int32> SpatialTriangleIndices;
	int32 IndexedVertexCount = 0;
	int32 IndexedIndexCount = 0;
	int32 IndexedLayerIndexCount = 0;
};

UENUM()
enum class EComposableCameraMeshShapeType : uint8
{
	Rectangle,
	Circle,
	Polygon
};

/** Document-local affine coordinates for a bounded circular eraser. */
USTRUCT()
struct COMPOSABLECAMERASYSTEM_API FComposableCameraMeshEraseStamp
{
	GENERATED_BODY()

	UPROPERTY() FVector Center = FVector::ZeroVector;
	UPROPERTY() FVector AxisX = FVector::ForwardVector;
	UPROPERTY() FVector AxisY = FVector::RightVector;
	UPROPERTY() FVector AxisZ = FVector::UpVector;
	UPROPERTY() double Radius = 150.0;
	UPROPERTY() double Depth = 100.0;
};

/** Retained editor source; rectangle/circle use two controls, polygons use vertices. */
USTRUCT()
struct COMPOSABLECAMERASYSTEM_API FComposableCameraMeshAuthoredShape
{
	GENERATED_BODY()

	UPROPERTY() FGuid ShapeId;
	UPROPERTY() FGuid LayerId;
	UPROPERTY() EComposableCameraMeshShapeType Type = EComposableCameraMeshShapeType::Rectangle;
	UPROPERTY() TArray<FVector2D> ControlPoints;
	UPROPERTY() FVector PlaneOrigin = FVector::ZeroVector;
	UPROPERTY() FVector PlaneNormal = FVector::UpVector;
	UPROPERTY() double SampleSpacing = 100.0;
	UPROPERTY() double ProjectionDistance = 100.0;
	UPROPERTY() double MinimumFloorNormalZ = 0.25;
	UPROPERTY() int32 CircleSegments = 64;
	/** Reapplied when controls change, so editing never resurrects erased coverage. */
	UPROPERTY() TArray<FComposableCameraMeshEraseStamp> Erasures;
};

/** Full-fidelity tool source. Future simplification must never overwrite it. */
USTRUCT()
struct COMPOSABLECAMERASYSTEM_API FComposableCameraMeshSurfaceAuthoringData
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FVector3f> Vertices;

	UPROPERTY()
	TArray<int32> Indices;

	/** Stable layer GUID per triangle. */
	UPROPERTY()
	TArray<FGuid> TriangleLayerIds;

#if WITH_EDITORONLY_DATA
	/** Empty in legacy documents; invalid GUID denotes freehand brush geometry. */
	UPROPERTY()
	TArray<FGuid> TriangleShapeIds;

	UPROPERTY()
	TArray<FComposableCameraMeshAuthoredShape> Shapes;
#endif

	void Reset()
	{
		Vertices.Reset();
		Indices.Reset();
		TriangleLayerIds.Reset();
#if WITH_EDITORONLY_DATA
		TriangleShapeIds.Reset();
		Shapes.Reset();
#endif
	}

	bool IsConsistent() const
	{
		return Indices.Num() % 3 == 0
			&& TriangleLayerIds.Num() == Indices.Num() / 3
#if WITH_EDITORONLY_DATA
			&& (TriangleShapeIds.IsEmpty() || TriangleShapeIds.Num() == TriangleLayerIds.Num())
#endif
			;
	}
};

/** Editor-only derived polygons. GUIDs keep metadata/color independent of geometry. */
USTRUCT()
struct COMPOSABLECAMERASYSTEM_API FComposableCameraMeshPreviewPatch
{
	GENERATED_BODY()
	UPROPERTY() FGuid LayerId;
	UPROPERTY() FVector Normal = FVector::UpVector;
	UPROPERTY() TArray<FVector> Vertices;
};

USTRUCT()
struct COMPOSABLECAMERASYSTEM_API FComposableCameraMeshPreviewCell
{
	GENERATED_BODY()
	UPROPERTY() FVector Position = FVector::ZeroVector;
	UPROPERTY() FVector Normal = FVector::UpVector;
	UPROPERTY() FGuid LayerId;
	UPROPERTY() bool bFullCoverage = false;
	UPROPERTY() TArray<FComposableCameraMeshPreviewPatch> Patches;
};

/** Stored only in the actor's WITH_EDITORONLY_DATA property; legacy Levels have Version=0. */
USTRUCT()
struct COMPOSABLECAMERASYSTEM_API FComposableCameraMeshSurfaceEditorPreview
{
	GENERATED_BODY()
	UPROPERTY() int32 Version = 0;
	UPROPERTY() double CellSize = 10.0;
	UPROPERTY() FBox2D Bounds = FBox2D(ForceInit);
	UPROPERTY() TArray<FComposableCameraMeshPreviewCell> Cells;
};

/** Matches saved Layer geometry to a ground hit supplied by business code. Does not trace the scene. */
USTRUCT(BlueprintType)
struct COMPOSABLECAMERASYSTEM_API FComposableCameraMeshGroundQueryParams
{
	GENERATED_BODY()

	/** Maximum absolute world-Z separation between GroundHit.ImpactPoint and each Layer's exact triangle hit, in cm. Zero requires an exact height match. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Camera", meta = (ClampMin = "0.0", Units = "cm"))
	double SurfaceTolerance = 5.0;
};

USTRUCT(BlueprintType)
struct COMPOSABLECAMERASYSTEM_API FComposableCameraMeshLayerQueryResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Mesh Camera")
	FGuid LayerId;

	UPROPERTY(BlueprintReadOnly, Category = "Mesh Camera")
	FName LayerName = NAME_None;

	/** Zero-based order in the Mesh Camera Layers list. Lower draws above higher. */
	UPROPERTY(BlueprintReadOnly, Category = "Mesh Camera")
	int32 LayerOrder = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Mesh Camera")
	TObjectPtr<UComposableCameraMeshProfile> Profile;

	/** This Layer's own saved triangle intersection at the supplied ground XY. Never replaced by GroundHit.ImpactPoint. */
	UPROPERTY(BlueprintReadOnly, Category = "Mesh Camera")
	FVector SurfacePosition = FVector::ZeroVector;

	/** Absolute world-Z separation from GroundHit.ImpactPoint, in cm. Low-level storage ray queries report distance from their ray origin. */
	UPROPERTY(BlueprintReadOnly, Category = "Mesh Camera")
	double VerticalDistance = 0.0;

	/** Runtime identity. Layer GUIDs can repeat across instanced storage actors. */
	UPROPERTY(Transient)
	TObjectPtr<AComposableCameraMeshSurfaceStorageActor> StorageActor;
};

/** Inline storage covers normal nested-room depth without per-frame allocation. */
using FComposableCameraMeshLayerQueryResults =
	TArray<FComposableCameraMeshLayerQueryResult, TInlineAllocator<16>>;
