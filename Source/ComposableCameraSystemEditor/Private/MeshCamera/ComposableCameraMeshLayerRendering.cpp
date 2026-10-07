// Copyright 2026 Sulley. All Rights Reserved.

#include "MeshCamera/ComposableCameraMeshLayerRendering.h"

#include "DynamicMeshBuilder.h"
#include "HAL/PlatformTime.h"
#include "EditorModes.h"
#include "Engine/Engine.h"
#include "Math/IntRect.h"
#include "MeshCamera/ComposableCameraMeshSurfaceTypes.h"
#include "MeshCamera/ComposableCameraMeshLayerAuthoringIndex.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "PrimitiveDrawInterface.h"
#include "SceneManagement.h"

namespace UE::ComposableCamera::MeshEditor
{
	namespace
	{
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

		void SubtractSurfaceCoverage(TConstArrayView<FVector> Subject, const FVector& SubjectNormal,
			TConstArrayView<FVector> Cut, const FVector& CutNormal, FSurfacePieces& OutPieces)
		{
			// Compare heights where footprints actually overlap, never at an
			// extrapolated grid center. A slope can cross the tolerance inside a cell;
			// clip that band before subtraction so distinct storeys remain visible.
			const FVector2D Gradient(-SubjectNormal.X / SubjectNormal.Z + CutNormal.X / CutNormal.Z,
				-SubjectNormal.Y / SubjectNormal.Z + CutNormal.Y / CutNormal.Z);
			const FVector& Reference = Cut[0];
			const double Delta = PlanePosition(Subject[0], SubjectNormal, Reference.X, Reference.Y).Z - Reference.Z;
			const double Constant = Delta - FVector2D::DotProduct(Gradient, FVector2D(Reference.X, Reference.Y));
			FSurfacePolygon SameSurfaceCut;
			if (Gradient.IsNearlyZero(UE_DOUBLE_SMALL_NUMBER))
			{
				if (FMath::Abs(Delta) <= SameSurfaceTolerance) { SameSurfaceCut.Append(Cut.GetData(), Cut.Num()); }
			}
			else
			{
				SameSurfaceCut = ClipHalfPlane(Cut, Gradient, -SameSurfaceTolerance - Constant);
				SameSurfaceCut = ClipHalfPlane(SameSurfaceCut, -Gradient, Constant - SameSurfaceTolerance);
			}
			if (PolygonArea(SameSurfaceCut) <= UE_DOUBLE_SMALL_NUMBER)
			{
				FSurfacePolygon Unchanged;
				Unchanged.Append(Subject.GetData(), Subject.Num());
				OutPieces.Add(MoveTemp(Unchanged));
				return;
			}
			SubtractPolygon(Subject, SameSurfaceCut, OutPieces);
		}

		void ResolveCoverage(FResolvedSurfaceCell& Cell, FSurfacePolygon Polygon, int32 LayerIndex,
			const FVector& Normal, double CellSize)
		{
			if (Cell.bFullCoverage && Cell.LayerIndex <= LayerIndex)
			{
				bool bCovered = true;
				for (const FVector& Vertex : Polygon)
				{
					bCovered &= FMath::Abs(Vertex.Z - PlanePosition(Cell.LocalPosition, Cell.LocalNormal, Vertex.X, Vertex.Y).Z) <= SameSurfaceTolerance;
				}
				if (bCovered) { return; }
			}
			FSurfacePieces Pending;
			Pending.Add(Polygon);
			auto Previous = MoveTemp(Cell.Patches);
			for (auto& Old : Previous)
			{
				if (Old.LayerIndex <= LayerIndex)
				{
					FSurfacePieces Next;
					for (const auto& Piece : Pending) { SubtractSurfaceCoverage(Piece, Normal, Old.LocalVertices, Old.LocalNormal, Next); }
					Pending = MoveTemp(Next);
					Cell.Patches.Add(MoveTemp(Old));
				}
				else
				{
					FSurfacePieces Pieces;
					SubtractSurfaceCoverage(Old.LocalVertices, Old.LocalNormal, Polygon, Normal, Pieces);
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

		bool RasterizeTriangle(const FVector& A, const FVector& B, const FVector& C, const FVector& Normal,
			int32 LayerIndex, int32 StartX, int32 EndX, int32 StartY, int32 EndY,
			FResolvedSurfaceVisualization& Visualization, const std::atomic_bool* Cancelled,
			FVisualizationUpdateStats* Stats = nullptr, const FIntRect* ExcludeCells = nullptr)
		{
			for (int32 CellY = StartY; CellY <= EndY; ++CellY)
			{
				if (Cancelled && Cancelled->load(std::memory_order_relaxed)) { return false; }
				for (int32 CellX = StartX; CellX <= EndX; ++CellX)
				{
					if (ExcludeCells && ExcludeCells->Contains(FIntPoint(CellX, CellY))) { continue; }
					if (Stats) { ++Stats->CellTests; }
					const FIntPoint Grid(CellX, CellY);
					FSurfacePolygon Polygon;
					Polygon.Append({A, B, C});
					const double X = CellX * Visualization.CellSize;
					const double Y = CellY * Visualization.CellSize;
					Polygon = ClipHalfPlane(Polygon, FVector2D(1.0, 0.0), X);
					Polygon = ClipHalfPlane(Polygon, FVector2D(-1.0, 0.0), -X - Visualization.CellSize);
					Polygon = ClipHalfPlane(Polygon, FVector2D(0.0, 1.0), Y);
					Polygon = ClipHalfPlane(Polygon, FVector2D(0.0, -1.0), -Y - Visualization.CellSize);
					if (PolygonArea(Polygon) <= UE_DOUBLE_SMALL_NUMBER) { continue; }
					if (const auto* Existing = Visualization.CellsByGrid.Find(Grid))
					{
						ResolveCoverage(Visualization.Cells[(*Existing)[0]], MoveTemp(Polygon), LayerIndex, Normal, Visualization.CellSize);
					}
					else
					{
						FResolvedSurfaceCell Cell;
						Cell.LocalPosition = PlanePosition(A, Normal, (CellX + 0.5) * Visualization.CellSize,
							(CellY + 0.5) * Visualization.CellSize);
						Cell.LocalNormal = Normal; Cell.LayerIndex = LayerIndex;
						ResolveCoverage(Cell, MoveTemp(Polygon), LayerIndex, Normal, Visualization.CellSize);
						const int32 Index = Visualization.Cells.Add(MoveTemp(Cell));
						Visualization.CellsByGrid.FindOrAdd(Grid).Add(Index);
					}
				}
			}
			return true;
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
			FVisualizationUpdateStats* OutStats = nullptr, const std::atomic_bool* Cancelled = nullptr,
			const FMeshLayerAuthoringIndex* SourceIndex = nullptr, int32 AppendFrom = INDEX_NONE)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(CCS_MeshLayers_CoverageUpdate);
			if (OutStats) { *OutStats = {}; }
			if (TriangleCount <= 0 || Layers.IsEmpty())
			{
				OutVisualization.Reset();
				return;
			}

			FBox2D ProjectedBounds(ForceInit);
			if (SourceIndex) { ProjectedBounds = SourceIndex->GetProjectedBounds(Layers); }
			else for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
			{
				if (OutStats) { ++OutStats->SourceTriangleTests; }
				if (Cancelled && Cancelled->load(std::memory_order_relaxed)) { OutVisualization.Reset(); return; }
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
				for (int32 Y = DirtyMin.Y; AppendFrom == INDEX_NONE && Y <= DirtyMax.Y; ++Y)
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
			TArray<int32> Candidates;
			const bool bUseCandidates = SourceIndex && !bFullRebuild && AppendFrom == INDEX_NONE;
			if (bUseCandidates)
			{
				const FBox2D CellBounds(FVector2D(DirtyMin.X * OutVisualization.CellSize, DirtyMin.Y * OutVisualization.CellSize),
					FVector2D((DirtyMax.X + 1) * OutVisualization.CellSize, (DirtyMax.Y + 1) * OutVisualization.CellSize));
				SourceIndex->FindVisualizationCandidates(CellBounds, Layers, Candidates);
			}
			const int32 FirstTriangle = !bFullRebuild && AppendFrom != INDEX_NONE ? AppendFrom : 0;
			const int32 WorkCount = bUseCandidates ? Candidates.Num() : TriangleCount - FirstTriangle;
			for (int32 WorkIndex = 0; WorkIndex < WorkCount; ++WorkIndex)
			{
				const int32 TriangleIndex = bUseCandidates ? Candidates[WorkIndex] : FirstTriangle + WorkIndex;
				if (OutStats) { ++OutStats->SourceTriangleTests; }
				if (Cancelled && Cancelled->load(std::memory_order_relaxed)) { OutVisualization.Reset(); return; }
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

				if (!RasterizeTriangle(A, B, C, Normal, LayerIndex, StartX, EndX, StartY, EndY,
					OutVisualization, Cancelled, OutStats)) { OutVisualization.Reset(); return; }
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
					FVector3f(Vertex + FVector::UpVector * VisualizationSurfaceOffset),
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

#if WITH_EDITORONLY_DATA
	struct FAuthoringVisualizationUpdate::FState
	{
		TMap<FGuid, int32> LayerIndices;
		TArray<int32> Candidates;
		FVisualizationUpdateStats Stats;
		FIntPoint DirtyMin = FIntPoint(0, 0), DirtyMax = FIntPoint(0, 0);
		int32 ClearX = 0, ClearY = 0, FirstTriangle = 0, Cursor = 0, WorkCount = 0;
		int32 StartX = 0, EndX = 0, EndY = 0, CellX = 0, CellY = 0, LayerIndex = INDEX_NONE;
		FVector A, B, C, Normal;
		bool bCandidates = false, bClearing = false, bRasterizing = false, bFinished = true;
	};
	FAuthoringVisualizationUpdate::FAuthoringVisualizationUpdate() : State(MakeUnique<FState>()) {}
	FAuthoringVisualizationUpdate::~FAuthoringVisualizationUpdate() = default;
	const FVisualizationUpdateStats& FAuthoringVisualizationUpdate::GetStats() const { return State->Stats; }

	double GetAuthoringVisualizationCellSize(const FBox2D& Bounds)
	{
		const FVector2D Size = Bounds.bIsValid ? Bounds.GetSize() : FVector2D::ZeroVector;
		return FMath::Max(MinimumCellSize, FMath::Sqrt(Size.X * Size.Y / TargetVisibleCellCount));
	}

	void FAuthoringVisualizationUpdate::Begin(const FComposableCameraMeshSurfaceAuthoringData& Data,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers, FResolvedSurfaceVisualization& Visualization,
		const FBox2D* DirtyBounds, const FMeshLayerAuthoringIndex* SourceIndex, int32 AppendFrom,
		const FBox2D* SnapshotBounds, double SnapshotCellSize)
	{
		auto& S = *State; S = FState();
		if (!Data.IsConsistent() || (!SnapshotBounds && Data.TriangleLayerIds.IsEmpty()) || Layers.IsEmpty()) { Visualization.Reset(); return; }
		for (int32 Index = 0; Index < Layers.Num(); ++Index) { S.LayerIndices.Add(Layers[Index].LayerId, Index); }
		if (SourceIndex && !SourceIndex->IsCurrent(Data)) { SourceIndex = nullptr; }
		FBox2D ProjectedBounds(ForceInit);
		if (SnapshotBounds) { ProjectedBounds = *SnapshotBounds; }
		else if (SourceIndex) { ProjectedBounds = SourceIndex->GetProjectedBounds(Layers); }
		else for (int32 Triangle = 0; Triangle < Data.TriangleLayerIds.Num(); ++Triangle)
		{
			++S.Stats.SourceTriangleTests;
			const int32* Layer = S.LayerIndices.Find(Data.TriangleLayerIds[Triangle]);
			if (!Layer || !Layers[*Layer].bEnabled) { continue; }
			FVector A, B, C;
			if (GetTriangle(Data.Vertices, Data.Indices, Triangle, A, B, C) && GetProjectedTriangleArea(A, B, C) > UE_DOUBLE_SMALL_NUMBER)
			{
				ProjectedBounds += FVector2D(A.X, A.Y); ProjectedBounds += FVector2D(B.X, B.Y); ProjectedBounds += FVector2D(C.X, C.Y);
			}
		}
		if (!ProjectedBounds.bIsValid) { Visualization.Reset(); return; }
		const FVector2D Size = ProjectedBounds.GetSize();
		const double Area = Size.X * Size.Y;
		const double DesiredCellSize = SnapshotCellSize > 0.0 ? SnapshotCellSize : GetAuthoringVisualizationCellSize(ProjectedBounds);
		S.Stats.bFullRebuild = !DirtyBounds || !DirtyBounds->bIsValid || !Visualization.LocalBounds.bIsValid
			|| DesiredCellSize > Visualization.CellSize * 1.25;
		if (S.Stats.bFullRebuild)
		{
			Visualization.Reset(); Visualization.CellSize = DesiredCellSize;
			const int32 EstimatedCells = FMath::Max(1, FMath::CeilToInt(Area / FMath::Square(DesiredCellSize)));
			Visualization.Cells.Reserve(EstimatedCells); Visualization.CellsByGrid.Reserve(EstimatedCells);
		}
		else
		{
			FBox2D Bounds = Visualization.LocalBounds; Bounds += ProjectedBounds;
			S.DirtyMin = FIntPoint(FMath::FloorToInt(FMath::Max(DirtyBounds->Min.X, Bounds.Min.X) / Visualization.CellSize),
				FMath::FloorToInt(FMath::Max(DirtyBounds->Min.Y, Bounds.Min.Y) / Visualization.CellSize));
			S.DirtyMax = FIntPoint(FMath::FloorToInt(FMath::Min(DirtyBounds->Max.X, Bounds.Max.X) / Visualization.CellSize),
				FMath::FloorToInt(FMath::Min(DirtyBounds->Max.Y, Bounds.Max.Y) / Visualization.CellSize));
			S.ClearX = S.DirtyMin.X; S.ClearY = S.DirtyMin.Y;
			S.bClearing = AppendFrom == INDEX_NONE && S.DirtyMin.X <= S.DirtyMax.X && S.DirtyMin.Y <= S.DirtyMax.Y;
		}
		Visualization.LocalBounds = ProjectedBounds;
		S.bCandidates = SourceIndex && !S.Stats.bFullRebuild && AppendFrom == INDEX_NONE;
		if (S.bCandidates)
		{
			const FBox2D Bounds(FVector2D(S.DirtyMin.X * Visualization.CellSize, S.DirtyMin.Y * Visualization.CellSize),
				FVector2D((S.DirtyMax.X + 1) * Visualization.CellSize, (S.DirtyMax.Y + 1) * Visualization.CellSize));
			SourceIndex->FindVisualizationCandidates(Bounds, Layers, S.Candidates);
		}
		S.FirstTriangle = !S.Stats.bFullRebuild && AppendFrom != INDEX_NONE ? AppendFrom : 0;
		S.WorkCount = S.bCandidates ? S.Candidates.Num() : Data.TriangleLayerIds.Num() - S.FirstTriangle;
		S.bFinished = false;
	}

	bool FAuthoringVisualizationUpdate::Advance(const FComposableCameraMeshSurfaceAuthoringData& Data,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers, FResolvedSurfaceVisualization& Visualization,
		int32 MaxOperations, double TimeBudgetSeconds)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CCS_MeshLayers_CoverageUpdate);
		auto& S = *State;
		const double Started = FPlatformTime::Seconds();
		int32 Operations = 0;
		while (!S.bFinished)
		{
			if (Operations >= MaxOperations || (TimeBudgetSeconds > 0.0 && FPlatformTime::Seconds() - Started >= TimeBudgetSeconds)) { return false; }
			++Operations;
			if (S.bClearing)
			{
				RemoveGridCells(Visualization, FIntPoint(S.ClearX, S.ClearY));
				if (++S.ClearX > S.DirtyMax.X) { S.ClearX = S.DirtyMin.X; ++S.ClearY; }
				S.bClearing = S.ClearY <= S.DirtyMax.Y;
				continue;
			}
			if (S.bRasterizing)
			{
				RasterizeTriangle(S.A, S.B, S.C, S.Normal, S.LayerIndex, S.CellX, S.CellX, S.CellY, S.CellY,
					Visualization, nullptr, &S.Stats);
				if (++S.CellX > S.EndX) { S.CellX = S.StartX; ++S.CellY; }
				S.bRasterizing = S.CellY <= S.EndY;
				continue;
			}
			if (S.Cursor >= S.WorkCount) { S.bFinished = true; break; }
			const int32 Triangle = S.bCandidates ? S.Candidates[S.Cursor++] : S.FirstTriangle + S.Cursor++;
			++S.Stats.SourceTriangleTests;
			const int32* Layer = S.LayerIndices.Find(Data.TriangleLayerIds[Triangle]);
			if (!Layer || !Layers[*Layer].bEnabled) { continue; }
			S.LayerIndex = *Layer;
			if (!GetTriangle(Data.Vertices, Data.Indices, Triangle, S.A, S.B, S.C)
				|| GetProjectedTriangleArea(S.A, S.B, S.C) <= UE_DOUBLE_SMALL_NUMBER) { continue; }
			S.Normal = FVector::CrossProduct(S.B - S.A, S.C - S.A).GetSafeNormal();
			if (S.Normal.IsNearlyZero()) { continue; }
			if (S.Normal.Z < 0.0) { S.Normal *= -1.0; Swap(S.B, S.C); }
			if (S.Normal.Z <= UE_DOUBLE_KINDA_SMALL_NUMBER) { continue; }
			const int32 MinX = FMath::FloorToInt(FMath::Min3(S.A.X, S.B.X, S.C.X) / Visualization.CellSize);
			const int32 MaxX = FMath::FloorToInt(FMath::Max3(S.A.X, S.B.X, S.C.X) / Visualization.CellSize);
			const int32 MinY = FMath::FloorToInt(FMath::Min3(S.A.Y, S.B.Y, S.C.Y) / Visualization.CellSize);
			const int32 MaxY = FMath::FloorToInt(FMath::Max3(S.A.Y, S.B.Y, S.C.Y) / Visualization.CellSize);
			S.StartX = S.Stats.bFullRebuild ? MinX : FMath::Max(MinX, S.DirtyMin.X);
			S.EndX = S.Stats.bFullRebuild ? MaxX : FMath::Min(MaxX, S.DirtyMax.X);
			S.CellY = S.Stats.bFullRebuild ? MinY : FMath::Max(MinY, S.DirtyMin.Y);
			S.EndY = S.Stats.bFullRebuild ? MaxY : FMath::Min(MaxY, S.DirtyMax.Y);
			S.CellX = S.StartX;
			if (S.StartX > S.EndX || S.CellY > S.EndY) { continue; }
			++S.Stats.RasterizedTriangles; S.bRasterizing = true;
		}
		return S.bFinished;
	}
#endif


	void AppendVisualizationPatch(const FResolvedSurfacePatch& Patch,
		TArray<FDynamicMeshVertex>& Vertices, TArray<uint32>& Indices)
	{
		if (Patch.LocalVertices.Num() < 3) { return; }
		const FVector TangentX = (Patch.LocalVertices[1] - Patch.LocalVertices[0]).GetSafeNormal();
		const FVector TangentY = FVector::CrossProduct(Patch.LocalNormal, TangentX).GetSafeNormal();
		const uint32 FirstVertex = Vertices.Num();
		for (const FVector& Position : Patch.LocalVertices)
		{
			FDynamicMeshVertex Vertex(FVector3f(Position + FVector::UpVector * VisualizationSurfaceOffset));
			Vertex.SetTangents(FVector3f(TangentX), FVector3f(TangentY), FVector3f(Patch.LocalNormal));
			Vertices.Add(Vertex);
		}
		for (int32 Index = 1; Index + 1 < Patch.LocalVertices.Num(); ++Index)
		{
			Indices.Append({FirstVertex, FirstVertex + static_cast<uint32>(Index), FirstVertex + static_cast<uint32>(Index + 1)});
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
		FResolvedSurfaceVisualization& OutVisualization, FVisualizationUpdateStats* OutStats, const FMeshLayerAuthoringIndex* SourceIndex)
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
			}, OutVisualization, &DirtyBounds, OutStats, nullptr,
			SourceIndex && SourceIndex->IsCurrent(Data) ? SourceIndex : nullptr);
	}

	void AppendAuthoringVisualization(const FComposableCameraMeshSurfaceAuthoringData& Data,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers, int32 FirstAddedTriangle, const FBox2D& DirtyBounds,
		FResolvedSurfaceVisualization& OutVisualization, FVisualizationUpdateStats* OutStats, const FMeshLayerAuthoringIndex* SourceIndex)
	{
		if (!Data.IsConsistent() || FirstAddedTriangle < 0 || FirstAddedTriangle > Data.TriangleLayerIds.Num())
		{
			BuildAuthoringVisualization(Data, Layers, OutVisualization);
			if (OutStats) { *OutStats = {}; OutStats->bFullRebuild = true; }
			return;
		}
		TMap<FGuid, int32> LayerIndices;
		for (int32 Index = 0; Index < Layers.Num(); ++Index) { LayerIndices.Add(Layers[Index].LayerId, Index); }
		BuildResolvedVisualization(Data.Vertices, Data.Indices, Data.TriangleLayerIds.Num(), Layers,
			[&](int32 Triangle)
			{
				const int32* Layer = LayerIndices.Find(Data.TriangleLayerIds[Triangle]);
				return Layer ? *Layer : INDEX_NONE;
			}, OutVisualization, &DirtyBounds, OutStats, nullptr,
			SourceIndex && SourceIndex->IsCurrent(Data) ? SourceIndex : nullptr, FirstAddedTriangle);
	}

#endif

	void BuildRuntimeVisualization(
		const FComposableCameraMeshSurfaceRuntimeData& Data,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers,
		FResolvedSurfaceVisualization& OutVisualization, const std::atomic_bool* Cancelled)
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
			OutVisualization, nullptr, nullptr, Cancelled);
	}

	void BuildRuntimeVisualizationTiles(const FComposableCameraMeshSurfaceRuntimeData& Data,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers, const FVector2D& LocalFocus,
		TFunctionRef<void(FResolvedSurfaceVisualization&&)> Publish, const std::atomic_bool* Cancelled)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CCS_MeshLayers_StreamingCoverage);
		if (!Data.IsConsistent() || Layers.IsEmpty()) { return; }
		auto IsCancelled = [&]() { return Cancelled && Cancelled->load(std::memory_order_relaxed); };
		FBox2D Bounds(ForceInit);
		// Only bounds and lightweight candidate binning precede the first final tile.
		// Do not clip/export the entire document before publishing anything.
		for (int32 Triangle = 0; Triangle < Data.TriangleLayerIndices.Num(); ++Triangle)
		{
			if (IsCancelled()) { return; }
			const int32 Layer = Data.TriangleLayerIndices[Triangle];
			FVector A, B, C;
			if (!Layers.IsValidIndex(Layer) || !Layers[Layer].bEnabled
				|| !GetTriangle(Data.Vertices, Data.Indices, Triangle, A, B, C)
				|| GetProjectedTriangleArea(A, B, C) <= UE_DOUBLE_SMALL_NUMBER) { continue; }
			Bounds += FVector2D(A.X, A.Y); Bounds += FVector2D(B.X, B.Y); Bounds += FVector2D(C.X, C.Y);
		}
		if (!Bounds.bIsValid) { return; }
		const FVector2D Size = Bounds.GetSize();
		const double CellSize = FMath::Max(MinimumCellSize, FMath::Sqrt(Size.X * Size.Y / TargetVisibleCellCount));
		constexpr int32 TileCells = 32;
		struct FTileTriangle
		{
			FVector A, B, C, Normal;
			FIntPoint Min, Max;
			int32 Layer;
		};
		TArray<FTileTriangle> Triangles;
		Triangles.Reserve(Data.TriangleLayerIndices.Num());
		TMap<FIntPoint, TArray<int32>> Bins;
		for (int32 Triangle = 0; Triangle < Data.TriangleLayerIndices.Num(); ++Triangle)
		{
			if (IsCancelled()) { return; }
			const int32 Layer = Data.TriangleLayerIndices[Triangle];
			FTileTriangle Prepared;
			if (!Layers.IsValidIndex(Layer) || !Layers[Layer].bEnabled
				|| !GetTriangle(Data.Vertices, Data.Indices, Triangle, Prepared.A, Prepared.B, Prepared.C)
				|| GetProjectedTriangleArea(Prepared.A, Prepared.B, Prepared.C) <= UE_DOUBLE_SMALL_NUMBER) { continue; }
			Prepared.Normal = FVector::CrossProduct(Prepared.B - Prepared.A, Prepared.C - Prepared.A).GetSafeNormal();
			if (Prepared.Normal.Z < 0.0) { Prepared.Normal *= -1.0; Swap(Prepared.B, Prepared.C); }
			if (Prepared.Normal.Z <= UE_DOUBLE_KINDA_SMALL_NUMBER) { continue; }
			Prepared.Layer = Layer;
			Prepared.Min = FIntPoint(FMath::FloorToInt(FMath::Min3(Prepared.A.X, Prepared.B.X, Prepared.C.X) / CellSize),
				FMath::FloorToInt(FMath::Min3(Prepared.A.Y, Prepared.B.Y, Prepared.C.Y) / CellSize));
			Prepared.Max = FIntPoint(FMath::FloorToInt(FMath::Max3(Prepared.A.X, Prepared.B.X, Prepared.C.X) / CellSize),
				FMath::FloorToInt(FMath::Max3(Prepared.A.Y, Prepared.B.Y, Prepared.C.Y) / CellSize));
			const FIntPoint MinTile(FMath::FloorToInt(static_cast<double>(Prepared.Min.X) / TileCells),
				FMath::FloorToInt(static_cast<double>(Prepared.Min.Y) / TileCells));
			const FIntPoint MaxTile(FMath::FloorToInt(static_cast<double>(Prepared.Max.X) / TileCells),
				FMath::FloorToInt(static_cast<double>(Prepared.Max.Y) / TileCells));
			const int32 Index = Triangles.Add(MoveTemp(Prepared));
			for (int32 Y = MinTile.Y; Y <= MaxTile.Y; ++Y)
			{
				if (IsCancelled()) { return; }
				for (int32 X = MinTile.X; X <= MaxTile.X; ++X) { Bins.FindOrAdd(FIntPoint(X, Y)).Add(Index); }
			}
		}
		TArray<FIntPoint> Order;
		Bins.GenerateKeyArray(Order);
		auto Distance = [&](const FIntPoint& Tile)
		{
			return (FVector2D((Tile.X + 0.5) * TileCells * CellSize, (Tile.Y + 0.5) * TileCells * CellSize) - LocalFocus).SizeSquared();
		};
		Order.Sort([&](const FIntPoint& A, const FIntPoint& B)
		{
			const double DA = Distance(A), DB = Distance(B);
			return DA != DB ? DA < DB : (A.Y != B.Y ? A.Y < B.Y : A.X < B.X);
		});
		bool bPublishedFirst = false;
		for (const FIntPoint& Key : Order)
		{
			if (IsCancelled()) { return; }
			// Source order is preserved within each cell. All competing Layers are
			// resolved before this tile escapes; later tiles cannot change its colors.
			auto ResolveTile = [&](const FIntRect& Cells, const FIntRect* Exclude, FResolvedSurfaceVisualization& Tile)
			{
				Tile.CellSize = CellSize; Tile.LocalBounds = Bounds;
				Tile.Cells.Reserve(Cells.Area()); Tile.CellsByGrid.Reserve(Cells.Area());
				for (int32 Index : Bins.FindChecked(Key))
				{
					const auto& Triangle = Triangles[Index];
					if (!RasterizeTriangle(Triangle.A, Triangle.B, Triangle.C, Triangle.Normal, Triangle.Layer,
						FMath::Max(Triangle.Min.X, Cells.Min.X), FMath::Min(Triangle.Max.X, Cells.Max.X - 1),
						FMath::Max(Triangle.Min.Y, Cells.Min.Y), FMath::Min(Triangle.Max.Y, Cells.Max.Y - 1),
						Tile, Cancelled, nullptr, Exclude)) { return false; }
				}
				return true;
			};
			const FIntRect TileCellsRect(Key.X * TileCells, Key.Y * TileCells, (Key.X + 1) * TileCells, (Key.Y + 1) * TileCells);
			FIntRect FirstCells;
			bool bExcludeFirst = false;
			if (!bPublishedFirst)
			{
				// A tiny final region gives immediate progress without waiting even
				// for the first regular tile. It uses complete coverage, not a draft.
				constexpr int32 FirstSide = 8;
				const int32 X = FMath::Clamp(FMath::FloorToInt(LocalFocus.X / CellSize) - FirstSide / 2,
					TileCellsRect.Min.X, TileCellsRect.Max.X - FirstSide);
				const int32 Y = FMath::Clamp(FMath::FloorToInt(LocalFocus.Y / CellSize) - FirstSide / 2,
					TileCellsRect.Min.Y, TileCellsRect.Max.Y - FirstSide);
				FirstCells = FIntRect(X, Y, X + FirstSide, Y + FirstSide);
				FResolvedSurfaceVisualization First;
				if (!ResolveTile(FirstCells, nullptr, First)) { return; }
				if (!First.Cells.IsEmpty())
				{
					Publish(MoveTemp(First)); bPublishedFirst = bExcludeFirst = true;
					if (IsCancelled()) { return; }
				}
			}
			FResolvedSurfaceVisualization Tile;
			if (!ResolveTile(TileCellsRect, bExcludeFirst ? &FirstCells : nullptr, Tile)) { return; }
			if (!Tile.Cells.IsEmpty()) { Publish(MoveTemp(Tile)); bPublishedFirst = true; }
		}
	}

	void BuildVisualizationMeshes(
		const FResolvedSurfaceVisualization& Visualization,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers,
		TArray<FResolvedSurfaceLayerMesh>& OutMeshes, const std::atomic_bool* Cancelled)
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
				if (Cancelled && Cancelled->load(std::memory_order_relaxed)) { OutMeshes.Reset(); return; }
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
				if (Cancelled && Cancelled->load(std::memory_order_relaxed)) { OutMeshes.Reset(); return; }
				for (const auto& Patch : Cell.Patches)
				{
					if (Patch.LayerIndex != LayerIndex) { continue; }
					const int32 FirstVertex = Mesh.LocalVertices.Num();
					for (const FVector& Vertex : Patch.LocalVertices)
					{
						Mesh.LocalVertices.Add(Vertex + FVector::UpVector * VisualizationSurfaceOffset);
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
