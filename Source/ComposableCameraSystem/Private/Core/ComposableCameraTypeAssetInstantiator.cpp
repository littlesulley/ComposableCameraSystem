// Copyright 2026 Sulley. All Rights Reserved.

#include "Core/ComposableCameraTypeAssetInstantiator.h"

#include "Cameras/ComposableCameraCameraBase.h"
#include "ComposableCameraSystemModule.h"
#include "Core/ComposableCameraRuntimeDataBlock.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "Nodes/ComposableCameraComputeNodeBase.h"
#include "Transitions/ComposableCameraTransitionBase.h"

namespace UE::ComposableCameras
{
	void ConstructCameraFromTypeAsset(
		AComposableCameraCameraBase* Camera,
		UComposableCameraTypeAsset* TypeAsset,
		const FComposableCameraParameterBlock& ParameterBlock)
	{
		if (!Camera || !TypeAsset)
		{
			return;
		}

		// Propagate camera-identity fields from the type asset onto the spawned
		// instance so modifiers and context-stack resume logic see the right values.
		Camera->CameraTag = TypeAsset->CameraTag;
		Camera->bDefaultPreserveCameraPose = TypeAsset->bDefaultPreserveCameraPose;

		// Stamp the source type asset and parameter block onto the camera so that
		// ReactivateCurrentCamera (triggered by modifier changes) can fully
		// reconstruct the camera from the same source instead of producing an
		// empty shell. See PCM::ReactivateCurrentCamera for the consumer.
		Camera->SourceTypeAsset = TypeAsset;
		Camera->SourceParameterBlock = ParameterBlock;

		// Name the spawned camera actor after the source type asset so it is
		// identifiable in the World Outliner and debug logs. Use
		// MakeUniqueObjectName so a resume-from-pop path (which spawns a new
		// camera while the old one with the same name is still alive during
		// the transition) does not assert on a name collision.
		{
			const FString AssetName = TypeAsset->GetName();
			const FName DesiredName(*FString::Printf(TEXT("Camera_%s"), *AssetName));
			const FName UniqueName = MakeUniqueObjectName(Camera->GetOuter(), Camera->GetClass(), DesiredName);
			Camera->Rename(*UniqueName.ToString());
#if WITH_EDITOR
			Camera->SetActorLabel(FString::Printf(TEXT("Camera_%s"), *AssetName));
#endif
		}

		// Clear any default nodes the camera class may have.
		Camera->CameraNodes.Empty();
		Camera->ComputeNodes.Empty();

		// Build a set of camera-node indices that are actually referenced by the
		// execution chain. Nodes not in this set are orphaned in the graph (no
		// exec-pin connection) and are skipped during duplication to save memory
		// and initialization cost. We still push nullptr at their index so that
		// CameraNodes[i] keeps its 1:1 correspondence with NodeTemplates[i];
		// the RuntimeDataBlock pin-key offsets and FullExecChain indices depend
		// on that mapping.
		TSet<int32> ConnectedNodeIndices;
		if (TypeAsset->FullExecChain.Num() > 0)
		{
			for (const FComposableCameraExecEntry& Entry : TypeAsset->FullExecChain)
			{
				if (Entry.CameraNodeIndex != INDEX_NONE)
				{
					ConnectedNodeIndices.Add(Entry.CameraNodeIndex);
				}
			}
		}
		else if (TypeAsset->ExecutionOrder.Num() > 0)
		{
			// Legacy fallback: FullExecChain is empty, use the flat ExecutionOrder.
			for (const int32 Idx : TypeAsset->ExecutionOrder)
			{
				ConnectedNodeIndices.Add(Idx);
			}
		}
		// If both are empty (should not happen for type-asset cameras), we
		// duplicate everything. ConnectedNodeIndices stays empty and the
		// bHasExecChain flag below gates the skip logic off.
		const bool bHasExecChain = ConnectedNodeIndices.Num() > 0;

		// Duplicate node templates from the type asset.
		// Reserve the full size up front to maintain index correspondence.
		Camera->CameraNodes.SetNum(TypeAsset->NodeTemplates.Num());
		for (int32 i = 0; i < TypeAsset->NodeTemplates.Num(); ++i)
		{
			UComposableCameraCameraNodeBase* Template = TypeAsset->NodeTemplates[i];
			if (!Template)
			{
				Camera->CameraNodes[i] = nullptr;
				continue;
			}

			// Skip orphaned nodes: present in NodeTemplates but not referenced by
			// the execution chain. The nullptr preserves index correspondence.
			if (bHasExecChain && !ConnectedNodeIndices.Contains(i))
			{
				Camera->CameraNodes[i] = nullptr;
				continue;
			}

			UComposableCameraCameraNodeBase* NodeInstance = DuplicateObject<UComposableCameraCameraNodeBase>(Template, Camera);
			if (!NodeInstance)
			{
				UE_LOG(LogComposableCameraSystem, Warning,
					TEXT("ConstructCameraFromTypeAsset: Failed to duplicate node template at index %d (%s)."),
					i, *Template->GetClass()->GetName());
				Camera->CameraNodes[i] = nullptr;
				continue;
			}
			Camera->CameraNodes[i] = NodeInstance;
		}

		// Duplicate compute node templates from the type asset.
		//
		// Compute nodes live in their own index space from the data block's point
		// of view. The runtime NodeIndex used for SetRuntimeDataBlock is
		// (NodeTemplates.Num() + ComputeIdx), matching the layout allocated by
		// BuildRuntimeDataLayout. That keeps output pin keys (and per-instance
		// default override keys) unique across chains without the pin key struct
		// needing to discriminate.
		//
		// Same skip-unconnected logic as camera nodes: build a set of referenced
		// compute-node indices from ComputeFullExecChain (or ComputeExecutionOrder
		// as legacy fallback), and push nullptr for orphaned indices.
		TSet<int32> ConnectedComputeNodeIndices;
		if (TypeAsset->ComputeFullExecChain.Num() > 0)
		{
			for (const FComposableCameraExecEntry& Entry : TypeAsset->ComputeFullExecChain)
			{
				if (Entry.CameraNodeIndex != INDEX_NONE)
				{
					ConnectedComputeNodeIndices.Add(Entry.CameraNodeIndex);
				}
			}
		}
		else if (TypeAsset->ComputeExecutionOrder.Num() > 0)
		{
			for (const int32 Idx : TypeAsset->ComputeExecutionOrder)
			{
				ConnectedComputeNodeIndices.Add(Idx);
			}
		}
		const bool bHasComputeExecChain = ConnectedComputeNodeIndices.Num() > 0;

		Camera->ComputeNodes.SetNum(TypeAsset->ComputeNodeTemplates.Num());
		for (int32 i = 0; i < TypeAsset->ComputeNodeTemplates.Num(); ++i)
		{
			UComposableCameraComputeNodeBase* Template = TypeAsset->ComputeNodeTemplates[i];
			if (!Template)
			{
				Camera->ComputeNodes[i] = nullptr;
				continue;
			}

			if (bHasComputeExecChain && !ConnectedComputeNodeIndices.Contains(i))
			{
				Camera->ComputeNodes[i] = nullptr;
				continue;
			}

			UComposableCameraComputeNodeBase* NodeInstance =
				DuplicateObject<UComposableCameraComputeNodeBase>(Template, Camera);
			if (!NodeInstance)
			{
				UE_LOG(LogComposableCameraSystem, Warning,
					TEXT("ConstructCameraFromTypeAsset: Failed to duplicate compute node template at index %d (%s)."),
					i, *Template->GetClass()->GetName());
				Camera->ComputeNodes[i] = nullptr;
				continue;
			}
			Camera->ComputeNodes[i] = NodeInstance;
		}

		// Build the RuntimeDataBlock layout, owned by the camera.
		Camera->OwnedRuntimeDataBlock = MakeUnique<FComposableCameraRuntimeDataBlock>();
		*Camera->OwnedRuntimeDataBlock = TypeAsset->BuildRuntimeDataLayout();

		// Apply caller-provided parameter values.
		TypeAsset->ApplyParameterBlock(*Camera->OwnedRuntimeDataBlock, ParameterBlock);

		// Apply delegate bindings from the parameter block. Delegates are not POD
		// and cannot live in the data block's byte array. They are stored in a
		// parallel map on the parameter block and written directly into the target
		// node's FDelegateProperty UPROPERTY via reflection.
		TypeAsset->ApplyDelegateBindings(Camera, ParameterBlock);

		// Wire the data block to each node instance.
		// Nodes hold raw pointers. They never outlive the camera that owns the block.
		FComposableCameraRuntimeDataBlock* DataBlock = Camera->OwnedRuntimeDataBlock.Get();
		for (int32 i = 0; i < Camera->CameraNodes.Num(); ++i)
		{
			if (Camera->CameraNodes[i])
			{
				Camera->CameraNodes[i]->SetRuntimeDataBlock(DataBlock, i);
			}
		}

		// Wire compute nodes to the same data block, using the offset index space
		// (NodeTemplates.Num() + ComputeIdx). The base index must come from the
		// TypeAsset's author-order array count rather than Camera->CameraNodes.Num()
		// in case the camera-node duplication loop skipped a null template. The
		// data block layout uses TypeAsset->NodeTemplates.Num() as its base, so we
		// match that exactly here.
		const int32 ComputeNodeIndexBase = TypeAsset->NodeTemplates.Num();
		Camera->TypeAssetNodeTemplateCount = ComputeNodeIndexBase;
		for (int32 i = 0; i < Camera->ComputeNodes.Num(); ++i)
		{
			if (Camera->ComputeNodes[i])
			{
				Camera->ComputeNodes[i]->SetRuntimeDataBlock(DataBlock, ComputeNodeIndexBase + i);
			}
		}

		// Copy exec chains from the type asset to the camera for runtime dispatch.
		// FullExecChain and ComputeFullExecChain contain the interleaved
		// CameraNode + SetVariable sequences; TickCamera and BeginPlayCamera walk
		// these at runtime instead of the flat node arrays when they are non-empty.
		Camera->FullExecChain = TypeAsset->FullExecChain;
		Camera->ComputeFullExecChain = TypeAsset->ComputeFullExecChain;

		// Reorder ComputeNodes by ComputeExecutionOrder ONLY when the new
		// ComputeFullExecChain is empty (legacy assets saved before the field
		// existed). When ComputeFullExecChain is present, the exec chain itself
		// drives execution order and its CameraNodeIndex entries reference
		// ComputeNodeTemplates author-order indices. Reordering the array
		// would break that correspondence.
		if (Camera->ComputeFullExecChain.Num() == 0
			&& TypeAsset->ComputeExecutionOrder.Num() > 0
			&& Camera->ComputeNodes.Num() > 0)
		{
			TArray<TObjectPtr<UComposableCameraComputeNodeBase>> Reordered;
			Reordered.Reserve(Camera->ComputeNodes.Num());
			for (const int32 AuthorIdx : TypeAsset->ComputeExecutionOrder)
			{
				if (Camera->ComputeNodes.IsValidIndex(AuthorIdx))
				{
					Reordered.Add(Camera->ComputeNodes[AuthorIdx]);
				}
			}
			// Preserve any compute nodes that weren't referenced by the execution
			// order (e.g. orphaned / disconnected in the graph) by appending them
			// at the end.
			for (UComposableCameraComputeNodeBase* Node : Camera->ComputeNodes)
			{
				if (Node && !Reordered.Contains(Node))
				{
					Reordered.Add(Node);
				}
			}
			Camera->ComputeNodes = MoveTemp(Reordered);
		}

		// Run per-node Initialize now that CameraNodes is populated and every node is
		// wired to the RuntimeDataBlock. Director::ActivateNewCamera already called
		// Camera->Initialize earlier in the flow, but at that point CameraNodes was
		// empty (the base class spawned via SpawnActorDeferred has no default nodes),
		// so the per-node init loop was a no-op. Without this second call, every
		// type-asset node silently skipped Node::Initialize. Meaning OwningCamera /
		// OwningPlayerCameraManager, the OnInitialize BP event, OnPreTick/OnPostTick
		// delegate wiring all never ran for type-asset cameras. Exposed parameters
		// still flowed through the data block and hid the bug in most cases.
		Camera->InitializeNodes();

		// Copy the type asset's enter transition onto the camera (for resume/pop transitions).
		if (TypeAsset->EnterTransition && !Camera->EnterTransition)
		{
			Camera->EnterTransition = DuplicateObject<UComposableCameraTransitionBase>(
				TypeAsset->EnterTransition, Camera);
		}
	}
}
