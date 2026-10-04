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
		int32 ClippedTriangles = 0;
		int32 RejectedTriangles = 0;
	};

	/** Resumable projection. Collision callbacks run only on the caller's thread. */
	class FProjectedShapeBuild
	{
	public:
		EShapeBuildResult Begin(TConstArrayView<FVector2D> Outline, double SampleSpacing, const FGuid& LayerId);
		bool Advance(TFunctionRef<bool(const FVector2D&, FVector&)> Project, int32 MaxQueries, double TimeBudgetSeconds = 0.0);
		bool IsFinished() const { return bFinished; }
		EShapeBuildResult GetResult() const { return Result; }
		FComposableCameraMeshSurfaceAuthoringData TakeData() { return MoveTemp(Data); }
		TConstArrayView<FVector2D> GetOutline() const { return Points; }
		TConstArrayView<int32> GetOutlineIndices() const { return OutlineIndices; }
	private:
		struct FTriangle { FVector2D A, B, C; };
		struct FSample { FVector Position = FVector::ZeroVector; bool bValid = false; };
		TArray<FVector2D> Points;
		TArray<int32> OutlineIndices;
		TArray<FTriangle> Leaves;
		TMap<FVector2D, FSample> Samples;
		FComposableCameraMeshSurfaceAuthoringData Data;
		FGuid Layer;
		FVector CurrentVertices[3] = {FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector};
		int32 LeafIndex = 0;
		int32 SampleIndex = 0;
		int32 SkippedCount = 0;
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
		FEraseGeometryStats* OutStats = nullptr, FBox2D* OutChangedBounds = nullptr);

	/** Commit-time work only. Project returns document-local coordinates. Failures preserve OutData. */
	EShapeBuildResult BuildProjectedShape(
		TConstArrayView<FVector2D> Outline,
		double SampleSpacing,
		const FGuid& LayerId,
		TFunctionRef<bool(const FVector2D&, FVector&)> Project,
		FComposableCameraMeshSurfaceAuthoringData& OutData);
}
