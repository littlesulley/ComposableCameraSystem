// Copyright 2026 Sulley. All Rights Reserved.
#include "MeshCamera/ComposableCameraMeshLayerSavedPreview.h"

namespace UE::ComposableCamera::MeshEditor
{
	FComposableCameraMeshSurfaceEditorPreview SaveEditorPreview(const FResolvedSurfaceVisualization& Visualization,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers)
	{
		FComposableCameraMeshSurfaceEditorPreview Saved;
		Saved.Version = 1; Saved.CellSize = Visualization.CellSize; Saved.Bounds = Visualization.LocalBounds;
		Saved.Cells.Reserve(Visualization.Cells.Num());
		for (const auto& Cell : Visualization.Cells)
		{
			auto& Target = Saved.Cells.AddDefaulted_GetRef();
			Target.Position = Cell.LocalPosition; Target.Normal = Cell.LocalNormal; Target.bFullCoverage = Cell.bFullCoverage;
			if (Layers.IsValidIndex(Cell.LayerIndex)) { Target.LayerId = Layers[Cell.LayerIndex].LayerId; }
			Target.Patches.Reserve(Cell.Patches.Num());
			for (const auto& Patch : Cell.Patches)
			{
				if (!Layers.IsValidIndex(Patch.LayerIndex)) { continue; }
				auto& Copy = Target.Patches.AddDefaulted_GetRef();
				Copy.LayerId = Layers[Patch.LayerIndex].LayerId; Copy.Normal = Patch.LocalNormal;
				Copy.Vertices.Append(Patch.LocalVertices.GetData(), Patch.LocalVertices.Num());
			}
		}
		return Saved;
	}

	bool MatchesEditorPreview(const FComposableCameraMeshSurfaceEditorPreview& Saved,
		const FResolvedSurfaceVisualization& Visualization, TConstArrayView<FComposableCameraMeshLayerDefinition> Layers)
	{
		if (Saved.Version != 1 || Saved.CellSize != Visualization.CellSize || Saved.Cells.Num() != Visualization.Cells.Num()
			|| Saved.Bounds.bIsValid != Visualization.LocalBounds.bIsValid) { return false; }
		if (Saved.Bounds.bIsValid && (Saved.Bounds.Min != Visualization.LocalBounds.Min || Saved.Bounds.Max != Visualization.LocalBounds.Max)) { return false; }
		for (int32 Index = 0; Index < Saved.Cells.Num(); ++Index)
		{
			const auto& Cell = Visualization.Cells[Index]; const auto& Stored = Saved.Cells[Index];
			const FGuid Summary = Layers.IsValidIndex(Cell.LayerIndex) ? Layers[Cell.LayerIndex].LayerId : FGuid();
			if (Stored.Position != Cell.LocalPosition || Stored.Normal != Cell.LocalNormal || Stored.LayerId != Summary
				|| Stored.bFullCoverage != Cell.bFullCoverage || Stored.Patches.Num() != Cell.Patches.Num()) { return false; }
			for (int32 Patch = 0; Patch < Cell.Patches.Num(); ++Patch)
			{
				const auto& Current = Cell.Patches[Patch]; const auto& Previous = Stored.Patches[Patch];
				if (!Layers.IsValidIndex(Current.LayerIndex) || Previous.LayerId != Layers[Current.LayerIndex].LayerId
					|| Previous.Normal != Current.LocalNormal || Previous.Vertices.Num() != Current.LocalVertices.Num()) { return false; }
				for (int32 Vertex = 0; Vertex < Previous.Vertices.Num(); ++Vertex)
				{ if (Previous.Vertices[Vertex] != Current.LocalVertices[Vertex]) { return false; } }
			}
		}
		return true;
	}

	bool LoadEditorPreview(const FComposableCameraMeshSurfaceEditorPreview& Saved,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers, FResolvedSurfaceVisualization& Out)
	{
		if (Saved.Version != 1 || !FMath::IsFinite(Saved.CellSize) || Saved.CellSize <= 0.0) { return false; }
		if ((!Saved.Cells.IsEmpty() && !Saved.Bounds.bIsValid)
			|| (Saved.Bounds.bIsValid && (Saved.Bounds.Min.ContainsNaN() || Saved.Bounds.Max.ContainsNaN()
				|| Saved.Bounds.Min.X > Saved.Bounds.Max.X || Saved.Bounds.Min.Y > Saved.Bounds.Max.Y))) { return false; }
		FResolvedSurfaceVisualization Loaded; Loaded.CellSize = Saved.CellSize; Loaded.LocalBounds = Saved.Bounds;
		TMap<FGuid, int32> LayerIndices;
		for (int32 Index = 0; Index < Layers.Num(); ++Index) { LayerIndices.Add(Layers[Index].LayerId, Index); }
		Loaded.Cells.Reserve(Saved.Cells.Num()); Loaded.CellsByGrid.Reserve(Saved.Cells.Num());
		for (const auto& Cell : Saved.Cells)
		{
			if (Cell.Position.ContainsNaN() || Cell.Normal.ContainsNaN()) { return false; }
			const double GridX = Cell.Position.X / Saved.CellSize, GridY = Cell.Position.Y / Saved.CellSize;
			if (!FMath::IsFinite(GridX) || !FMath::IsFinite(GridY)
				|| GridX <= MIN_int32 || GridX >= MAX_int32 || GridY <= MIN_int32 || GridY >= MAX_int32) { return false; }
			auto& Target = Loaded.Cells.AddDefaulted_GetRef();
			Target.LocalPosition = Cell.Position; Target.LocalNormal = Cell.Normal; Target.bFullCoverage = Cell.bFullCoverage;
			const int32* Summary = LayerIndices.Find(Cell.LayerId); Target.LayerIndex = Summary ? *Summary : INDEX_NONE;
			for (const auto& Patch : Cell.Patches)
			{
				const int32* Layer = LayerIndices.Find(Patch.LayerId);
				if (!Layer || !Layers[*Layer].bEnabled || Patch.Vertices.Num() < 3 || Patch.Normal.ContainsNaN()) { return false; }
				for (const FVector& Point : Patch.Vertices) { if (Point.ContainsNaN()) { return false; } }
				auto& Copy = Target.Patches.AddDefaulted_GetRef(); Copy.LayerIndex = *Layer; Copy.LocalNormal = Patch.Normal;
				Copy.LocalVertices.Append(Patch.Vertices.GetData(), Patch.Vertices.Num());
			}
			const FIntPoint Grid(FMath::FloorToInt(GridX), FMath::FloorToInt(GridY));
			Loaded.CellsByGrid.FindOrAdd(Grid).Add(Loaded.Cells.Num() - 1);
		}
		Out = MoveTemp(Loaded); return true;
	}
}
