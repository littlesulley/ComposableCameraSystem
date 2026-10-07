// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MeshCamera/ComposableCameraMeshSurfaceTypes.h"
#include "Templates/Function.h"

struct FComposableCameraMeshSurfaceAuthoringData;
struct FComposableCameraMeshAuthoredShape;
struct FComposableCameraMeshEraseStamp;

namespace UE::ComposableCamera::MeshEditor
{
	class FMeshLayerAuthoringIndex;
	enum class EShapeBuildResult : uint8
	{
		Success,
		PartialSurface,
		InvalidOutline,
		TooComplex,
		NoSurface
	};

	struct FEraseGeometryStats
	{
		int32 ConsideredTriangles = 0;
		int32 ClippedTriangles = 0;
		int32 RejectedTriangles = 0;
		/** Safely inside the prism: removed without per-plane polygon splitting. Included in ClippedTriangles. */
		int32 InteriorTriangles = 0;
	};

	/** Same exact prism subtraction, resumable between original candidate triangles. No source may change between advances. */
	class FEraseGeometryBuild
	{
	public:
		/** False for invalid input or an empty candidate set; neither case mutates source. */
		bool Begin(const FComposableCameraMeshSurfaceAuthoringData& Data, const FGuid& LayerId,
			const FComposableCameraMeshEraseStamp& Stamp, bool bRememberForShapes, FMeshLayerAuthoringIndex* SourceIndex = nullptr);
		bool Advance(FComposableCameraMeshSurfaceAuthoringData& Data, FMeshLayerAuthoringIndex* SourceIndex = nullptr,
			int32 MaxTriangles = MAX_int32, double TimeBudgetSeconds = 0.0);
		bool HasChanged() const { return bChanged; }
		const FBox2D& GetChangedBounds() const { return ChangedBounds; }
		const FEraseGeometryStats& GetStats() const { return Stats; }
	private:
		struct FCutPlane { FVector Normal; double Limit; };
		TArray<FCutPlane, TInlineAllocator<34>> Planes;
		TArray<int32> Candidates;
		TSet<FGuid> ErasedShapes, AlreadyErasedShapes;
		FComposableCameraMeshEraseStamp EraseStamp;
		FGuid EraseLayer;
		FBox2D ChangedBounds = FBox2D(ForceInit);
		FEraseGeometryStats Stats;
		int32 OriginalTriangleCount = 0, WorkIndex = 0;
		bool bIndexed = false, bHasShapeIds = false, bRemember = false, bChanged = false, bFinished = true;
	};

	/** Resumable projection. Collision callbacks run only on the caller's thread. */
	class FProjectedShapeBuild
	{
	public:
		EShapeBuildResult Begin(TConstArrayView<FVector2D> Outline, double SampleSpacing, const FGuid& LayerId,
			int32 MaxTriangles = 16384, double SurfaceErrorTolerance = 1.0);
		bool Advance(TFunctionRef<bool(const FVector2D&, FVector&)> Project, int32 MaxQueries, double TimeBudgetSeconds = 0.0);
		bool IsFinished() const { return bFinished; }
		EShapeBuildResult GetResult() const { return Result; }
		FComposableCameraMeshSurfaceAuthoringData TakeData() { return MoveTemp(Data); }
		TConstArrayView<FVector2D> GetOutline() const { return Points; }
		TConstArrayView<int32> GetOutlineIndices() const { return OutlineIndices; }
	private:
		struct FTriangle { FVector2D A, B, C; int32 RefinementDepth = 0; };
		struct FSample { FVector Position = FVector::ZeroVector; bool bValid = false; };
		static void SplitTriangle(const FTriangle& Triangle, FTriangle& First, FTriangle& Second);
		void AppendEdgeSamples(const FVector2D& Start, const FVector2D& End, TArray<FVector, TInlineAllocator<16>>& Boundary, int32 Depth = 0) const;
		TArray<FVector2D> Points;
		TArray<int32> OutlineIndices;
		TArray<FTriangle> Leaves;
		TArray<FTriangle> Completed;
		TMap<FVector2D, FSample> Samples;
		FComposableCameraMeshSurfaceAuthoringData Data;
		FGuid Layer;
		FVector CurrentVertices[7] = {};
		int32 LeafIndex = 0;
		int32 EmissionIndex = 0;
		int32 SampleIndex = 0;
		int32 SkippedCount = 0;
		int32 ValidSampleCount = 0;
		int32 TriangleLimit = 16384;
		double ApproximationError = 1.0;
		bool bLeafValid = true;
		bool bFinished = true;
		EShapeBuildResult Result = EShapeBuildResult::InvalidOutline;
	};

	FVector2D SnapShapePoint(const FVector2D& Point, double GridSize);
	void BuildRectangleOutline(const FVector2D& Start, const FVector2D& End, TArray<FVector2D>& OutOutline);
	void BuildCircleOutline(const FVector2D& Center, double Radius, int32 Segments, TArray<FVector2D>& OutOutline);
	void BuildShapeOutline(const FComposableCameraMeshAuthoredShape& Shape, TArray<FVector2D>& OutOutline);
	FVector ShapePlanePosition(const FComposableCameraMeshAuthoredShape& Shape, const FVector2D& Point);
	void AppendShapeGeometry(FComposableCameraMeshSurfaceAuthoringData& Data, const FComposableCameraMeshSurfaceAuthoringData& Added, const FGuid& ShapeId);
	void RemoveShapeGeometry(FComposableCameraMeshSurfaceAuthoringData& Data, const FGuid& ShapeId);
	FGuid FindShapeOnRay(const FComposableCameraMeshSurfaceAuthoringData& Data, const FGuid& LayerId, const FVector& Origin, const FVector& Direction);

	/** Convex-prism subtraction, preserving interpolated heights and triangle ownership. */
	bool EraseShapeGeometry(FComposableCameraMeshSurfaceAuthoringData& Data, const FGuid& LayerId,
		const FComposableCameraMeshEraseStamp& Stamp, bool bRememberForShapes = true,
		FEraseGeometryStats* OutStats = nullptr, FBox2D* OutChangedBounds = nullptr,
		FMeshLayerAuthoringIndex* SourceIndex = nullptr);

	/** Authoring work only. Project returns output-frame coordinates; error tolerance uses those units. Failures preserve OutData. */
	EShapeBuildResult BuildProjectedShape(
		TConstArrayView<FVector2D> Outline,
		double SampleSpacing,
		const FGuid& LayerId,
		TFunctionRef<bool(const FVector2D&, FVector&)> Project,
		FComposableCameraMeshSurfaceAuthoringData& OutData, int32 MaxTriangles = 16384, double SurfaceErrorTolerance = 1.0);
}
