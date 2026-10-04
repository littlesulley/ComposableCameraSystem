// Copyright 2026 Sulley. All Rights Reserved.

#include "MeshCamera/ComposableCameraMeshLayerRendering.h"

#include "DynamicMeshBuilder.h"
#include "EditorModes.h"
#include "Engine/Engine.h"
#include "MeshCamera/ComposableCameraMeshSurfaceTypes.h"
#include "PrimitiveDrawInterface.h"
#include "SceneManagement.h"

namespace UE::ComposableCamera::MeshEditor
{
	namespace
	{
		constexpr double SurfaceOffset = 1.5;
		constexpr double MinimumCellSize = 10.0;
		constexpr double TargetVisibleCellCount = 100000.0;
		constexpr double SameSurfaceTolerance = 5.0;
		using FSurfaceCellIndices = TArray<int32, TInlineAllocator<2>>;

		void RemoveGridCells(FResolvedSurfaceVisualization& Visualization, const FIntPoint& Grid)
		{
			FSurfaceCellIndices* Indices = Visualization.CellsByGrid.Find(Grid);
			if (!Indices) { return; }
			while (!Indices->IsEmpty())
			{
				const int32 Removed = Indices->Pop(EAllowShrinking::No);
				const int32 Last = Visualization.Cells.Num() - 1;
				if (Removed != Last)
				{
					const FVector& Position = Visualization.Cells[Last].LocalPosition;
					const FIntPoint MovedGrid(FMath::FloorToInt(Position.X / Visualization.CellSize),
						FMath::FloorToInt(Position.Y / Visualization.CellSize));
					FSurfaceCellIndices& MovedIndices = Visualization.CellsByGrid.FindChecked(MovedGrid);
					MovedIndices[MovedIndices.Find(Last)] = Removed;
				}
				Visualization.Cells.RemoveAtSwap(Removed, 1, EAllowShrinking::No);
			}
			Visualization.CellsByGrid.Remove(Grid);
		}

		bool GetTriangle(
			TConstArrayView<FVector3f> Vertices,
			TConstArrayView<int32> Indices,
			int32 TriangleIndex,
			FVector& OutA,
			FVector& OutB,
			FVector& OutC)
		{
			const int32 Offset = TriangleIndex * 3;
			if (!Indices.IsValidIndex(Offset + 2))
			{
				return false;
			}

			const int32 Index0 = Indices[Offset];
			const int32 Index1 = Indices[Offset + 1];
			const int32 Index2 = Indices[Offset + 2];
			if (!Vertices.IsValidIndex(Index0)
				|| !Vertices.IsValidIndex(Index1)
				|| !Vertices.IsValidIndex(Index2))
			{
				return false;
			}

			OutA = FVector(Vertices[Index0]);
			OutB = FVector(Vertices[Index1]);
			OutC = FVector(Vertices[Index2]);
			return true;
		}

		double GetProjectedTriangleArea(const FVector& A, const FVector& B, const FVector& C)
		{
			return FMath::Abs(
				(B.X - A.X) * (C.Y - A.Y)
				- (B.Y - A.Y) * (C.X - A.X)) * 0.5;
		}

		using FSurfacePieces = TArray<FSurfacePolygon, TInlineAllocator<4>>;

		double PolygonArea(TConstArrayView<FVector> Polygon)
		{
			double Area = 0.0;
			for (int32 Index = 1; Index + 1 < Polygon.Num(); ++Index)
			{
				Area += FVector::CrossProduct(Polygon[Index] - Polygon[0], Polygon[Index + 1] - Polygon[0]).Z;
			}
			return FMath::Abs(Area) * 0.5;
		}

		FSurfacePolygon ClipHalfPlane(TConstArrayView<FVector> Polygon, const FVector2D& Normal, double Limit)
		{
			FSurfacePolygon Result;
			auto AddPoint = [&Result](const FVector& Point)
			{
				if (Result.IsEmpty() || !Result.Last().Equals(Point, 1.e-10)) { Result.Add(Point); }
			};
			for (int32 Index = 0; Index < Polygon.Num(); ++Index)
			{
				const FVector& A = Polygon[Index];
				const FVector& B = Polygon[(Index + 1) % Polygon.Num()];
				const double DA = A.X * Normal.X + A.Y * Normal.Y - Limit;
				const double DB = B.X * Normal.X + B.Y * Normal.Y - Limit;
				if (DA >= 0.0) { AddPoint(A); }
				if ((DA >= 0.0) != (DB >= 0.0)) { AddPoint(FMath::Lerp(A, B, DA / (DA - DB))); }
			}
			if (Result.Num() > 1 && Result[0].Equals(Result.Last(), 1.e-10)) { Result.Pop(EAllowShrinking::No); }
			return Result;
		}

		void SubtractPolygon(TConstArrayView<FVector> Subject, TConstArrayView<FVector> Cut, FSurfacePieces& OutPieces)
		{
			// Most neighboring fragments only touch or are disjoint. A separating
			// edge avoids both clipping work and needless fragmentation of the cache.
			auto HasSeparatingEdge = [](TConstArrayView<FVector> A, TConstArrayView<FVector> B)
			{
				for (int32 Edge = 0; Edge < A.Num(); ++Edge)
				{
					const FVector& Start = A[Edge];
					const FVector& End = A[(Edge + 1) % A.Num()];
					const FVector2D Normal(Start.Y - End.Y, End.X - Start.X);
					if (Normal.IsNearlyZero(UE_DOUBLE_SMALL_NUMBER)) { continue; }
					const double Limit = Start.X * Normal.X + Start.Y * Normal.Y;
					bool bOutside = true;
					for (const FVector& Point : B)
					{
						if (Point.X * Normal.X + Point.Y * Normal.Y > Limit) { bOutside = false; break; }
					}
					if (bOutside) { return true; }
				}
				return false;
			};
			if (HasSeparatingEdge(Subject, Cut) || HasSeparatingEdge(Cut, Subject))
			{
				FSurfacePolygon Unchanged;
				Unchanged.Append(Subject.GetData(), Subject.Num());
				OutPieces.Add(MoveTemp(Unchanged));
				return;
			}
			FSurfacePolygon Inside;
			Inside.Append(Subject.GetData(), Subject.Num());
			for (int32 Edge = 0; Edge < Cut.Num() && Inside.Num() >= 3; ++Edge)
			{
				const FVector& A = Cut[Edge];
				const FVector& B = Cut[(Edge + 1) % Cut.Num()];
				const FVector2D Normal(A.Y - B.Y, B.X - A.X);
				if (Normal.IsNearlyZero(UE_DOUBLE_SMALL_NUMBER)) { continue; }
				const double Limit = A.X * Normal.X + A.Y * Normal.Y;
				FSurfacePolygon Outside = ClipHalfPlane(Inside, -Normal, -Limit);
				if (PolygonArea(Outside) > UE_DOUBLE_SMALL_NUMBER) { OutPieces.Add(MoveTemp(Outside)); }
				Inside = ClipHalfPlane(Inside, Normal, Limit);
			}
		}

		FVector PlanePosition(const FVector& Origin, const FVector& Normal, double X, double Y)
		{
			FVector Point(X, Y, Origin.Z);
			Point.Z += FVector::DotProduct(Origin - Point, Normal) / Normal.Z;
			return Point;
		}

		void ResolveCoverage(FResolvedSurfaceCell& Cell, FSurfacePolygon Polygon, int32 LayerIndex,
			const FVector& Normal, double CellSize)
		{
			// Interior cells stay cheap even with many overlapping brush stamps.
			if (Cell.bFullCoverage && Cell.LayerIndex <= LayerIndex) { return; }
			FSurfacePieces Pending;
			Pending.Add(Polygon);
			auto Previous = MoveTemp(Cell.Patches);
			for (auto& Old : Previous)
			{
				if (Old.LayerIndex <= LayerIndex)
				{
					FSurfacePieces Next;
					for (const auto& Piece : Pending) { SubtractPolygon(Piece, Old.LocalVertices, Next); }
					Pending = MoveTemp(Next);
					Cell.Patches.Add(MoveTemp(Old));
				}
				else
				{
					FSurfacePieces Pieces;
					SubtractPolygon(Old.LocalVertices, Polygon, Pieces);
					for (auto& Piece : Pieces)
					{
						auto& Patch = Cell.Patches.AddDefaulted_GetRef();
						Patch.LocalVertices = MoveTemp(Piece);
						Patch.LocalNormal = Old.LocalNormal;
						Patch.LayerIndex = Old.LayerIndex;
					}
				}
			}
			for (auto& Piece : Pending)
			{
				auto& Patch = Cell.Patches.AddDefaulted_GetRef();
				Patch.LocalVertices = MoveTemp(Piece);
				Patch.LocalNormal = Normal;
				Patch.LayerIndex = LayerIndex;
			}
			Cell.bFullCoverage = false;
			if (Cell.Patches.IsEmpty()) { return; }
			const FResolvedSurfacePatch* Dominant = &Cell.Patches[0];
			for (const auto& Patch : Cell.Patches)
			{
				if (Patch.LayerIndex < Dominant->LayerIndex) { Dominant = &Patch; }
			}
			Cell.LayerIndex = Dominant->LayerIndex;
			Cell.LocalNormal = Dominant->LocalNormal;
			Cell.LocalPosition = PlanePosition(Dominant->LocalVertices[0], Cell.LocalNormal, Cell.LocalPosition.X, Cell.LocalPosition.Y);

			// Collapse a fully covered, coplanar cell into one quad. Boundary cells
			// retain the clipped polygons instead of expanding them to square pixels.
			double Area = 0.0;
			for (const auto& Patch : Cell.Patches)
			{
				if (Patch.LayerIndex != Cell.LayerIndex
					|| FVector::DotProduct(Patch.LocalNormal, Cell.LocalNormal) < 1.0 - 1.e-8) { return; }
				for (const FVector& Vertex : Patch.LocalVertices)
				{
					if (FMath::Abs(FVector::DotProduct(Vertex - Cell.LocalPosition, Cell.LocalNormal)) > 1.e-4) { return; }
				}
				Area += PolygonArea(Patch.LocalVertices);
			}
			if (!FMath::IsNearlyEqual(Area, CellSize * CellSize, CellSize * CellSize * 1.e-10)) { return; }
			FResolvedSurfacePatch Full;
			Full.LayerIndex = Cell.LayerIndex; Full.LocalNormal = Cell.LocalNormal;
			const double Half = CellSize * 0.5;
			for (const FVector2D& Offset : {FVector2D(-Half, -Half), FVector2D(Half, -Half), FVector2D(Half, Half), FVector2D(-Half, Half)})
			{
				Full.LocalVertices.Add(PlanePosition(Cell.LocalPosition, Cell.LocalNormal,
					Cell.LocalPosition.X + Offset.X, Cell.LocalPosition.Y + Offset.Y));
			}
			Cell.Patches.Reset();
			Cell.Patches.Add(MoveTemp(Full));
			Cell.bFullCoverage = true;
		}

		template <typename ResolveLayerIndexType>
		void BuildResolvedVisualization(
			TConstArrayView<FVector3f> Vertices,
			TConstArrayView<int32> Indices,
			int32 TriangleCount,
			TConstArrayView<FComposableCameraMeshLayerDefinition> Layers,
			ResolveLayerIndexType&& ResolveLayerIndex,
			FResolvedSurfaceVisualization& OutVisualization,
			const FBox2D* DirtyBounds = nullptr,
			FVisualizationUpdateStats* OutStats = nullptr)
		{
			if (OutStats) { *OutStats = {}; }
			if (TriangleCount <= 0 || Layers.IsEmpty())
			{
				OutVisualization.Reset();
				return;
			}

			FBox2D ProjectedBounds(ForceInit);
			for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
			{
				const int32 LayerIndex = ResolveLayerIndex(TriangleIndex);
				if (!Layers.IsValidIndex(LayerIndex) || !Layers[LayerIndex].bEnabled)
				{
					continue;
				}

				FVector A;
				FVector B;
				FVector C;
				if (GetTriangle(Vertices, Indices, TriangleIndex, A, B, C)
					&& GetProjectedTriangleArea(A, B, C) > UE_DOUBLE_SMALL_NUMBER)
				{
					ProjectedBounds += FVector2D(A.X, A.Y);
					ProjectedBounds += FVector2D(B.X, B.Y);
					ProjectedBounds += FVector2D(C.X, C.Y);
				}
			}

			if (!ProjectedBounds.bIsValid)
			{
				OutVisualization.Reset();
				return;
			}

			const FVector2D ProjectedSize = ProjectedBounds.GetSize();
			const double ProjectedBoundsArea = ProjectedSize.X * ProjectedSize.Y;
			const double DesiredCellSize = FMath::Max(
				MinimumCellSize,
				FMath::Sqrt(ProjectedBoundsArea / TargetVisibleCellCount));
			// Keep the grid stable during a stroke; regrid only for substantial growth.
			const bool bFullRebuild = !DirtyBounds || !DirtyBounds->bIsValid
				|| !OutVisualization.LocalBounds.bIsValid || DesiredCellSize > OutVisualization.CellSize * 1.25;
			if (OutStats) { OutStats->bFullRebuild = bFullRebuild; }
			// FIntPoint's default constructor leaves coordinates uninitialized.
			// These bounds are unused by full rebuilds and overwritten by regional ones.
			FIntPoint DirtyMin(0, 0), DirtyMax(0, 0);
			if (bFullRebuild)
			{
				OutVisualization.Reset();
				OutVisualization.CellSize = DesiredCellSize;
			}
			else
			{
				// Include old bounds: erasing an edge can shrink the new document bounds.
				FBox2D CoverageBounds = OutVisualization.LocalBounds;
				CoverageBounds += ProjectedBounds;
				DirtyMin = FIntPoint(FMath::FloorToInt(FMath::Max(DirtyBounds->Min.X, CoverageBounds.Min.X) / OutVisualization.CellSize),
					FMath::FloorToInt(FMath::Max(DirtyBounds->Min.Y, CoverageBounds.Min.Y) / OutVisualization.CellSize));
				DirtyMax = FIntPoint(FMath::FloorToInt(FMath::Min(DirtyBounds->Max.X, CoverageBounds.Max.X) / OutVisualization.CellSize),
					FMath::FloorToInt(FMath::Min(DirtyBounds->Max.Y, CoverageBounds.Max.Y) / OutVisualization.CellSize));
				for (int32 Y = DirtyMin.Y; Y <= DirtyMax.Y; ++Y)
				{
					for (int32 X = DirtyMin.X; X <= DirtyMax.X; ++X) { RemoveGridCells(OutVisualization, FIntPoint(X, Y)); }
				}
			}
			OutVisualization.LocalBounds = ProjectedBounds;
			const int32 EstimatedGridCellCount = FMath::Max(
				1,
				FMath::CeilToInt(
					ProjectedBoundsArea
					/ FMath::Square(OutVisualization.CellSize)));
			if (bFullRebuild)
			{
				OutVisualization.Cells.Reserve(EstimatedGridCellCount);
				OutVisualization.CellsByGrid.Reserve(EstimatedGridCellCount);
			}
			auto& SurfaceCellsByGrid = OutVisualization.CellsByGrid;

			for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
			{
				const int32 LayerIndex = ResolveLayerIndex(TriangleIndex);
				if (!Layers.IsValidIndex(LayerIndex) || !Layers[LayerIndex].bEnabled)
				{
					continue;
				}

				FVector A;
				FVector B;
				FVector C;
				if (!GetTriangle(Vertices, Indices, TriangleIndex, A, B, C)
					|| GetProjectedTriangleArea(A, B, C) <= UE_DOUBLE_SMALL_NUMBER)
				{
					continue;
				}

				FVector Normal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
				if (Normal.IsNearlyZero())
				{
					continue;
				}
				if (Normal.Z < 0.0)
				{
					Normal *= -1.0;
					Swap(B, C);
				}
				if (Normal.Z <= UE_DOUBLE_KINDA_SMALL_NUMBER) { continue; }

				const int32 MinCellX = FMath::FloorToInt(
					FMath::Min3(A.X, B.X, C.X) / OutVisualization.CellSize);
				const int32 MaxCellX = FMath::FloorToInt(
					FMath::Max3(A.X, B.X, C.X) / OutVisualization.CellSize);
				const int32 MinCellY = FMath::FloorToInt(
					FMath::Min3(A.Y, B.Y, C.Y) / OutVisualization.CellSize);
				const int32 MaxCellY = FMath::FloorToInt(
					FMath::Max3(A.Y, B.Y, C.Y) / OutVisualization.CellSize);
				const int32 StartX = bFullRebuild ? MinCellX : FMath::Max(MinCellX, DirtyMin.X);
				const int32 EndX = bFullRebuild ? MaxCellX : FMath::Min(MaxCellX, DirtyMax.X);
				const int32 StartY = bFullRebuild ? MinCellY : FMath::Max(MinCellY, DirtyMin.Y);
				const int32 EndY = bFullRebuild ? MaxCellY : FMath::Min(MaxCellY, DirtyMax.Y);
				if (StartX > EndX || StartY > EndY) { continue; }
				if (OutStats) { ++OutStats->RasterizedTriangles; }

				for (int32 CellY = StartY; CellY <= EndY; ++CellY)
				{
					for (int32 CellX = StartX; CellX <= EndX; ++CellX)
					{
						if (OutStats) { ++OutStats->CellTests; }
						const FIntPoint Grid(CellX, CellY);
						const FVector Position = PlanePosition(A, Normal, (CellX + 0.5) * OutVisualization.CellSize,
							(CellY + 0.5) * OutVisualization.CellSize);
						int32 ExistingIndex = INDEX_NONE;
						if (const auto* IndicesAtGrid = SurfaceCellsByGrid.Find(Grid))
						{
							for (int32 Index : *IndicesAtGrid)
							{
								if (FMath::Abs(OutVisualization.Cells[Index].LocalPosition.Z - Position.Z) <= SameSurfaceTolerance)
								{
									ExistingIndex = Index;
									break;
								}
							}
						}
						if (ExistingIndex != INDEX_NONE && OutVisualization.Cells[ExistingIndex].bFullCoverage
							&& OutVisualization.Cells[ExistingIndex].LayerIndex <= LayerIndex) { continue; }

						FSurfacePolygon Polygon;
						Polygon.Append({A, B, C});
						const double X = CellX * OutVisualization.CellSize;
						const double Y = CellY * OutVisualization.CellSize;
						Polygon = ClipHalfPlane(Polygon, FVector2D(1.0, 0.0), X);
						Polygon = ClipHalfPlane(Polygon, FVector2D(-1.0, 0.0), -X - OutVisualization.CellSize);
						Polygon = ClipHalfPlane(Polygon, FVector2D(0.0, 1.0), Y);
						Polygon = ClipHalfPlane(Polygon, FVector2D(0.0, -1.0), -Y - OutVisualization.CellSize);
						if (PolygonArea(Polygon) <= UE_DOUBLE_SMALL_NUMBER) { continue; }
						if (ExistingIndex != INDEX_NONE)
						{
							ResolveCoverage(OutVisualization.Cells[ExistingIndex], MoveTemp(Polygon), LayerIndex, Normal, OutVisualization.CellSize);
						}
						else
						{
							FResolvedSurfaceCell Cell;
							Cell.LocalPosition = Position; Cell.LocalNormal = Normal; Cell.LayerIndex = LayerIndex;
							ResolveCoverage(Cell, MoveTemp(Polygon), LayerIndex, Normal, OutVisualization.CellSize);
							const int32 Index = OutVisualization.Cells.Add(MoveTemp(Cell));
							SurfaceCellsByGrid.FindOrAdd(Grid).Add(Index);
						}
					}
				}
			}
		}

		void AddSurfacePatch(FDynamicMeshBuilder& MeshBuilder, const FResolvedSurfacePatch& Patch)
		{
			const FVector TangentX = (Patch.LocalVertices[1] - Patch.LocalVertices[0]).GetSafeNormal();
			const FVector TangentY = FVector::CrossProduct(Patch.LocalNormal, TangentX).GetSafeNormal();
			int32 FirstVertex = INDEX_NONE;
			for (const FVector& Vertex : Patch.LocalVertices)
			{
				// Offset along document Z so adjacent patches retain identical XY boundaries.
				const int32 Index = MeshBuilder.AddVertex(
					FVector3f(Vertex + FVector::UpVector * SurfaceOffset),
					FVector2f::ZeroVector,
					FVector3f(TangentX),
					FVector3f(TangentY),
					FVector3f(Patch.LocalNormal),
					FColor::White);
				if (FirstVertex == INDEX_NONE) { FirstVertex = Index; }
			}
			for (int32 Index = 1; Index + 1 < Patch.LocalVertices.Num(); ++Index)
			{
				MeshBuilder.AddTriangle(FirstVertex, FirstVertex + Index, FirstVertex + Index + 1);
			}
		}

		void DrawResolvedLayer(
			FPrimitiveDrawInterface* PDI,
			const FTransform& LocalToWorld,
			const FResolvedSurfaceVisualization& Visualization,
			int32 LayerIndex,
			const FLinearColor& FillColor)
		{
			int32 VertexCount = 0;
			int32 TriangleCount = 0;
			for (const FResolvedSurfaceCell& Cell : Visualization.Cells)
			{
				for (const auto& Patch : Cell.Patches)
				{
					if (Patch.LayerIndex == LayerIndex)
					{
						VertexCount += Patch.LocalVertices.Num();
						TriangleCount += Patch.LocalVertices.Num() - 2;
					}
				}
			}
			if (TriangleCount == 0 || !GEngine || !GEngine->GeomMaterial) { return; }

			FDynamicMeshBuilder MeshBuilder(PDI->View->GetFeatureLevel());
			MeshBuilder.ReserveVertices(VertexCount);
			MeshBuilder.ReserveTriangles(TriangleCount);
			for (const FResolvedSurfaceCell& Cell : Visualization.Cells)
			{
				for (const auto& Patch : Cell.Patches)
				{
					if (Patch.LayerIndex == LayerIndex) { AddSurfacePatch(MeshBuilder, Patch); }
				}
			}

			FDynamicColoredMaterialRenderProxy* MaterialProxy =
				new FDynamicColoredMaterialRenderProxy(
					GEngine->GeomMaterial->GetRenderProxy(),
					FillColor);
			PDI->RegisterDynamicResource(MaterialProxy);
			MeshBuilder.Draw(
				PDI,
				LocalToWorld.ToMatrixWithScale(),
				MaterialProxy,
				SDPG_World,
				true,
				false);
		}

		void DrawVisualizationImpl(
			FPrimitiveDrawInterface* PDI,
			const FTransform& LocalToWorld,
			const FResolvedSurfaceVisualization& Visualization,
			TConstArrayView<FComposableCameraMeshLayerDefinition> Layers)
		{
			for (int32 LayerIndex = 0; LayerIndex < Layers.Num(); ++LayerIndex)
			{
				const FComposableCameraMeshLayerDefinition& Layer = Layers[LayerIndex];
				if (!Layer.bEnabled)
				{
					continue;
				}

				FLinearColor FillColor = Layer.DebugColor;
				FillColor.A = FMath::Clamp(FillColor.A, 0.12f, 0.5f);
				DrawResolvedLayer(
					PDI,
					LocalToWorld,
					Visualization,
					LayerIndex,
					FillColor);
			}
		}
	}

	void DrawShapePreview(FPrimitiveDrawInterface* PDI, const FTransform& LocalToWorld, const FResolvedSurfaceLayerMesh& Mesh)
	{
		if (!PDI || !PDI->View || Mesh.Indices.IsEmpty() || !GEngine || !GEngine->GeomMaterial) { return; }
		FDynamicMeshBuilder MeshBuilder(PDI->View->GetFeatureLevel());
		MeshBuilder.ReserveVertices(Mesh.Indices.Num()); MeshBuilder.ReserveTriangles(Mesh.Indices.Num() / 3);
		for (int32 Index = 0; Index + 2 < Mesh.Indices.Num(); Index += 3)
		{
			FResolvedSurfacePatch Patch;
			for (int32 Corner = 0; Corner < 3; ++Corner) { Patch.LocalVertices.Add(Mesh.LocalVertices[Mesh.Indices[Index + Corner]]); }
			Patch.LocalNormal = FVector::CrossProduct(Patch.LocalVertices[1] - Patch.LocalVertices[0], Patch.LocalVertices[2] - Patch.LocalVertices[0]).GetSafeNormal();
			AddSurfacePatch(MeshBuilder, Patch);
		}
		auto* MaterialProxy = new FDynamicColoredMaterialRenderProxy(GEngine->GeomMaterial->GetRenderProxy(), FLinearColor(Mesh.Color));
		PDI->RegisterDynamicResource(MaterialProxy);
		MeshBuilder.Draw(PDI, LocalToWorld.ToMatrixWithScale(), MaterialProxy, SDPG_World, true, false);
	}

	void DrawVisualization(
		FPrimitiveDrawInterface* PDI,
		const FTransform& LocalToWorld,
		const FResolvedSurfaceVisualization& Visualization,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers)
	{
		if (!PDI || !PDI->View)
		{
			return;
		}
		DrawVisualizationImpl(PDI, LocalToWorld, Visualization, Layers);
	}

#if WITH_EDITORONLY_DATA
	void BuildAuthoringVisualization(
		const FComposableCameraMeshSurfaceAuthoringData& Data,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers,
		FResolvedSurfaceVisualization& OutVisualization)
	{
		OutVisualization.Reset();
		if (!Data.IsConsistent())
		{
			return;
		}

		TMap<FGuid, int32> LayerIndicesById;
		LayerIndicesById.Reserve(Layers.Num());
		for (int32 LayerIndex = 0; LayerIndex < Layers.Num(); ++LayerIndex)
		{
			LayerIndicesById.Add(Layers[LayerIndex].LayerId, LayerIndex);
		}

		BuildResolvedVisualization(
			Data.Vertices,
			Data.Indices,
			Data.Indices.Num() / 3,
			Layers,
			[&Data, &LayerIndicesById](int32 TriangleIndex) -> int32
			{
				if (const int32* LayerIndex =
					LayerIndicesById.Find(Data.TriangleLayerIds[TriangleIndex]))
				{
					return *LayerIndex;
				}
				return INDEX_NONE;
			},
			OutVisualization);
	}

	void UpdateAuthoringVisualization(const FComposableCameraMeshSurfaceAuthoringData& Data,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers, const FBox2D& DirtyBounds,
		FResolvedSurfaceVisualization& OutVisualization, FVisualizationUpdateStats* OutStats)
	{
		if (!Data.IsConsistent()) { OutVisualization.Reset(); if (OutStats) { *OutStats = {}; } return; }
		TMap<FGuid, int32> LayerIndicesById;
		LayerIndicesById.Reserve(Layers.Num());
		for (int32 Index = 0; Index < Layers.Num(); ++Index) { LayerIndicesById.Add(Layers[Index].LayerId, Index); }
		BuildResolvedVisualization(Data.Vertices, Data.Indices, Data.Indices.Num() / 3, Layers,
			[&Data, &LayerIndicesById](int32 Triangle) -> int32
			{
				const int32* Layer = LayerIndicesById.Find(Data.TriangleLayerIds[Triangle]);
				return Layer ? *Layer : INDEX_NONE;
			}, OutVisualization, &DirtyBounds, OutStats);
	}

#endif

	void BuildRuntimeVisualization(
		const FComposableCameraMeshSurfaceRuntimeData& Data,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers,
		FResolvedSurfaceVisualization& OutVisualization)
	{
		OutVisualization.Reset();
		if (!Data.IsConsistent())
		{
			return;
		}

		BuildResolvedVisualization(
			Data.Vertices,
			Data.Indices,
			Data.Indices.Num() / 3,
			Layers,
			[&Data](int32 TriangleIndex)
			{
				return Data.TriangleLayerIndices[TriangleIndex];
			},
			OutVisualization);
	}

	void BuildVisualizationMeshes(
		const FResolvedSurfaceVisualization& Visualization,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers,
		TArray<FResolvedSurfaceLayerMesh>& OutMeshes)
	{
		OutMeshes.Reset();
		for (int32 LayerIndex = 0; LayerIndex < Layers.Num(); ++LayerIndex)
		{
			const FComposableCameraMeshLayerDefinition& Layer = Layers[LayerIndex];
			if (!Layer.bEnabled)
			{
				continue;
			}

			int32 VertexCount = 0;
			int32 TriangleCount = 0;
			for (const FResolvedSurfaceCell& Cell : Visualization.Cells)
			{
				for (const auto& Patch : Cell.Patches)
				{
					if (Patch.LayerIndex == LayerIndex)
					{
						VertexCount += Patch.LocalVertices.Num();
						TriangleCount += Patch.LocalVertices.Num() - 2;
					}
				}
			}
			if (TriangleCount == 0) { continue; }

			FResolvedSurfaceLayerMesh& Mesh = OutMeshes.AddDefaulted_GetRef();
			Mesh.LayerIndex = LayerIndex;
			FLinearColor FillColor = Layer.DebugColor;
			FillColor.A = FMath::Clamp(FillColor.A, 0.12f, 0.5f);
			Mesh.Color = FillColor.ToFColor(true);
			Mesh.LocalVertices.Reserve(VertexCount);
			Mesh.Indices.Reserve(TriangleCount * 3);

			for (const FResolvedSurfaceCell& Cell : Visualization.Cells)
			{
				for (const auto& Patch : Cell.Patches)
				{
					if (Patch.LayerIndex != LayerIndex) { continue; }
					const int32 FirstVertex = Mesh.LocalVertices.Num();
					for (const FVector& Vertex : Patch.LocalVertices)
					{
						Mesh.LocalVertices.Add(Vertex + FVector::UpVector * SurfaceOffset);
					}
					for (int32 Index = 1; Index + 1 < Patch.LocalVertices.Num(); ++Index)
					{
						Mesh.Indices.Add(FirstVertex);
						Mesh.Indices.Add(FirstVertex + Index);
						Mesh.Indices.Add(FirstVertex + Index + 1);
					}
				}
			}
		}
	}

	bool ShouldDrawPIEPreview(
		bool bPreviewRequested,
		bool bPIEIsEnding,
		EWorldType::Type WorldType)
	{
		return bPreviewRequested
			&& !bPIEIsEnding
			&& WorldType == EWorldType::PIE;
	}

}
