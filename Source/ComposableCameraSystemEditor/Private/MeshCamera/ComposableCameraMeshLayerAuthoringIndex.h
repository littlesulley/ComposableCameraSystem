// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FComposableCameraMeshSurfaceAuthoringData;
struct FComposableCameraMeshLayerDefinition;
struct FComposableCameraMeshEraseStamp;

namespace UE::ComposableCamera::MeshEditor
{
	/** Disposable Edit-only broad phase. Structural rewrites must Reset; append/erase refresh changed blocks. */
	class FMeshLayerAuthoringIndex
	{
	public:
		static constexpr int32 TrianglesPerBlock = 128;
		void Reset();
		void Build(const FComposableCameraMeshSurfaceAuthoringData& Data);
		bool IsCurrent(const FComposableCameraMeshSurfaceAuthoringData& Data) const;
		void Append(const FComposableCameraMeshSurfaceAuthoringData& Data, int32 FirstTriangle);
		void MarkChanged(int32 FirstTriangle, int32 EndTriangle);
		void RefreshChanged(const FComposableCameraMeshSurfaceAuthoringData& Data);
		FBox2D GetProjectedBounds(TConstArrayView<FComposableCameraMeshLayerDefinition> Layers) const;
		void FindVisualizationCandidates(const FBox2D& Bounds, TConstArrayView<FComposableCameraMeshLayerDefinition> Layers,
			TArray<int32>& OutTriangles) const;
		void FindEraseCandidates(const FComposableCameraMeshEraseStamp& Stamp, const FGuid& LayerId,
			TArray<int32>& OutTriangles) const;
		int32 GetLastRefreshedTriangleCount() const { return LastRefreshedTriangleCount; }
		uint64 GetAllocatedSize() const;
	private:
		struct FLayerBounds
		{
			FGuid LayerId;
			FBox Bounds = FBox(ForceInit);
			FBox2D ProjectedBounds = FBox2D(ForceInit);
		};
		struct FBlock { TArray<FLayerBounds, TInlineAllocator<2>> Layers; };
		void RefreshBlock(const FComposableCameraMeshSurfaceAuthoringData& Data, int32 Block);
		void CaptureCounts(const FComposableCameraMeshSurfaceAuthoringData& Data);
		void RebuildTree();
		void UpdateTreeLeaf(int32 Block);
		void MergeTreeNode(int32 Node);
		TArray<FBlock> Blocks;
		/** Persistent bounding hierarchy; leaves retain original block IDs/source order. */
		TArray<FBlock> Tree;
		int32 TreeLeaves = 0;
		TSet<int32> ChangedBlocks;
		int32 IndexedTriangles = INDEX_NONE;
		int32 IndexedVertices = INDEX_NONE;
		int32 LastRefreshedTriangleCount = 0;
	};
}
