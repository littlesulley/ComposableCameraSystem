// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
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

	/**
	 * Finds every enabled Layer on the first surface along a local-space ray.
	 * Results follow Layer array order (top row first).
	 */
	bool QueryLocalRayLayers(
		const FVector& LocalOrigin,
		const FVector& LocalDirection,
		double MaxDistance,
		double SameSurfaceTolerance,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers,
		TArray<int32, TInlineAllocator<16>>& OutLayerIndices,
		FVector& OutLocalSurfacePosition,
		double& OutRayDistance) const;
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

	UPROPERTY(BlueprintReadOnly, Category = "Mesh Camera")
	FVector SurfacePosition = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Mesh Camera")
	double VerticalDistance = 0.0;

	/** Runtime identity. Layer GUIDs can repeat across instanced storage actors. */
	UPROPERTY(Transient)
	TObjectPtr<AComposableCameraMeshSurfaceStorageActor> StorageActor;
};

/** Inline storage covers normal nested-room depth without per-frame allocation. */
using FComposableCameraMeshLayerQueryResults =
	TArray<FComposableCameraMeshLayerQueryResult, TInlineAllocator<16>>;
