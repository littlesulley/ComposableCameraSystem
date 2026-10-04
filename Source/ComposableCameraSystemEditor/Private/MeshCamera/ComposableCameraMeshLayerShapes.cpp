// Copyright 2026 Sulley. All Rights Reserved.

#include "MeshCamera/ComposableCameraMeshLayerShapes.h"
#include "HAL/PlatformTime.h"

#include "Algo/Reverse.h"
#include "CompGeom/PolygonTriangulation.h"
#include "MeshCamera/ComposableCameraMeshSurfaceTypes.h"

namespace UE::ComposableCamera::MeshEditor
{
	namespace
	{
		constexpr int32 MaxOutlinePoints = 256;
		constexpr int32 MaxShapeTriangles = 16384;
		constexpr double PointTolerance = 0.001;

		bool HasValidTriangle(const FComposableCameraMeshSurfaceAuthoringData& Data, int32 Triangle)
		{
			const int32 Offset = Triangle * 3;
			return Data.Indices.IsValidIndex(Offset + 2)
				&& Data.Vertices.IsValidIndex(Data.Indices[Offset])
				&& Data.Vertices.IsValidIndex(Data.Indices[Offset + 1])
				&& Data.Vertices.IsValidIndex(Data.Indices[Offset + 2]);
		}

		double Cross2D(const FVector2D& A, const FVector2D& B) { return A.X * B.Y - A.Y * B.X; }

		bool OnSegment(const FVector2D& A, const FVector2D& B, const FVector2D& P)
		{
			return FMath::Abs(Cross2D(B - A, P - A)) <= PointTolerance
				&& P.X >= FMath::Min(A.X, B.X) - PointTolerance && P.X <= FMath::Max(A.X, B.X) + PointTolerance
				&& P.Y >= FMath::Min(A.Y, B.Y) - PointTolerance && P.Y <= FMath::Max(A.Y, B.Y) + PointTolerance;
		}

		bool EdgesIntersect(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D)
		{
			const double AB_C = Cross2D(B - A, C - A), AB_D = Cross2D(B - A, D - A);
			const double CD_A = Cross2D(D - C, A - C), CD_B = Cross2D(D - C, B - C);
			return (AB_C * AB_D < 0.0 && CD_A * CD_B < 0.0)
				|| OnSegment(A, B, C) || OnSegment(A, B, D) || OnSegment(C, D, A) || OnSegment(C, D, B);
		}

		bool PrepareOutline(TConstArrayView<FVector2D> Input, TArray<FVector2D>& Points)
		{
			if (Input.Num() < 3 || Input.Num() > MaxOutlinePoints) { return false; }
			Points.Append(Input.GetData(), Input.Num());
			for (const FVector2D& Point : Points) { if (Point.ContainsNaN()) { return false; } }
			// Remove redundant straight-line vertices. Backtracking and touching edges remain invalid.
			for (int32 Index = 0; Index < Points.Num() && Points.Num() >= 3;)
			{
				const FVector2D Prev = Points[(Index + Points.Num() - 1) % Points.Num()];
				const FVector2D Next = Points[(Index + 1) % Points.Num()];
				if (Points[Index].Equals(Prev, PointTolerance)) { return false; }
				if (OnSegment(Prev, Next, Points[Index]))
				{
					Points.RemoveAt(Index);
					Index = 0;
				}
				else { ++Index; }
			}
			if (Points.Num() < 3) { return false; }
			double Area2 = 0.0;
			for (int32 Index = 0; Index < Points.Num(); ++Index)
			{
				const int32 Next = (Index + 1) % Points.Num();
				if (Points[Index].Equals(Points[Next], PointTolerance)) { return false; }
				Area2 += Cross2D(Points[Index] - Points[0], Points[Next] - Points[0]);
				for (int32 Other = Index + 1; Other < Points.Num(); ++Other)
				{
					const int32 OtherNext = (Other + 1) % Points.Num();
					if (Next == Other || OtherNext == Index) { continue; }
					if (EdgesIntersect(Points[Index], Points[Next], Points[Other], Points[OtherNext])) { return false; }
				}
			}
			if (!FMath::IsFinite(Area2) || FMath::Abs(Area2) <= PointTolerance) { return false; }
			if (Area2 < 0.0) { Algo::Reverse(Points); }
			return true;
		}
	}

	void BuildShapeOutline(const FComposableCameraMeshAuthoredShape& Shape, TArray<FVector2D>& OutOutline)
	{
		OutOutline.Reset();
		switch (Shape.Type)
		{
		case EComposableCameraMeshShapeType::Rectangle:
			if (Shape.ControlPoints.Num() == 2) { BuildRectangleOutline(Shape.ControlPoints[0], Shape.ControlPoints[1], OutOutline); }
			break;
		case EComposableCameraMeshShapeType::Circle:
			if (Shape.ControlPoints.Num() == 2) { BuildCircleOutline(Shape.ControlPoints[0], (Shape.ControlPoints[1] - Shape.ControlPoints[0]).Size(), Shape.CircleSegments, OutOutline); }
			break;
		case EComposableCameraMeshShapeType::Polygon:
			OutOutline = Shape.ControlPoints;
			break;
		}
	}

	FVector ShapePlanePosition(const FComposableCameraMeshAuthoredShape& Shape, const FVector2D& Point)
	{
		FVector Position(Point.X, Point.Y, Shape.PlaneOrigin.Z);
		if (FMath::Abs(Shape.PlaneNormal.Z) > UE_DOUBLE_SMALL_NUMBER)
		{
			Position.Z += FVector::DotProduct(Shape.PlaneOrigin - Position, Shape.PlaneNormal) / Shape.PlaneNormal.Z;
		}
		return Position;
	}

	void AppendShapeGeometry(FComposableCameraMeshSurfaceAuthoringData& Data, const FComposableCameraMeshSurfaceAuthoringData& Added, const FGuid& ShapeId)
	{
		Data.TriangleShapeIds.SetNum(Data.TriangleLayerIds.Num());
		const int32 Base = Data.Vertices.Num();
		Data.Vertices.Append(Added.Vertices);
		for (int32 Index : Added.Indices) { Data.Indices.Add(Base + Index); }
		Data.TriangleLayerIds.Append(Added.TriangleLayerIds);
		for (int32 Index = 0; Index < Added.TriangleLayerIds.Num(); ++Index) { Data.TriangleShapeIds.Add(ShapeId); }
	}

	void RemoveShapeGeometry(FComposableCameraMeshSurfaceAuthoringData& Data, const FGuid& ShapeId)
	{
		FComposableCameraMeshSurfaceAuthoringData Remaining;
		Remaining.Shapes = Data.Shapes;
		for (int32 Triangle = 0; Triangle < Data.TriangleLayerIds.Num(); ++Triangle)
		{
			if (!HasValidTriangle(Data, Triangle)) { continue; }
			const FGuid Owner = Data.TriangleShapeIds.IsValidIndex(Triangle) ? Data.TriangleShapeIds[Triangle] : FGuid();
			if (Owner == ShapeId) { continue; }
			const int32 Offset = Triangle * 3;
			const int32 Base = Remaining.Vertices.Num();
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				Remaining.Vertices.Add(Data.Vertices[Data.Indices[Offset + Corner]]);
				Remaining.Indices.Add(Base + Corner);
			}
			Remaining.TriangleLayerIds.Add(Data.TriangleLayerIds[Triangle]);
			Remaining.TriangleShapeIds.Add(Owner);
		}
		Data = MoveTemp(Remaining);
	}

	FGuid FindShapeOnRay(const FComposableCameraMeshSurfaceAuthoringData& Data, const FGuid& LayerId, const FVector& Origin, const FVector& Direction)
	{
		FGuid Result;
		double ClosestDistance = UE_DOUBLE_BIG_NUMBER;
		int32 SelectedShapeOrder = INDEX_NONE;
		for (int32 Triangle = Data.TriangleLayerIds.Num() - 1; Triangle >= 0; --Triangle)
		{
			if (!HasValidTriangle(Data, Triangle)) { continue; }
			if (Data.TriangleLayerIds[Triangle] != LayerId || !Data.TriangleShapeIds.IsValidIndex(Triangle) || !Data.TriangleShapeIds[Triangle].IsValid()) { continue; }
			const int32 Offset = Triangle * 3;
			FVector Intersection, Normal;
			if (FMath::SegmentTriangleIntersection(Origin, Origin + Direction * HALF_WORLD_MAX,
				FVector(Data.Vertices[Data.Indices[Offset]]), FVector(Data.Vertices[Data.Indices[Offset + 1]]), FVector(Data.Vertices[Data.Indices[Offset + 2]]), Intersection, Normal))
			{
				const double Distance = FVector::Distance(Origin, Intersection);
				const FGuid Owner = Data.TriangleShapeIds[Triangle];
				const int32 ShapeOrder = Data.Shapes.IndexOfByPredicate([&Owner](const auto& Shape) { return Shape.ShapeId == Owner; });
				// Same-surface picking follows retained Shape order, independent of
				// triangle swap-removal or erase retessellation.
				if (Distance < ClosestDistance - PointTolerance
					|| (FMath::Abs(Distance - ClosestDistance) <= PointTolerance && ShapeOrder > SelectedShapeOrder))
				{
					ClosestDistance = Distance;
					Result = Owner;
					SelectedShapeOrder = ShapeOrder;
				}
			}
		}
		return Result;
	}

	bool EraseShapeGeometry(FComposableCameraMeshSurfaceAuthoringData& Data, const FGuid& LayerId,
		const FComposableCameraMeshEraseStamp& Stamp, bool bRememberForShapes, FEraseGeometryStats* OutStats, FBox2D* OutChangedBounds)
	{
		if (OutStats) { *OutStats = {}; }
		if (OutChangedBounds) { *OutChangedBounds = FBox2D(ForceInit); }
		if (!Data.IsConsistent() || Stamp.Radius <= 0.0 || Stamp.Depth <= 0.0) { return false; }
		// Subtract a 32-sided circular prism. Split in local 3D, preserving height.
		struct FCutPlane { FVector Normal; double Limit; };
		TArray<FCutPlane, TInlineAllocator<34>> Planes;
		Planes.Add({Stamp.AxisZ, Stamp.Depth});
		Planes.Add({-Stamp.AxisZ, Stamp.Depth});
		for (int32 Side = 0; Side < 32; ++Side)
		{
			const double Angle = (Side + 0.5) * 2.0 * UE_DOUBLE_PI / 32;
			Planes.Add({Stamp.AxisX * FMath::Cos(Angle) + Stamp.AxisY * FMath::Sin(Angle), Stamp.Radius * FMath::Cos(UE_DOUBLE_PI / 32)});
		}
		const bool bHasShapeIds = !Data.TriangleShapeIds.IsEmpty();
		TSet<FGuid> ErasedShapes;
		TSet<FGuid> AlreadyErasedShapes;
		if (bRememberForShapes)
		{
			for (const auto& Shape : Data.Shapes)
			{
				if (Shape.Erasures.ContainsByPredicate([&Stamp](const auto& Old)
				{
					return Old.Center.Equals(Stamp.Center, 1.e-6) && Old.AxisX.Equals(Stamp.AxisX, 1.e-6)
						&& Old.AxisY.Equals(Stamp.AxisY, 1.e-6) && Old.AxisZ.Equals(Stamp.AxisZ, 1.e-6)
						&& FMath::IsNearlyEqual(Old.Radius, Stamp.Radius, 1.e-6) && FMath::IsNearlyEqual(Old.Depth, Stamp.Depth, 1.e-6);
				})) { AlreadyErasedShapes.Add(Shape.ShapeId); }
			}
		}
		bool bChanged = false;
		auto Emit = [&Data, bHasShapeIds](TConstArrayView<FVector> Polygon, const FGuid& Layer, const FGuid& Owner)
		{
			for (int32 Index = 1; Index + 1 < Polygon.Num(); ++Index)
			{
				if (FVector::CrossProduct(Polygon[Index] - Polygon[0], Polygon[Index + 1] - Polygon[0]).SizeSquared() < 1.e-12) { continue; }
				const int32 Base = Data.Vertices.Num();
				Data.Vertices.Add(FVector3f(Polygon[0])); Data.Vertices.Add(FVector3f(Polygon[Index])); Data.Vertices.Add(FVector3f(Polygon[Index + 1]));
				Data.Indices.Append({Base, Base + 1, Base + 2});
				Data.TriangleLayerIds.Add(Layer);
				if (bHasShapeIds) { Data.TriangleShapeIds.Add(Owner); }
			}
		};
		// Descending source indices: swap-removal can move only an already processed
		// triangle or a new fragment into the current slot. Never recut new fragments.
		for (int32 Triangle = Data.TriangleLayerIds.Num() - 1; Triangle >= 0; --Triangle)
		{
			if (!HasValidTriangle(Data, Triangle)) { continue; }
			const FGuid Owner = Data.TriangleShapeIds.IsValidIndex(Triangle) ? Data.TriangleShapeIds[Triangle] : FGuid();
			const int32 Offset = Triangle * 3;
			const FVector Original[] = {FVector(Data.Vertices[Data.Indices[Offset]]), FVector(Data.Vertices[Data.Indices[Offset + 1]]), FVector(Data.Vertices[Data.Indices[Offset + 2]])};
			const FGuid Layer = Data.TriangleLayerIds[Triangle];
			bool bOutside = Layer != LayerId || AlreadyErasedShapes.Contains(Owner);
			// Cheap brush-space bounds reject distant triangles before polygon allocation.
			for (int32 AxisIndex = 0; AxisIndex < 3 && !bOutside; ++AxisIndex)
			{
				const FVector Axis = AxisIndex == 0 ? Stamp.AxisX : AxisIndex == 1 ? Stamp.AxisY : Stamp.AxisZ;
				const double Limit = AxisIndex == 2 ? Stamp.Depth : Stamp.Radius;
				const double A = FVector::DotProduct(Original[0] - Stamp.Center, Axis);
				const double B = FVector::DotProduct(Original[1] - Stamp.Center, Axis);
				const double C = FVector::DotProduct(Original[2] - Stamp.Center, Axis);
				bOutside = FMath::Min3(A, B, C) > Limit || FMath::Max3(A, B, C) < -Limit;
			}
			if (bOutside)
			{
				if (OutStats) { ++OutStats->RejectedTriangles; }
				continue;
			}
			if (OutStats) { ++OutStats->ClippedTriangles; }
			// A triangle clipped by 34 planes has at most 37 vertices. Inline scratch
			// avoids the old heap allocation on every plane of every candidate triangle.
			using FCutPolygon = TArray<FVector, TInlineAllocator<40>>;
			FCutPolygon Inside;
			Inside.Append(Original, UE_ARRAY_COUNT(Original));
			TArray<FCutPolygon, TInlineAllocator<34>> OutsidePieces;
			for (const FCutPlane& Plane : Planes)
			{
				if (Inside.Num() < 3) { break; }
				FCutPolygon NextInside, Outside;
				for (int32 Index = 0; Index < Inside.Num(); ++Index)
				{
					const FVector A = Inside[Index], B = Inside[(Index + 1) % Inside.Num()];
					const double DA = FVector::DotProduct(A - Stamp.Center, Plane.Normal) - Plane.Limit;
					const double DB = FVector::DotProduct(B - Stamp.Center, Plane.Normal) - Plane.Limit;
					if (DA <= 0.0) { NextInside.Add(A); } else { Outside.Add(A); }
					if ((DA <= 0.0) != (DB <= 0.0))
					{
						const FVector Intersection = FMath::Lerp(A, B, DA / (DA - DB));
						NextInside.Add(Intersection); Outside.Add(Intersection);
					}
				}
				if (Outside.Num() >= 3) { OutsidePieces.Add(MoveTemp(Outside)); }
				Inside = MoveTemp(NextInside);
			}
			double RemovedArea = 0.0;
			for (int32 Index = 1; Index + 1 < Inside.Num(); ++Index)
			{
				RemovedArea += FVector::CrossProduct(Inside[Index] - Inside[0], Inside[Index + 1] - Inside[0]).Size();
			}
			if (RemovedArea <= 1.e-6) { continue; }
			bChanged = true;
			if (OutChangedBounds)
			{
				// Outside the removed polygon, coverage is unchanged even if its source
				// triangulation differs. Keep remote cached partitions instead of recutting them.
				for (const FVector& Point : Inside) { *OutChangedBounds += FVector2D(Point.X, Point.Y); }
			}
			if (Owner.IsValid()) { ErasedShapes.Add(Owner); }
			Data.Indices.RemoveAtSwap(Offset, 3, EAllowShrinking::No);
			Data.TriangleLayerIds.RemoveAtSwap(Triangle, 1, EAllowShrinking::No);
			if (bHasShapeIds) { Data.TriangleShapeIds.RemoveAtSwap(Triangle, 1, EAllowShrinking::No); }
			for (const FCutPolygon& Piece : OutsidePieces) { Emit(Piece, Layer, Owner); }
		}
		if (!bChanged) { return false; }
		// Retain untouched vertices/triangles and Shape buffers. Save's existing
		// orphan cleanup compacts unused vertices, outside interactive brush work.
		if (Data.Indices.IsEmpty()) { Data.Vertices.Reset(); }
		if (bRememberForShapes)
		{
			for (FComposableCameraMeshAuthoredShape& Shape : Data.Shapes)
			{
				if (ErasedShapes.Contains(Shape.ShapeId)) { Shape.Erasures.Add(Stamp); }
			}
		}
		return true;
	}

	FVector2D SnapShapePoint(const FVector2D& Point, double GridSize)
	{
		return FMath::IsFinite(GridSize) && GridSize > 0.0
			? FVector2D(FMath::GridSnap(Point.X, GridSize), FMath::GridSnap(Point.Y, GridSize)) : Point;
	}

	void BuildRectangleOutline(const FVector2D& Start, const FVector2D& End, TArray<FVector2D>& OutOutline)
	{
		OutOutline.Reset(4);
		OutOutline.Add(Start);
		OutOutline.Add(FVector2D(End.X, Start.Y));
		OutOutline.Add(End);
		OutOutline.Add(FVector2D(Start.X, End.Y));
	}

	void BuildCircleOutline(const FVector2D& Center, double Radius, int32 Segments, TArray<FVector2D>& OutOutline)
	{
		Segments = FMath::Clamp(Segments, 12, 128);
		OutOutline.Reset(Segments);
		for (int32 Index = 0; Index < Segments; ++Index)
		{
			const double Angle = 2.0 * UE_DOUBLE_PI * Index / Segments;
			OutOutline.Add(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
		}
	}

	EShapeBuildResult FProjectedShapeBuild::Begin(TConstArrayView<FVector2D> Outline, double SampleSpacing, const FGuid& LayerId)
	{
		Points.Reset(); OutlineIndices.Reset(); Leaves.Reset(); Samples.Reset(); Data.Reset();
		LeafIndex = SampleIndex = SkippedCount = 0;
		bLeafValid = true; bFinished = true;
		Result = EShapeBuildResult::InvalidOutline;
		Layer = LayerId;
		if (!LayerId.IsValid() || !FMath::IsFinite(SampleSpacing) || SampleSpacing < 10.0 || !PrepareOutline(Outline, Points))
		{
			return EShapeBuildResult::InvalidOutline;
		}
		TArray<UE::Geometry::FIndex3i> Triangles;
		PolygonTriangulation::TriangulateSimplePolygon(Points, Triangles, false);
		if (Triangles.Num() != Points.Num() - 2) { return EShapeBuildResult::InvalidOutline; }
		TArray<FTriangle> Pending;
		for (const UE::Geometry::FIndex3i& Triangle : Triangles)
		{
			const FTriangle Candidate{Points[Triangle.A], Points[Triangle.B], Points[Triangle.C]};
			if (Cross2D(Candidate.B - Candidate.A, Candidate.C - Candidate.A) <= 0.0) { return EShapeBuildResult::InvalidOutline; }
			Pending.Add(Candidate);
			OutlineIndices.Append({Triangle.A, Triangle.B, Triangle.C});
		}
		const double MaxEdgeSquared = FMath::Square(SampleSpacing);
		while (!Pending.IsEmpty())
		{
			const FTriangle Triangle = Pending.Pop(EAllowShrinking::No);
			const double AB = (Triangle.B - Triangle.A).SizeSquared();
			const double BC = (Triangle.C - Triangle.B).SizeSquared();
			const double CA = (Triangle.A - Triangle.C).SizeSquared();
			if (FMath::Max3(AB, BC, CA) > MaxEdgeSquared)
			{
				if (AB >= BC && AB >= CA)
				{
					const FVector2D Mid = (Triangle.A + Triangle.B) * 0.5;
					Pending.Add({Triangle.A, Mid, Triangle.C}); Pending.Add({Mid, Triangle.B, Triangle.C});
				}
				else if (BC >= CA)
				{
					const FVector2D Mid = (Triangle.B + Triangle.C) * 0.5;
					Pending.Add({Triangle.B, Mid, Triangle.A}); Pending.Add({Mid, Triangle.C, Triangle.A});
				}
				else
				{
					const FVector2D Mid = (Triangle.C + Triangle.A) * 0.5;
					Pending.Add({Triangle.C, Mid, Triangle.B}); Pending.Add({Mid, Triangle.A, Triangle.B});
				}
			}
			else
			{
				Leaves.Add(Triangle);
			}
			if (Leaves.Num() + Pending.Num() > MaxShapeTriangles) { Result = EShapeBuildResult::TooComplex; return Result; }
		}
		// Density is bounded before the first collision query; release creates only
		// the small outline preview. Projection resumes under a per-frame budget.
		Data.Vertices.Reserve(Leaves.Num() * 3); Data.Indices.Reserve(Leaves.Num() * 3);
		Data.TriangleLayerIds.Reserve(Leaves.Num()); Samples.Reserve(Leaves.Num() * 3);
		Result = EShapeBuildResult::Success; bFinished = false;
		return Result;
	}

	bool FProjectedShapeBuild::Advance(TFunctionRef<bool(const FVector2D&, FVector&)> Project, int32 MaxQueries, double TimeBudgetSeconds)
	{
		if (bFinished || MaxQueries <= 0) { return bFinished; }
		const double Started = FPlatformTime::Seconds();
		int32 Queries = 0;
		while (LeafIndex < Leaves.Num())
		{
			if (TimeBudgetSeconds > 0.0 && FPlatformTime::Seconds() - Started >= TimeBudgetSeconds) { return false; }
			const FTriangle& Triangle = Leaves[LeafIndex];
			const FVector2D Locations[] = {Triangle.A, Triangle.B, Triangle.C,
				(Triangle.A + Triangle.B) * 0.5, (Triangle.B + Triangle.C) * 0.5,
				(Triangle.C + Triangle.A) * 0.5, (Triangle.A + Triangle.B + Triangle.C) / 3.0};
			const FVector2D& Point = Locations[SampleIndex];
			FSample Sample;
			if (const FSample* Existing = Samples.Find(Point)) { Sample = *Existing; }
			else
			{
				if (Queries >= MaxQueries) { return false; }
				Sample.bValid = Project(Point, Sample.Position) && !Sample.Position.ContainsNaN();
				Samples.Add(Point, Sample); ++Queries;
			}
			bLeafValid &= Sample.bValid;
			if (SampleIndex < 3) { CurrentVertices[SampleIndex] = Sample.Position; }
			++SampleIndex;
			// Match the synchronous floor test: always resolve the three corners,
			// then stop querying midpoints/centroid as soon as the leaf is invalid.
			if (SampleIndex < 3 || (bLeafValid && SampleIndex < static_cast<int32>(UE_ARRAY_COUNT(Locations)))) { continue; }
			if (!bLeafValid) { ++SkippedCount; }
			else
			{
				const int32 Base = Data.Vertices.Num();
				for (const FVector& Vertex : CurrentVertices) { Data.Vertices.Add(FVector3f(Vertex)); }
				Data.Indices.Append({Base, Base + 1, Base + 2}); Data.TriangleLayerIds.Add(Layer);
			}
			++LeafIndex; SampleIndex = 0; bLeafValid = true;
		}
		bFinished = true;
		Result = Data.Indices.IsEmpty() ? EShapeBuildResult::NoSurface
			: SkippedCount > 0 ? EShapeBuildResult::PartialSurface : EShapeBuildResult::Success;
		return true;
	}

	EShapeBuildResult BuildProjectedShape(TConstArrayView<FVector2D> Outline, double SampleSpacing,
		const FGuid& LayerId, TFunctionRef<bool(const FVector2D&, FVector&)> Project,
		FComposableCameraMeshSurfaceAuthoringData& OutData)
	{
		FProjectedShapeBuild Build;
		const EShapeBuildResult Started = Build.Begin(Outline, SampleSpacing, LayerId);
		if (Started != EShapeBuildResult::Success) { return Started; }
		Build.Advance(Project, MAX_int32);
		const EShapeBuildResult Result = Build.GetResult();
		if (Result == EShapeBuildResult::Success || Result == EShapeBuildResult::PartialSurface) { OutData = Build.TakeData(); }
		return Result;
	}
}
