// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Editors/ComposableCameraGraphNodeBase.h"
#include "ComposableCameraBeginPlayStartGraphNode.generated.h"

/**
 * Sentinel graph node marking the start of the BeginPlay compute chain.
 *
 * Parallel to UComposableCameraStartGraphNode, which anchors the per-frame
 * camera-node execution chain. This sentinel exists because type-asset cameras
 * can now author a separate one-shot compute chain that runs exactly once on
 * AActor::BeginPlay - see UComposableCameraComputeNodeBase and
 * AComposableCameraCameraBase::BeginPlayCamera for the runtime model. By giving
 * the compute chain its own sentinel, the editor can:
 *
 * - walk exec wires starting from this node to build
 * OwningTypeAsset->ComputeExecutionOrder, completely independent of the
 * main Start sentinel's walk (which builds FullExecChain / ExecutionOrder);
 *
 * - refuse cross-chain exec wires at schema level - a wire rooted at this
 * sentinel can only reach compute nodes, and a wire rooted at the main
 * Start sentinel can only reach camera nodes;
 *
 * - preserve the compute chain sentinel's canvas position across save /
 * reopen independently of the main Start sentinel, using
 * OwningTypeAsset->BeginPlayStartNodePosition.
 *
 * Like Start / Output, this sentinel is not user-creatable, not deletable, and
 * not duplicatable. The schema's CreateDefaultNodesForGraph is the only site
 * that spawns it, and exactly one instance ever exists on a graph.
 *
 * The PN_ExecOut name constant and the CreateExecOutPin helper are inherited
 * from UComposableCameraGraphNodeBase, so this node's AllocateDefaultPins
 * mirrors the main Start sentinel's implementation byte-for-byte - the only
 * thing that distinguishes it at runtime is its class identity, which the
 * sync phase uses to discriminate which chain a walk belongs to.
 */
UCLASS()
class COMPOSABLECAMERASYSTEMEDITOR_API UComposableCameraBeginPlayStartGraphNode: public UComposableCameraGraphNodeBase
{
	GENERATED_BODY()

public:
	// UEdGraphNode Interface 

	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FText GetTooltipText() const override;
	virtual bool CanUserDeleteNode() const override { return false; }
	virtual bool CanDuplicateNode() const override { return false; }
};
