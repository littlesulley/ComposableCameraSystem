// Copyright 2026 Sulley. All Rights Reserved.

#include "MeshCamera/ComposableCameraMeshSurfaceTypes.h"

namespace
{
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
}

bool FComposableCameraMeshSurfaceRuntimeData::IsConsistent() const
{
	return Indices.Num() % 3 == 0
		&& TriangleLayerIndices.Num() == Indices.Num() / 3;
}

bool FComposableCameraMeshSurfaceRuntimeData::QueryLocalRayLayers(
	const FVector& LocalOrigin,
	const FVector& LocalDirection,
	double MaxDistance,
	double SameSurfaceTolerance,
	TConstArrayView<FComposableCameraMeshLayerDefinition> Layers,
	TArray<int32, TInlineAllocator<16>>& OutLayerIndices,
	FVector& OutLocalSurfacePosition,
	double& OutRayDistance) const
{
	OutLayerIndices.Reset();
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

	const int32 TriangleCount = Indices.Num() / 3;
	for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
	{
		const int32 LayerIndex = TriangleLayerIndices[TriangleIndex];
		if (!Layers.IsValidIndex(LayerIndex) || !Layers[LayerIndex].bEnabled)
		{
			continue;
		}

		const int32 IndexOffset = TriangleIndex * 3;
		const int32 Index0 = Indices[IndexOffset];
		const int32 Index1 = Indices[IndexOffset + 1];
		const int32 Index2 = Indices[IndexOffset + 2];
		if (!Vertices.IsValidIndex(Index0)
			|| !Vertices.IsValidIndex(Index1)
			|| !Vertices.IsValidIndex(Index2))
		{
			continue;
		}

		double Distance = 0.0;
		if (!IntersectRayTriangle(
			LocalOrigin,
			Direction,
			Vertices[Index0],
			Vertices[Index1],
			Vertices[Index2],
			Distance)
			|| Distance > MaxDistance)
		{
			continue;
		}

		if (!bFound || Distance < BestDistance)
		{
			bFound = true;
			BestDistance = Distance;
		}
	}

	if (!bFound)
	{
		return false;
	}

	for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
	{
		const int32 LayerIndex = TriangleLayerIndices[TriangleIndex];
		if (!Layers.IsValidIndex(LayerIndex) || !Layers[LayerIndex].bEnabled
			|| OutLayerIndices.Contains(LayerIndex))
		{
			continue;
		}

		const int32 IndexOffset = TriangleIndex * 3;
		const int32 Index0 = Indices[IndexOffset];
		const int32 Index1 = Indices[IndexOffset + 1];
		const int32 Index2 = Indices[IndexOffset + 2];
		if (!Vertices.IsValidIndex(Index0)
			|| !Vertices.IsValidIndex(Index1)
			|| !Vertices.IsValidIndex(Index2))
		{
			continue;
		}

		double Distance = 0.0;
		if (IntersectRayTriangle(
			LocalOrigin,
			Direction,
			Vertices[Index0],
			Vertices[Index1],
			Vertices[Index2],
			Distance)
			&& Distance <= MaxDistance
			&& FMath::Abs(Distance - BestDistance) <= SameSurfaceTolerance)
		{
			OutLayerIndices.Add(LayerIndex);
		}
	}
	OutLayerIndices.Sort();
	if (OutLayerIndices.IsEmpty())
	{
		return false;
	}

	OutRayDistance = BestDistance;
	OutLocalSurfacePosition = LocalOrigin + Direction * BestDistance;
	return true;
}
