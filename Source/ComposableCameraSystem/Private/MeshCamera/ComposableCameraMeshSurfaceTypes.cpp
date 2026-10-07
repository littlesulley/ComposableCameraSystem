// Copyright 2026 Sulley. All Rights Reserved.

#include "MeshCamera/ComposableCameraMeshSurfaceTypes.h"

#include "ProfilingDebugging/CpuProfilerTrace.h"
#include <algorithm>

namespace
{
	bool MeshSurfaceRayIntersectsBounds(const FVector& Origin, const FVector& Direction,
		const FBox& Bounds, double MaxDistance)
	{
		// The triangle predicate admits a small barycentric edge tolerance. Pad
		// bounds proportionally so acceleration cannot reject those valid hits.
		const FBox Padded = Bounds.ExpandBy(
			Bounds.GetSize().GetMax() * (2.0 * UE_DOUBLE_KINDA_SMALL_NUMBER)
			+ UE_DOUBLE_SMALL_NUMBER);
		double Near = 0.0;
		double Far = MaxDistance;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (Direction[Axis] == 0.0)
			{
				if (Origin[Axis] < Padded.Min[Axis] || Origin[Axis] > Padded.Max[Axis]) return false;
				continue;
			}
			double Entry = (Padded.Min[Axis] - Origin[Axis]) / Direction[Axis];
			double Exit = (Padded.Max[Axis] - Origin[Axis]) / Direction[Axis];
			if (Entry > Exit) Swap(Entry, Exit);
			Near = FMath::Max(Near, Entry);
			Far = FMath::Min(Far, Exit);
			if (Near > Far) return false;
		}
		return Near <= Far;
	}

	bool IntersectRayTriangle(
		const FVector& Origin,
		const FVector& Direction,
		const FVector3f& Vertex0,
		const FVector3f& Vertex1,
		const FVector3f& Vertex2,
		double& OutDistance)
	{
		const FVector A(Vertex0);
		const FVector Edge1 = FVector(Vertex1) - A;
		const FVector Edge2 = FVector(Vertex2) - A;
		const FVector P = FVector::CrossProduct(Direction, Edge2);
		const double Determinant = FVector::DotProduct(Edge1, P);
		if (FMath::Abs(Determinant) <= UE_DOUBLE_SMALL_NUMBER)
		{
			return false;
		}

		const double InverseDeterminant = 1.0 / Determinant;
		const FVector T = Origin - A;
		const double U = FVector::DotProduct(T, P) * InverseDeterminant;
		if (U < -UE_DOUBLE_KINDA_SMALL_NUMBER || U > 1.0 + UE_DOUBLE_KINDA_SMALL_NUMBER)
		{
			return false;
		}

		const FVector Q = FVector::CrossProduct(T, Edge1);
		const double V = FVector::DotProduct(Direction, Q) * InverseDeterminant;
		if (V < -UE_DOUBLE_KINDA_SMALL_NUMBER || U + V > 1.0 + UE_DOUBLE_KINDA_SMALL_NUMBER)
		{
			return false;
		}

		const double Distance = FVector::DotProduct(Edge2, Q) * InverseDeterminant;
		if (Distance < 0.0)
		{
			return false;
		}

		OutDistance = Distance;
		return true;
	}
}

void FComposableCameraMeshSurfaceRuntimeData::Reset()
{
	Vertices.Reset();
	Indices.Reset();
	TriangleLayerIndices.Reset();
	LocalBounds = FBox(ForceInit);
	SpatialNodes.Reset();
	SpatialTriangleIndices.Reset();
	IndexedVertexCount = IndexedIndexCount = IndexedLayerIndexCount = 0;
}

bool FComposableCameraMeshSurfaceRuntimeData::IsConsistent() const
{
	return Indices.Num() % 3 == 0
		&& TriangleLayerIndices.Num() == Indices.Num() / 3;
}

bool FComposableCameraMeshSurfaceRuntimeData::HasSpatialIndex() const
{
	return !SpatialNodes.IsEmpty() && IndexedVertexCount == Vertices.Num()
		&& IndexedIndexCount == Indices.Num()
		&& IndexedLayerIndexCount == TriangleLayerIndices.Num();
}

void FComposableCameraMeshSurfaceRuntimeData::RebuildSpatialIndex()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(CCS_MeshSurface_BuildSpatialIndex);
	SpatialNodes.Reset();
	SpatialTriangleIndices.Reset();
	LocalBounds = FBox(ForceInit);
	IndexedVertexCount = IndexedIndexCount = IndexedLayerIndexCount = 0;
	if (!IsConsistent()) return;

	const int32 TriangleCount = Indices.Num() / 3;
	TArray<FBox> TriangleBounds;
	TriangleBounds.SetNum(TriangleCount);
	SpatialTriangleIndices.Reserve(TriangleCount);
	for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
	{
		FBox Bounds(ForceInit);
		bool bValid = true;
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const int32 VertexIndex = Indices[TriangleIndex * 3 + Corner];
			if (!Vertices.IsValidIndex(VertexIndex)) { bValid = false; break; }
			Bounds += FVector(Vertices[VertexIndex]);
		}
		if (bValid)
		{
			TriangleBounds[TriangleIndex] = Bounds;
			SpatialTriangleIndices.Add(TriangleIndex);
			LocalBounds += Bounds;
		}
	}
	if (!SpatialTriangleIndices.IsEmpty())
	{
		const uint32 MaxLeaves = FMath::RoundUpToPowerOfTwo(
			static_cast<uint32>(FMath::DivideAndRoundUp(SpatialTriangleIndices.Num(), 8)));
		SpatialNodes.Reserve(static_cast<int32>(MaxLeaves * 2 - 1));
		BuildSpatialNode(0, SpatialTriangleIndices.Num(), TriangleBounds);
		IndexedVertexCount = Vertices.Num();
		IndexedIndexCount = Indices.Num();
		IndexedLayerIndexCount = TriangleLayerIndices.Num();
	}
}

int32 FComposableCameraMeshSurfaceRuntimeData::BuildSpatialNode(
	int32 FirstTriangle, int32 NumTriangles, TConstArrayView<FBox> TriangleBounds)
{
	const int32 NodeIndex = SpatialNodes.AddDefaulted();
	FBox Bounds(ForceInit);
	FBox Centers(ForceInit);
	for (int32 Index = FirstTriangle; Index < FirstTriangle + NumTriangles; ++Index)
	{
		const FBox& Triangle = TriangleBounds[SpatialTriangleIndices[Index]];
		Bounds += Triangle;
		Centers += Triangle.GetCenter();
	}
	SpatialNodes[NodeIndex].Bounds = Bounds;
	if (NumTriangles <= 8)
	{
		SpatialNodes[NodeIndex].FirstTriangle = FirstTriangle;
		SpatialNodes[NodeIndex].NumTriangles = NumTriangles;
	}
	else
	{
		const FVector Extent = Centers.GetSize();
		int32 Axis = Extent.Y > Extent.X ? 1 : 0;
		if (Extent.Z > Extent[Axis]) Axis = 2;
		const int32 LeftCount = NumTriangles / 2;
		int32* First = SpatialTriangleIndices.GetData() + FirstTriangle;
		// Only median partition is required. Sorting every subtree made construction O(N log squared N).
		std::nth_element(First, First + LeftCount, First + NumTriangles,
			[TriangleBounds, Axis](int32 A, int32 B)
			{
				const double CenterA = TriangleBounds[A].GetCenter()[Axis];
				const double CenterB = TriangleBounds[B].GetCenter()[Axis];
				return CenterA == CenterB ? A < B : CenterA < CenterB;
			});
		BuildSpatialNode(FirstTriangle, LeftCount, TriangleBounds);
		BuildSpatialNode(FirstTriangle + LeftCount, NumTriangles - LeftCount, TriangleBounds);
	}
	// Recursive Add can relocate the array; reacquire by index after children.
	SpatialNodes[NodeIndex].EscapeIndex = SpatialNodes.Num();
	return NodeIndex;
}

bool FComposableCameraMeshSurfaceRuntimeData::QueryLocalRayLayers(
	const FVector& LocalOrigin,
	const FVector& LocalDirection,
	double MaxDistance,
	double SameSurfaceTolerance,
	TConstArrayView<FComposableCameraMeshLayerDefinition> Layers,
	TArray<int32, TInlineAllocator<16>>& OutLayerIndices,
	FVector& OutLocalSurfacePosition,
	double& OutRayDistance,
	int32* OutTriangleTests,
	TArray<double, TInlineAllocator<16>>* OutLayerRayDistances) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(CCS_MeshSurface_Query);
	OutLayerIndices.Reset();
	if (OutLayerRayDistances) OutLayerRayDistances->Reset();
	if (OutTriangleTests) *OutTriangleTests = 0;
	if (!IsConsistent() || Vertices.IsEmpty() || Indices.IsEmpty())
	{
		return false;
	}

	const FVector Direction = LocalDirection.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return false;
	}

	bool bFound = false;
	double BestDistance = MaxDistance;

	// No lazy index construction or traversal stack allocation in this hot path.
	// Standalone/legacy callers without a rebuilt index retain the linear path.
	auto VisitTriangles = [&](auto&& GetLimit, auto&& Visitor)
	{
		if (!HasSpatialIndex())
		{
			for (int32 TriangleIndex = 0; TriangleIndex < Indices.Num() / 3; ++TriangleIndex)
			{
				Visitor(TriangleIndex);
			}
			return;
		}
		int32 NodeIndex = 0;
		while (NodeIndex < SpatialNodes.Num())
		{
			const FSpatialNode& Node = SpatialNodes[NodeIndex];
			if (!MeshSurfaceRayIntersectsBounds(LocalOrigin, Direction, Node.Bounds, GetLimit()))
			{
				NodeIndex = Node.EscapeIndex;
				continue;
			}
			if (Node.NumTriangles == 0) { ++NodeIndex; continue; }
			for (int32 Index = Node.FirstTriangle; Index < Node.FirstTriangle + Node.NumTriangles; ++Index)
			{
				Visitor(SpatialTriangleIndices[Index]);
			}
			NodeIndex = Node.EscapeIndex;
		}
	};

	auto GetTriangleHit = [&](int32 TriangleIndex, double& OutDistance)
	{
		const int32 LayerIndex = TriangleLayerIndices[TriangleIndex];
		if (!Layers.IsValidIndex(LayerIndex) || !Layers[LayerIndex].bEnabled)
		{
			return false;
		}

		const int32 IndexOffset = TriangleIndex * 3;
		const int32 Index0 = Indices[IndexOffset];
		const int32 Index1 = Indices[IndexOffset + 1];
		const int32 Index2 = Indices[IndexOffset + 2];
		if (!Vertices.IsValidIndex(Index0)
			|| !Vertices.IsValidIndex(Index1)
			|| !Vertices.IsValidIndex(Index2))
		{
			return false;
		}
		if (OutTriangleTests) ++*OutTriangleTests;
		return IntersectRayTriangle(
			LocalOrigin,
			Direction,
			Vertices[Index0],
			Vertices[Index1],
			Vertices[Index2],
			OutDistance) && OutDistance <= MaxDistance;
	};

	VisitTriangles([&]() { return BestDistance; }, [&](int32 TriangleIndex)
	{
		double Distance = 0.0;
		if (GetTriangleHit(TriangleIndex, Distance) && (!bFound || Distance < BestDistance))
		{
			bFound = true;
			BestDistance = Distance;
		}
	});

	if (!bFound)
	{
		return false;
	}

	TArray<TPair<int32, double>, TInlineAllocator<16>> LayerHits;
	VisitTriangles([&]() { return FMath::Min(MaxDistance, BestDistance + SameSurfaceTolerance); },
		[&](int32 TriangleIndex)
	{
		const int32 LayerIndex = TriangleLayerIndices[TriangleIndex];
		if (!Layers.IsValidIndex(LayerIndex) || !Layers[LayerIndex].bEnabled
			|| (!OutLayerRayDistances && OutLayerIndices.Contains(LayerIndex)))
		{
			return;
		}
		double Distance = 0.0;
		if (GetTriangleHit(TriangleIndex, Distance)
			&& FMath::Abs(Distance - BestDistance) <= SameSurfaceTolerance)
		{
			if (OutLayerRayDistances)
			{
				// BVH visitation order does not imply the nearest hit within a Layer.
				if (auto* Hit = LayerHits.FindByPredicate([LayerIndex](const auto& Candidate)
					{ return Candidate.Key == LayerIndex; }))
				{
					Hit->Value = FMath::Min(Hit->Value, Distance);
				}
				else LayerHits.Emplace(LayerIndex, Distance);
			}
			else OutLayerIndices.Add(LayerIndex);
		}
	});
	if (OutLayerRayDistances)
	{
		LayerHits.Sort([](const auto& A, const auto& B) { return A.Key < B.Key; });
		for (const auto& Hit : LayerHits)
		{
			OutLayerIndices.Add(Hit.Key);
			OutLayerRayDistances->Add(Hit.Value);
		}
	}
	else OutLayerIndices.Sort();
	if (OutLayerIndices.IsEmpty())
	{
		return false;
	}

	OutRayDistance = BestDistance;
	OutLocalSurfacePosition = LocalOrigin + Direction * BestDistance;
	return true;
}
