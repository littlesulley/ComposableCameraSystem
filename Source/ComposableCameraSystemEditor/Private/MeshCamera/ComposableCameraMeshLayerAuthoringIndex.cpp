// Copyright 2026 Sulley. All Rights Reserved.

#include "MeshCamera/ComposableCameraMeshLayerAuthoringIndex.h"
#include "MeshCamera/ComposableCameraMeshSurfaceTypes.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace UE::ComposableCamera::MeshEditor
{
	namespace
	{
		bool Enabled(const FGuid& Id, TConstArrayView<FComposableCameraMeshLayerDefinition> Layers)
		{
			for (int32 Index = Layers.Num() - 1; Index >= 0; --Index)
			{
				if (Layers[Index].LayerId == Id) { return Layers[Index].bEnabled; }
			}
			return false;
		}
	}

	void FMeshLayerAuthoringIndex::Reset()
	{
		Blocks.Reset(); Tree.Reset(); TreeLeaves = 0; ChangedBlocks.Reset(); IndexedTriangles = IndexedVertices = INDEX_NONE; LastRefreshedTriangleCount = 0;
	}
	uint64 FMeshLayerAuthoringIndex::GetAllocatedSize() const
	{
		uint64 Bytes = Blocks.GetAllocatedSize() + Tree.GetAllocatedSize() + ChangedBlocks.GetAllocatedSize();
		for (const auto& Block : Blocks) { Bytes += Block.Layers.GetAllocatedSize(); }
		for (const auto& Node : Tree) { Bytes += Node.Layers.GetAllocatedSize(); }
		return Bytes;
	}
	void FMeshLayerAuthoringIndex::MergeTreeNode(int32 Node)
	{
		auto& Layers = Tree[Node].Layers; Layers.Reset();
		for (int32 Child = Node * 2; Child <= Node * 2 + 1; ++Child)
		{
			for (const auto& Source : Tree[Child].Layers)
			{
				auto* Target = Layers.FindByPredicate([&](const auto& Item) { return Item.LayerId == Source.LayerId; });
				if (!Target) { Target = &Layers.AddDefaulted_GetRef(); Target->LayerId = Source.LayerId; }
				if (Source.Bounds.IsValid) { Target->Bounds += Source.Bounds; }
				if (Source.ProjectedBounds.bIsValid) { Target->ProjectedBounds += Source.ProjectedBounds; }
			}
		}
	}
	void FMeshLayerAuthoringIndex::RebuildTree()
	{
		TreeLeaves = 1; while (TreeLeaves < Blocks.Num()) { TreeLeaves *= 2; }
		Tree.SetNum(TreeLeaves * 2);
		for (int32 Block = 0; Block < TreeLeaves; ++Block)
		{ Tree[TreeLeaves + Block].Layers = Blocks.IsValidIndex(Block) ? Blocks[Block].Layers : TArray<FLayerBounds, TInlineAllocator<2>>(); }
		for (int32 Node = TreeLeaves - 1; Node > 0; --Node) { MergeTreeNode(Node); }
	}
	void FMeshLayerAuthoringIndex::UpdateTreeLeaf(int32 Block)
	{
		if (Block < 0 || Block >= TreeLeaves) { return; }
		Tree[TreeLeaves + Block].Layers = Blocks.IsValidIndex(Block) ? Blocks[Block].Layers : TArray<FLayerBounds, TInlineAllocator<2>>();
		for (int32 Node = (TreeLeaves + Block) / 2; Node > 0; Node /= 2) { MergeTreeNode(Node); }
	}
	void FMeshLayerAuthoringIndex::CaptureCounts(const FComposableCameraMeshSurfaceAuthoringData& Data)
	{
		IndexedTriangles = Data.TriangleLayerIds.Num(); IndexedVertices = Data.Vertices.Num();
	}
	bool FMeshLayerAuthoringIndex::IsCurrent(const FComposableCameraMeshSurfaceAuthoringData& Data) const
	{
		return IndexedTriangles == Data.TriangleLayerIds.Num() && IndexedVertices == Data.Vertices.Num()
			&& ChangedBlocks.IsEmpty() && Data.IsConsistent();
	}
	void FMeshLayerAuthoringIndex::RefreshBlock(const FComposableCameraMeshSurfaceAuthoringData& Data, int32 Block)
	{
		auto& Bounds = Blocks[Block].Layers; Bounds.Reset();
		const int32 End = FMath::Min((Block + 1) * TrianglesPerBlock, Data.TriangleLayerIds.Num());
		for (int32 Triangle = Block * TrianglesPerBlock; Triangle < End; ++Triangle)
		{
			++LastRefreshedTriangleCount;
			const int32 Offset = Triangle * 3;
			if (!Data.Vertices.IsValidIndex(Data.Indices[Offset]) || !Data.Vertices.IsValidIndex(Data.Indices[Offset + 1])
				|| !Data.Vertices.IsValidIndex(Data.Indices[Offset + 2])) { continue; }
			const FVector A(Data.Vertices[Data.Indices[Offset]]), B(Data.Vertices[Data.Indices[Offset + 1]]), C(Data.Vertices[Data.Indices[Offset + 2]]);
			auto* Layer = Bounds.FindByPredicate([&](const auto& Item) { return Item.LayerId == Data.TriangleLayerIds[Triangle]; });
			if (!Layer) { Layer = &Bounds.AddDefaulted_GetRef(); Layer->LayerId = Data.TriangleLayerIds[Triangle]; }
			Layer->Bounds += A; Layer->Bounds += B; Layer->Bounds += C;
			// Identical eligibility to the resolver's bounds pass, including both windings.
			if (FMath::Abs((B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X)) * 0.5 > UE_DOUBLE_SMALL_NUMBER)
			{
				Layer->ProjectedBounds += FVector2D(A.X, A.Y); Layer->ProjectedBounds += FVector2D(B.X, B.Y);
				Layer->ProjectedBounds += FVector2D(C.X, C.Y);
			}
		}
	}
	void FMeshLayerAuthoringIndex::Build(const FComposableCameraMeshSurfaceAuthoringData& Data)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CCS_MeshLayers_AuthoringIndexBuild);
		Reset();
		if (!Data.IsConsistent()) { return; }
		Blocks.SetNum(FMath::DivideAndRoundUp(Data.TriangleLayerIds.Num(), TrianglesPerBlock));
		for (int32 Block = 0; Block < Blocks.Num(); ++Block) { RefreshBlock(Data, Block); }
		RebuildTree();
		CaptureCounts(Data);
	}
	void FMeshLayerAuthoringIndex::Append(const FComposableCameraMeshSurfaceAuthoringData& Data, int32 FirstTriangle)
	{
		if (IndexedTriangles != FirstTriangle || !ChangedBlocks.IsEmpty() || !Data.IsConsistent()) { Build(Data); return; }
		LastRefreshedTriangleCount = 0;
		Blocks.SetNum(FMath::DivideAndRoundUp(Data.TriangleLayerIds.Num(), TrianglesPerBlock));
		for (int32 Block = FirstTriangle / TrianglesPerBlock; Block < Blocks.Num(); ++Block) { RefreshBlock(Data, Block); }
		if (TreeLeaves < Blocks.Num()) { RebuildTree(); }
		else { for (int32 Block = FirstTriangle / TrianglesPerBlock; Block < Blocks.Num(); ++Block) { UpdateTreeLeaf(Block); } }
		CaptureCounts(Data);
	}
	void FMeshLayerAuthoringIndex::MarkChanged(int32 FirstTriangle, int32 EndTriangle)
	{
		if (FirstTriangle < 0 || EndTriangle <= FirstTriangle) { return; }
		for (int32 Block = FirstTriangle / TrianglesPerBlock; Block <= (EndTriangle - 1) / TrianglesPerBlock; ++Block) { ChangedBlocks.Add(Block); }
	}
	void FMeshLayerAuthoringIndex::RefreshChanged(const FComposableCameraMeshSurfaceAuthoringData& Data)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CCS_MeshLayers_AuthoringIndexRefresh);
		if (!Data.IsConsistent()) { Reset(); return; }
		LastRefreshedTriangleCount = 0;
		const int32 OldBlocks = Blocks.Num();
		Blocks.SetNum(FMath::DivideAndRoundUp(Data.TriangleLayerIds.Num(), TrianglesPerBlock));
		for (int32 Block : ChangedBlocks) { if (Blocks.IsValidIndex(Block)) { RefreshBlock(Data, Block); } }
		if (TreeLeaves < Blocks.Num()) { RebuildTree(); }
		else
		{
			for (int32 Block : ChangedBlocks) { UpdateTreeLeaf(Block); }
			for (int32 Block = Blocks.Num(); Block < OldBlocks; ++Block) { UpdateTreeLeaf(Block); }
		}
		ChangedBlocks.Reset(); CaptureCounts(Data);
	}
	FBox2D FMeshLayerAuthoringIndex::GetProjectedBounds(TConstArrayView<FComposableCameraMeshLayerDefinition> Layers) const
	{
		FBox2D Result(ForceInit);
		if (Tree.Num() < 2) { return Result; }
		for (const auto& Layer : Tree[1].Layers)
		{
			if (Layer.ProjectedBounds.bIsValid && Enabled(Layer.LayerId, Layers)) { Result += Layer.ProjectedBounds; }
		}
		return Result;
	}
	void FMeshLayerAuthoringIndex::FindVisualizationCandidates(const FBox2D& Bounds,
		TConstArrayView<FComposableCameraMeshLayerDefinition> Layers, TArray<int32>& OutTriangles) const
	{
		OutTriangles.Reset();
		if (!Bounds.bIsValid) { return; }
		if (Tree.Num() < 2) { return; }
		TArray<int32, TInlineAllocator<64>> Stack; Stack.Add(1);
		while (!Stack.IsEmpty())
		{
			const int32 Node = Stack.Pop(EAllowShrinking::No);
			if (!Tree[Node].Layers.ContainsByPredicate([&](const auto& Layer)
			{
				return Layer.ProjectedBounds.bIsValid && Layer.ProjectedBounds.Intersect(Bounds) && Enabled(Layer.LayerId, Layers);
			})) { continue; }
			if (Node < TreeLeaves) { Stack.Add(Node * 2 + 1); Stack.Add(Node * 2); continue; }
			const int32 Block = Node - TreeLeaves;
			// Ascending source order preserves the resolver's same-Layer tie behavior.
			for (int32 Triangle = Block * TrianglesPerBlock; Triangle < FMath::Min((Block + 1) * TrianglesPerBlock, IndexedTriangles); ++Triangle)
			{
				OutTriangles.Add(Triangle);
			}
		}
	}
	void FMeshLayerAuthoringIndex::FindEraseCandidates(const FComposableCameraMeshEraseStamp& Stamp,
		const FGuid& LayerId, TArray<int32>& OutTriangles) const
	{
		OutTriangles.Reset();
		if (Tree.Num() < 2) { return; }
		TArray<int32, TInlineAllocator<64>> Stack; Stack.Add(1);
		while (!Stack.IsEmpty())
		{
			const int32 Node = Stack.Pop(EAllowShrinking::No);
			const auto* Layer = Tree[Node].Layers.FindByPredicate([&](const auto& Item) { return Item.LayerId == LayerId; });
			if (!Layer || !Layer->Bounds.IsValid) { continue; }
			const FVector Center = Layer->Bounds.GetCenter() - Stamp.Center, Extent = Layer->Bounds.GetExtent();
			bool bOutside = false;
			for (int32 Axis = 0; Axis < 3 && !bOutside; ++Axis)
			{
				const FVector Direction = Axis == 0 ? Stamp.AxisX : Axis == 1 ? Stamp.AxisY : Stamp.AxisZ;
				const double Limit = Axis == 2 ? Stamp.Depth : Stamp.Radius;
				bOutside = FMath::Abs(FVector::DotProduct(Center, Direction)) > Limit + FVector::DotProduct(Extent, Direction.GetAbs()) + UE_DOUBLE_KINDA_SMALL_NUMBER;
			}
			if (bOutside) { continue; }
			if (Node < TreeLeaves) { Stack.Add(Node * 2); Stack.Add(Node * 2 + 1); continue; }
			const int32 Block = Node - TreeLeaves;
			// Descending original IDs retain the existing swap-remove/append algorithm.
			for (int32 Triangle = FMath::Min((Block + 1) * TrianglesPerBlock, IndexedTriangles) - 1; Triangle >= Block * TrianglesPerBlock; --Triangle)
			{
				OutTriangles.Add(Triangle);
			}
		}
	}
}
