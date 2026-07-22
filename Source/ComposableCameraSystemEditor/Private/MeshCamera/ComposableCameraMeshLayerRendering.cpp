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

		bool GetBarycentric2D(
			const FVector2D& Point,
			const FVector& A,
			const FVector& B,
			const FVector& C,
			FVector& OutBarycentric)
		{
			const double Denominator =
				(B.Y - C.Y) * (A.X - C.X)
				+ (C.X - B.X) * (A.Y - C.Y);
			if (FMath::IsNearlyZero(Denominator))
			{
				return false;
			}

			OutBarycentric.X =
				((B.Y - C.Y) * (Point.X - C.X)
					+ (C.X - B.X) * (Point.Y - C.Y)) / Denominator;
			OutBarycentric.Y =
				((C.Y - A.Y) * (Point.X - C.X)
					+ (A.X - C.X) * (Point.Y - C.Y)) / Denominator;
			OutBarycentric.Z = 1.0 - OutBarycentric.X - OutBarycentric.Y;
			constexpr double BarycentricTolerance = -UE_DOUBLE_KINDA_SMALL_NUMBER;
			return OutBarycentric.X >= BarycentricTolerance
				&& OutBarycentric.Y >= BarycentricTolerance
				&& OutBarycentric.Z >= BarycentricTolerance;
		}

		template <typename ResolveLayerIndexType>
		void BuildResolvedVisualization(
			TConstArrayView<FVector3f> Vertices,
			TConstArrayView<int32> Indices,
			int32 TriangleCount,
			TConstArrayView<FComposableCameraMeshLayerDefinition> Layers,
			ResolveLayerIndexType&& ResolveLayerIndex,
			FResolvedSurfaceVisualization& OutVisualization)
		{
			OutVisualization.CellSize = MinimumCellSize;
			OutVisualization.Cells.Reset();
			if (TriangleCount <= 0 || Layers.IsEmpty())
			{
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
				return;
			}

			const FVector2D ProjectedSize = ProjectedBounds.GetSize();
			const double ProjectedBoundsArea = ProjectedSize.X * ProjectedSize.Y;
			OutVisualization.CellSize = FMath::Max(
				MinimumCellSize,
				FMath::Sqrt(ProjectedBoundsArea / TargetVisibleCellCount));
			const int32 EstimatedGridCellCount = FMath::Max(
				1,
				FMath::CeilToInt(
					ProjectedBoundsArea
					/ FMath::Square(OutVisualization.CellSize)));
			OutVisualization.Cells.Reserve(EstimatedGridCellCount);
			TMap<FIntPoint, FSurfaceCellIndices> SurfaceCellsByGrid;
			SurfaceCellsByGrid.Reserve(EstimatedGridCellCount);

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
				}

				const int32 MinCellX = FMath::FloorToInt(
					FMath::Min3(A.X, B.X, C.X) / OutVisualization.CellSize);
				const int32 MaxCellX = FMath::FloorToInt(
					FMath::Max3(A.X, B.X, C.X) / OutVisualization.CellSize);
				const int32 MinCellY = FMath::FloorToInt(
					FMath::Min3(A.Y, B.Y, C.Y) / OutVisualization.CellSize);
				const int32 MaxCellY = FMath::FloorToInt(
					FMath::Max3(A.Y, B.Y, C.Y) / OutVisualization.CellSize);

				auto AddSample = [
					&OutVisualization,
					&SurfaceCellsByGrid,
					&Layers,
					&A,
					&B,
					&C,
					&Normal,
					LayerIndex](int32 CellX, int32 CellY, const FVector& Barycentric)
				{
					const FVector SamplePosition(
						(CellX + 0.5) * OutVisualization.CellSize,
						(CellY + 0.5) * OutVisualization.CellSize,
						A.Z * Barycentric.X + B.Z * Barycentric.Y + C.Z * Barycentric.Z);
					FSurfaceCellIndices& SurfaceCellIndices =
						SurfaceCellsByGrid.FindOrAdd(FIntPoint(CellX, CellY));

					for (const int32 SurfaceCellIndex : SurfaceCellIndices)
					{
						FResolvedSurfaceCell& ExistingCell =
							OutVisualization.Cells[SurfaceCellIndex];
						if (FMath::Abs(ExistingCell.LocalPosition.Z - SamplePosition.Z)
							> SameSurfaceTolerance)
						{
							continue;
						}

						// Layer list matches image-editor convention: row 0 is topmost.
						if (LayerIndex < ExistingCell.LayerIndex)
						{
							ExistingCell.LocalPosition = SamplePosition;
							ExistingCell.LocalNormal = Normal;
							ExistingCell.LayerIndex = LayerIndex;
						}
						return;
					}

					FResolvedSurfaceCell NewCell;
					NewCell.LocalPosition = SamplePosition;
					NewCell.LocalNormal = Normal;
					NewCell.LayerIndex = LayerIndex;
					const int32 NewCellIndex = OutVisualization.Cells.Add(MoveTemp(NewCell));
					SurfaceCellIndices.Add(NewCellIndex);
				};

				bool bRasterizedTriangle = false;
				for (int32 CellY = MinCellY; CellY <= MaxCellY; ++CellY)
				{
					for (int32 CellX = MinCellX; CellX <= MaxCellX; ++CellX)
					{
						const FVector2D SamplePoint(
							(CellX + 0.5) * OutVisualization.CellSize,
							(CellY + 0.5) * OutVisualization.CellSize);
						FVector Barycentric;
						if (GetBarycentric2D(SamplePoint, A, B, C, Barycentric))
						{
							AddSample(CellX, CellY, Barycentric);
							bRasterizedTriangle = true;
						}
					}
				}

				if (!bRasterizedTriangle)
				{
					const FVector Centroid = (A + B + C) / 3.0;
					AddSample(
						FMath::FloorToInt(Centroid.X / OutVisualization.CellSize),
						FMath::FloorToInt(Centroid.Y / OutVisualization.CellSize),
						FVector(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0));
				}
			}
		}

		bool AddSurfaceCell(
			FDynamicMeshBuilder& MeshBuilder,
			const FResolvedSurfaceCell& Cell,
			double CellSize)
		{
			FVector Normal = Cell.LocalNormal.GetSafeNormal();
			if (Normal.IsNearlyZero() || FMath::Abs(Normal.Z) <= UE_DOUBLE_KINDA_SMALL_NUMBER)
			{
				return false;
			}
			if (Normal.Z < 0.0)
			{
				Normal *= -1.0;
			}

			const double HalfCellSize = CellSize * 0.5;
			auto MakeCorner = [&Cell, &Normal](double DeltaX, double DeltaY)
			{
				const double DeltaZ =
					-(Normal.X * DeltaX + Normal.Y * DeltaY) / Normal.Z;
				return Cell.LocalPosition
					+ FVector(DeltaX, DeltaY, DeltaZ)
					+ Normal * SurfaceOffset;
			};

			const FVector A = MakeCorner(-HalfCellSize, -HalfCellSize);
			const FVector B = MakeCorner(HalfCellSize, -HalfCellSize);
			const FVector C = MakeCorner(HalfCellSize, HalfCellSize);
			const FVector D = MakeCorner(-HalfCellSize, HalfCellSize);
			const FVector TangentX = (B - A).GetSafeNormal();
			const FVector TangentY = FVector::CrossProduct(Normal, TangentX).GetSafeNormal();
			const int32 FirstVertex = MeshBuilder.AddVertex(
				FVector3f(A),
				FVector2f::ZeroVector,
				FVector3f(TangentX),
				FVector3f(TangentY),
				FVector3f(Normal),
				FColor::White);
			const FVector RemainingVertices[] = {B, C, D};
			for (const FVector& Vertex : RemainingVertices)
			{
				MeshBuilder.AddVertex(
					FVector3f(Vertex),
					FVector2f::ZeroVector,
					FVector3f(TangentX),
					FVector3f(TangentY),
					FVector3f(Normal),
					FColor::White);
			}
			MeshBuilder.AddTriangle(FirstVertex, FirstVertex + 1, FirstVertex + 2);
			MeshBuilder.AddTriangle(FirstVertex, FirstVertex + 2, FirstVertex + 3);
			return true;
		}

		bool GetSurfaceCellCorners(
			const FResolvedSurfaceCell& Cell,
			double CellSize,
			FVector (&OutCorners)[4])
		{
			FVector Normal = Cell.LocalNormal.GetSafeNormal();
			if (Normal.IsNearlyZero() || FMath::Abs(Normal.Z) <= UE_DOUBLE_KINDA_SMALL_NUMBER)
			{
				return false;
			}
			if (Normal.Z < 0.0)
			{
				Normal *= -1.0;
			}

			const double HalfCellSize = CellSize * 0.5;
			auto MakeCorner = [&Cell, &Normal](double DeltaX, double DeltaY)
			{
				const double DeltaZ =
					-(Normal.X * DeltaX + Normal.Y * DeltaY) / Normal.Z;
				return Cell.LocalPosition
					+ FVector(DeltaX, DeltaY, DeltaZ)
					+ Normal * SurfaceOffset;
			};

			OutCorners[0] = MakeCorner(-HalfCellSize, -HalfCellSize);
			OutCorners[1] = MakeCorner(HalfCellSize, -HalfCellSize);
			OutCorners[2] = MakeCorner(HalfCellSize, HalfCellSize);
			OutCorners[3] = MakeCorner(-HalfCellSize, HalfCellSize);
			return true;
		}

		void DrawResolvedLayer(
			FPrimitiveDrawInterface* PDI,
			const FTransform& LocalToWorld,
			const FResolvedSurfaceVisualization& Visualization,
			int32 LayerIndex,
			const FLinearColor& FillColor)
		{
			int32 LayerCellCount = 0;
			for (const FResolvedSurfaceCell& Cell : Visualization.Cells)
			{
				LayerCellCount += Cell.LayerIndex == LayerIndex ? 1 : 0;
			}
			if (LayerCellCount == 0 || !GEngine || !GEngine->GeomMaterial)
			{
				return;
			}

			FDynamicMeshBuilder MeshBuilder(PDI->View->GetFeatureLevel());
			MeshBuilder.ReserveVertices(LayerCellCount * 4);
			MeshBuilder.ReserveTriangles(LayerCellCount * 2);
			int32 AddedCellCount = 0;
			for (const FResolvedSurfaceCell& Cell : Visualization.Cells)
			{
				if (Cell.LayerIndex == LayerIndex
					&& AddSurfaceCell(MeshBuilder, Cell, Visualization.CellSize))
				{
					++AddedCellCount;
				}
			}
			if (AddedCellCount == 0)
			{
				return;
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
		OutVisualization.Cells.Reset();
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

#endif

	void BuildRuntimeVisualization(
		const FComposableCameraMeshSurfaceRuntimeData& Data,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers,
		FResolvedSurfaceVisualization& OutVisualization)
	{
		OutVisualization.Cells.Reset();
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

			int32 CellCount = 0;
			for (const FResolvedSurfaceCell& Cell : Visualization.Cells)
			{
				CellCount += Cell.LayerIndex == LayerIndex ? 1 : 0;
			}
			if (CellCount == 0)
			{
				continue;
			}

			FResolvedSurfaceLayerMesh& Mesh = OutMeshes.AddDefaulted_GetRef();
			Mesh.LayerIndex = LayerIndex;
			FLinearColor FillColor = Layer.DebugColor;
			FillColor.A = FMath::Clamp(FillColor.A, 0.12f, 0.5f);
			Mesh.Color = FillColor.ToFColor(true);
			Mesh.LocalVertices.Reserve(CellCount * 4);
			Mesh.Indices.Reserve(CellCount * 6);

			for (const FResolvedSurfaceCell& Cell : Visualization.Cells)
			{
				if (Cell.LayerIndex != LayerIndex)
				{
					continue;
				}

				FVector Corners[4];
				if (!GetSurfaceCellCorners(Cell, Visualization.CellSize, Corners))
				{
					continue;
				}

				const int32 FirstVertex = Mesh.LocalVertices.Num();
				Mesh.LocalVertices.Append(Corners, UE_ARRAY_COUNT(Corners));
				Mesh.Indices.Add(FirstVertex);
				Mesh.Indices.Add(FirstVertex + 1);
				Mesh.Indices.Add(FirstVertex + 2);
				Mesh.Indices.Add(FirstVertex);
				Mesh.Indices.Add(FirstVertex + 2);
				Mesh.Indices.Add(FirstVertex + 3);
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
