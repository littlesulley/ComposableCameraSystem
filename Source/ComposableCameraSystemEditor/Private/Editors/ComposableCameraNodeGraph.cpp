// Copyright Sulley. All rights reserved.

#include "Editors/ComposableCameraNodeGraph.h"
#include "Editors/ComposableCameraNodeGraphNode.h"
#include "Editors/ComposableCameraNodeGraphSchema.h"
#include "Editors/ComposableCameraStartGraphNode.h"
#include "Editors/ComposableCameraBeginPlayStartGraphNode.h"
#include "Editors/ComposableCameraOutputGraphNode.h"
#include "Editors/ComposableCameraVariableGraphNode.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "Nodes/ComposableCameraComputeNodeBase.h"
#include "Nodes/ComposableCameraNodePinTypes.h"
#include "EdGraphSchema_K2.h"

#define LOCTEXT_NAMESPACE "ComposableCameraNodeGraph"

// =============================================================================
//  Pin context-menu signalling
// =============================================================================
//
// The Mark/Consume pair encapsulates a single-shot signal from the schema's
// GetContextMenuActions to the toolkit's deferred selection handler. See the
// field comment on bPinContextMenuRequested for the ordering problem this
// works around. Keeping both sides behind methods means neither the schema
// nor the toolkit touches the field directly — the field's entire lifecycle
// lives in these two lines.

void UComposableCameraNodeGraph::MarkPinContextMenuRequested()
{
	bPinContextMenuRequested = true;
}

bool UComposableCameraNodeGraph::ConsumePinContextMenuRequested()
{
	const bool bWasRequested = bPinContextMenuRequested;
	bPinContextMenuRequested = false;
	return bWasRequested;
}

// =============================================================================
//  Orchestrators
// =============================================================================

void UComposableCameraNodeGraph::RebuildFromTypeAsset()
{
	if (!OwningTypeAsset)
	{
		return;
	}

	// Guard against re-entrant SyncToTypeAsset. Step 1 below calls RemoveNode,
	// which fires NotifyGraphChanged → the toolkit's OnGraphChanged →
	// SyncToTypeAsset. Without this guard, SyncToTypeAsset would walk the
	// partially-drained Nodes array and write an empty NodeTemplates back to
	// OwningTypeAsset, clobbering the durable data we're trying to rebuild
	// from. The symmetric guard in SyncToTypeAsset handles the reverse case.
	if (bIsRebuildingFromTypeAsset)
	{
		return;
	}
	TGuardValue<bool> RebuildGuard(bIsRebuildingFromTypeAsset, true);

	Modify();

	// Phase 1: drain the existing graph.
	RebuildPhase_RemoveAllGraphNodes();

	// Phase 2: re-create the Start sentinel.
	UComposableCameraStartGraphNode* StartNode = RebuildPhase_CreateStartNode();

	// Phase 2b: re-create the BeginPlay Start sentinel. Spawned unconditionally
	// so the palette can always offer "drop the first compute node" from a
	// visible root, even on graphs that currently have zero compute nodes.
	UComposableCameraBeginPlayStartGraphNode* BeginPlayStartNode =
		RebuildPhase_CreateBeginPlayStartNode();

	// Phase 3: re-create one camera graph node per NodeTemplate entry. The
	// returned array is parallel to NodeTemplates by index, so phases 5–8 can
	// resolve connection endpoints by raw index lookup.
	TArray<UComposableCameraNodeGraphNode*> CreatedGraphNodes;
	RebuildPhase_CreateCameraGraphNodes(CreatedGraphNodes);

	// Phase 3b: re-create one compute graph node per ComputeNodeTemplate entry.
	// Parallel to ComputeNodeTemplates by index within the compute space.
	TArray<UComposableCameraNodeGraphNode*> CreatedComputeGraphNodes;
	RebuildPhase_CreateComputeGraphNodes(CreatedComputeGraphNodes);

	// Phase 4: re-create the Output sentinel at the end of the graph.
	UComposableCameraOutputGraphNode* OutputNode = RebuildPhase_CreateOutputNode();

	// Phase 5: replay camera ↔ camera data wires.
	RebuildPhase_RestoreCameraNodePinConnections(CreatedGraphNodes);

	// Phase 5b: replay compute ↔ compute data wires.
	RebuildPhase_RestoreComputeNodePinConnections(CreatedComputeGraphNodes);

	// Phase 6: re-create variable graph nodes and wire them to the appropriate
	// chain's node pins. The lookup is consumed by phases 7/7b to wire
	// Set-variable exec entries by GUID.
	TMap<FGuid, UComposableCameraVariableGraphNode*> VariableGuidToGraphNode;
	RebuildPhase_RestoreVariableGraphNodes(CreatedGraphNodes, CreatedComputeGraphNodes, VariableGuidToGraphNode);

	// Phase 7: replay the camera execution chain (FullExecChain preferred,
	// ExecutionOrder as legacy fallback).
	RebuildPhase_RestoreExecutionChain(StartNode, OutputNode, CreatedGraphNodes, VariableGuidToGraphNode);

	// Phase 7b: replay the compute execution chain. Runs independently of
	// the camera chain — the two chains never share nodes or wires by
	// schema construction, so neither phase needs to know about the other.
	// Needs the VariableGuidToGraphNode lookup for Set-variable exec entries.
	RebuildPhase_RestoreComputeExecutionChain(BeginPlayStartNode, CreatedComputeGraphNodes, VariableGuidToGraphNode);
}

void UComposableCameraNodeGraph::SyncToTypeAsset()
{
	if (!OwningTypeAsset)
	{
		return;
	}

	// Do not let a sync run while we're rebuilding — the graph is in a
	// transient, partially-drained state. See the comment in
	// RebuildFromTypeAsset for why this matters.
	if (bIsRebuildingFromTypeAsset)
	{
		return;
	}
	if (bIsSyncingToTypeAsset)
	{
		return;
	}
	TGuardValue<bool> SyncGuard(bIsSyncingToTypeAsset, true);

	OwningTypeAsset->Modify();

	// Phase 1: walk Nodes, classify into camera / compute / variable / sentinel
	// buckets, sort each graph-node bucket by its currently-assigned NodeIndex.
	// Camera and compute graph nodes share the UComposableCameraNodeGraphNode
	// class and are distinguished purely by their NodeTemplate's class identity.
	TArray<UComposableCameraNodeGraphNode*> GraphNodes;
	TArray<UComposableCameraNodeGraphNode*> ComputeGraphNodes;
	TArray<UComposableCameraVariableGraphNode*> VariableGraphNodes;
	UComposableCameraStartGraphNode* StartNode = nullptr;
	UComposableCameraBeginPlayStartGraphNode* BeginPlayStartNode = nullptr;
	UComposableCameraOutputGraphNode* OutputNode = nullptr;
	SyncPhase_CollectGraphNodes(GraphNodes, ComputeGraphNodes, VariableGraphNodes,
		StartNode, BeginPlayStartNode, OutputNode);

	// Phase 2: snapshot the old NodeTemplates index → pointer mapping for
	// exposed-parameter migration in phase 8. Compute nodes have no analog —
	// the compute chain does not participate in ExposedParameters in v1.
	TMap<int32, UComposableCameraCameraNodeBase*> OldIndexToTemplate;
	SyncPhase_SnapshotOldTemplateIndices(OldIndexToTemplate);

	// Phase 3: rebuild NodeTemplates / NodeTemplatePositions / sentinel positions
	// from the graph and assign each GraphNode its new NodeIndex. Populates the
	// template-pointer → new-index map for phase 8.
	TMap<const UComposableCameraCameraNodeBase*, int32> TemplateToNewIndex;
	SyncPhase_RebuildNodeTemplatesAndPositions(GraphNodes, StartNode, OutputNode, TemplateToNewIndex);

	// Phase 3b: rebuild ComputeNodeTemplates / ComputeNodeTemplatePositions
	// and the BeginPlay sentinel position. Runs independently of the camera
	// phases — the two index spaces do not overlap and compute graph nodes'
	// NodeIndex fields are written exclusively by this phase.
	SyncPhase_RebuildComputeTemplatesAndPositions(ComputeGraphNodes, BeginPlayStartNode);

	// Phase 4: rebuild PinConnections from camera ↔ camera data wires.
	SyncPhase_RebuildPinConnections(GraphNodes);

	// Phase 4b: rebuild ComputePinConnections from compute ↔ compute data wires.
	SyncPhase_RebuildComputePinConnections(ComputeGraphNodes);

	// Phase 5: rebuild ExecutionOrder + FullExecChain by walking from Start.
	SyncPhase_RebuildExecutionChain(StartNode);

	// Phase 6b: rebuild VariableNodes records from each variable graph node.
	SyncPhase_RebuildVariableNodeRecords(VariableGraphNodes);

	// Phase 6c: rebuild ComputeExecutionOrder + ComputeFullExecChain by
	// walking from BeginPlayStart. Mirrors phase 6's camera chain walk but
	// for the compute chain, including variable Set-node interleaving.
	SyncPhase_RebuildComputeExecutionChain(BeginPlayStartNode);

	// Phase 7: migrate ExposedParameters target indices into the new index
	// space using template-pointer identity.
	SyncPhase_MigrateExposedParameters(OldIndexToTemplate, TemplateToNewIndex);

	// Phase 8: rebuild the parallel NodePinOverrides array from each
	// GraphNode's cached RuntimePinOverrides. Must run after phase 3 has
	// reassigned NodeIndex values so the parallel-array ordering lines up
	// with the freshly rebuilt NodeTemplates.
	SyncPhase_RebuildNodePinOverrides(GraphNodes);

	// Phase 8b: rebuild the parallel ComputeNodePinOverrides array. Must run
	// after phase 3b so the per-compute-node NodeIndex lines up with the
	// freshly rebuilt ComputeNodeTemplates.
	SyncPhase_RebuildComputeNodePinOverrides(ComputeGraphNodes);

	OwningTypeAsset->MarkPackageDirty();
}

// =============================================================================
//  SyncToTypeAsset phases
// =============================================================================

void UComposableCameraNodeGraph::SyncPhase_CollectGraphNodes(
	TArray<UComposableCameraNodeGraphNode*>& OutCameraGraphNodes,
	TArray<UComposableCameraNodeGraphNode*>& OutComputeGraphNodes,
	TArray<UComposableCameraVariableGraphNode*>& OutVariableGraphNodes,
	UComposableCameraStartGraphNode*& OutStartNode,
	UComposableCameraBeginPlayStartGraphNode*& OutBeginPlayStartNode,
	UComposableCameraOutputGraphNode*& OutOutputNode) const
{
	// The graph is the source of truth. We rebuild NodeTemplates, indices,
	// PinConnections, OutputConnection, ExecutionOrder, VariableNodes, and
	// the parallel Compute* arrays from it. Exposed parameters are migrated
	// by NodeTemplate identity (so deleting a node drops the parameter,
	// renaming an index preserves it).
	//
	// Camera and compute graph nodes share UComposableCameraNodeGraphNode —
	// they're classified here by inspecting the template's C++ class:
	// UComposableCameraComputeNodeBase-derived templates route to the compute
	// bucket, everything else routes to the camera bucket. Graph nodes with a
	// null NodeTemplate are skipped (they'd be silently dropped by downstream
	// phases anyway, but skipping here keeps the buckets free of empties).

	OutStartNode = nullptr;
	OutBeginPlayStartNode = nullptr;
	OutOutputNode = nullptr;

	for (UEdGraphNode* Node : Nodes)
	{
		if (UComposableCameraNodeGraphNode* GraphNode = Cast<UComposableCameraNodeGraphNode>(Node))
		{
			if (!GraphNode->NodeTemplate)
			{
				continue;
			}
			if (GraphNode->NodeTemplate->IsA<UComposableCameraComputeNodeBase>())
			{
				OutComputeGraphNodes.Add(GraphNode);
			}
			else
			{
				OutCameraGraphNodes.Add(GraphNode);
			}
		}
		else if (UComposableCameraVariableGraphNode* VarNode = Cast<UComposableCameraVariableGraphNode>(Node))
		{
			OutVariableGraphNodes.Add(VarNode);
		}
		else if (UComposableCameraOutputGraphNode* OutNode = Cast<UComposableCameraOutputGraphNode>(Node))
		{
			OutOutputNode = OutNode;
		}
		else if (UComposableCameraBeginPlayStartGraphNode* BpStNode = Cast<UComposableCameraBeginPlayStartGraphNode>(Node))
		{
			// BeginPlayStart must be checked before Start — it inherits from the
			// same base (UComposableCameraGraphNodeBase), but
			// UComposableCameraStartGraphNode is a distinct class in its own
			// right, so the Cast<UComposableCameraStartGraphNode> branch below
			// won't mis-classify it. Keeping the BeginPlay branch first anyway
			// so the ordering is obvious at the read site.
			OutBeginPlayStartNode = BpStNode;
		}
		else if (UComposableCameraStartGraphNode* StNode = Cast<UComposableCameraStartGraphNode>(Node))
		{
			OutStartNode = StNode;
		}
	}

	// Preserve the previous order where possible by sorting by the currently
	// assigned NodeIndex. Nodes created since the last sync have NodeIndex
	// pointing at the end of the array, so they append naturally. The sort
	// is applied to the camera and compute buckets independently — their
	// NodeIndex fields live in disjoint index spaces, so a single combined
	// sort wouldn't be meaningful.
	auto NodeIndexLess = [](const UComposableCameraNodeGraphNode& A, const UComposableCameraNodeGraphNode& B)
	{
		// INDEX_NONE sorts last so freshly-created unassigned nodes end up at the end.
		const int32 IndexA = (A.NodeIndex == INDEX_NONE) ? MAX_int32 : A.NodeIndex;
		const int32 IndexB = (B.NodeIndex == INDEX_NONE) ? MAX_int32 : B.NodeIndex;
		return IndexA < IndexB;
	};
	OutCameraGraphNodes.Sort(NodeIndexLess);
	OutComputeGraphNodes.Sort(NodeIndexLess);
}

void UComposableCameraNodeGraph::SyncPhase_SnapshotOldTemplateIndices(
	TMap<int32, UComposableCameraCameraNodeBase*>& OutOldIndexToTemplate) const
{
	for (int32 i = 0; i < OwningTypeAsset->NodeTemplates.Num(); ++i)
	{
		if (UComposableCameraCameraNodeBase* OldTemplate = OwningTypeAsset->NodeTemplates[i])
		{
			OutOldIndexToTemplate.Add(i, OldTemplate);
		}
	}
}

void UComposableCameraNodeGraph::SyncPhase_RebuildNodeTemplatesAndPositions(
	const TArray<UComposableCameraNodeGraphNode*>& GraphNodes,
	const UComposableCameraStartGraphNode* StartNode,
	const UComposableCameraOutputGraphNode* OutputNode,
	TMap<const UComposableCameraCameraNodeBase*, int32>& OutTemplateToNewIndex)
{
	// Also snapshot each graph node's canvas position into the parallel
	// NodeTemplatePositions array so the layout survives save/reopen. The two
	// arrays are written together here to preserve the invariant that
	// NodeTemplatePositions.Num() == NodeTemplates.Num() after a successful sync.

	TArray<TObjectPtr<UComposableCameraCameraNodeBase>> NewTemplates;
	TArray<FVector2D> NewTemplatePositions;
	NewTemplates.Reserve(GraphNodes.Num());
	NewTemplatePositions.Reserve(GraphNodes.Num());

	for (UComposableCameraNodeGraphNode* GraphNode : GraphNodes)
	{
		const int32 NewIndex = NewTemplates.Num();
		GraphNode->NodeIndex = NewIndex;
		OutTemplateToNewIndex.Add(GraphNode->NodeTemplate, NewIndex);
		NewTemplates.Add(GraphNode->NodeTemplate);
		NewTemplatePositions.Add(FVector2D(GraphNode->NodePosX, GraphNode->NodePosY));
	}
	OwningTypeAsset->NodeTemplates = MoveTemp(NewTemplates);
	OwningTypeAsset->NodeTemplatePositions = MoveTemp(NewTemplatePositions);

	// Snapshot sentinel positions so Start and Output also survive round-trip.
	if (StartNode)
	{
		OwningTypeAsset->StartNodePosition = FVector2D(StartNode->NodePosX, StartNode->NodePosY);
	}
	if (OutputNode)
	{
		OwningTypeAsset->OutputNodePosition = FVector2D(OutputNode->NodePosX, OutputNode->NodePosY);
	}
}

void UComposableCameraNodeGraph::SyncPhase_RebuildComputeTemplatesAndPositions(
	const TArray<UComposableCameraNodeGraphNode*>& ComputeGraphNodes,
	const UComposableCameraBeginPlayStartGraphNode* BeginPlayStartNode)
{
	// Mirror of SyncPhase_RebuildNodeTemplatesAndPositions for the compute
	// chain. Writes ComputeNodeTemplates / ComputeNodePinOverrides-parallel
	// ComputeNodeTemplatePositions and reassigns each compute graph node's
	// NodeIndex to its position in ComputeNodeTemplates. Compute nodes do
	// not participate in ExposedParameters (see orchestrator comment), so
	// we do not populate a template→index map here.
	//
	// The cast to UComposableCameraComputeNodeBase is safe because phase 1
	// classified the bucket: every entry in ComputeGraphNodes has a
	// non-null NodeTemplate that is a UComposableCameraComputeNodeBase
	// subclass. A checkf would be louder if that invariant breaks.

	TArray<TObjectPtr<UComposableCameraComputeNodeBase>> NewComputeTemplates;
	TArray<FVector2D> NewComputePositions;
	NewComputeTemplates.Reserve(ComputeGraphNodes.Num());
	NewComputePositions.Reserve(ComputeGraphNodes.Num());

	for (UComposableCameraNodeGraphNode* ComputeGraphNode : ComputeGraphNodes)
	{
		UComposableCameraComputeNodeBase* ComputeTemplate =
			Cast<UComposableCameraComputeNodeBase>(ComputeGraphNode->NodeTemplate);
		checkf(ComputeTemplate,
			TEXT("Compute graph node bucket contained a graph node whose NodeTemplate is not a compute node — classification bug in SyncPhase_CollectGraphNodes"));

		const int32 NewIndex = NewComputeTemplates.Num();
		ComputeGraphNode->NodeIndex = NewIndex;
		NewComputeTemplates.Add(ComputeTemplate);
		NewComputePositions.Add(FVector2D(ComputeGraphNode->NodePosX, ComputeGraphNode->NodePosY));
	}
	OwningTypeAsset->ComputeNodeTemplates = MoveTemp(NewComputeTemplates);
	OwningTypeAsset->ComputeNodeTemplatePositions = MoveTemp(NewComputePositions);

	if (BeginPlayStartNode)
	{
		OwningTypeAsset->BeginPlayStartNodePosition =
			FVector2D(BeginPlayStartNode->NodePosX, BeginPlayStartNode->NodePosY);
	}
}

void UComposableCameraNodeGraph::SyncPhase_RebuildPinConnections(
	const TArray<UComposableCameraNodeGraphNode*>& GraphNodes)
{
	// Wires whose source OR target is a variable graph node are captured in
	// the VariableNodes records instead — see SyncPhase_RebuildVariableNodeRecords.

	TArray<FComposableCameraPinConnection> NewConnections;

	for (UComposableCameraNodeGraphNode* TargetGraphNode : GraphNodes)
	{
		for (UEdGraphPin* InputPin : TargetGraphNode->Pins)
		{
			if (InputPin->Direction != EGPD_Input || InputPin->LinkedTo.Num() == 0)
			{
				continue;
			}
			if (InputPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			{
				continue;
			}

			UEdGraphPin* LinkedOutputPin = InputPin->LinkedTo[0];
			if (!LinkedOutputPin)
			{
				continue;
			}

			const UComposableCameraNodeGraphNode* SourceGraphNode =
				Cast<UComposableCameraNodeGraphNode>(LinkedOutputPin->GetOwningNode());
			if (!SourceGraphNode)
			{
				// Source is a variable node (or something else) — handled in the variable phase.
				continue;
			}
			// Defensive cross-chain guard: a compute source paired with a
			// camera target is schema-disallowed, but a malformed asset could
			// still carry such a wire. Skip silently rather than writing a
			// compute-space NodeIndex into PinConnections (which indexes
			// NodeTemplates, not ComputeNodeTemplates).
			if (SourceGraphNode->NodeTemplate &&
				SourceGraphNode->NodeTemplate->IsA<UComposableCameraComputeNodeBase>())
			{
				continue;
			}

			FComposableCameraPinConnection Conn;
			Conn.SourceNodeIndex = SourceGraphNode->NodeIndex;
			Conn.SourcePinName = LinkedOutputPin->PinName;
			Conn.TargetNodeIndex = TargetGraphNode->NodeIndex;
			Conn.TargetPinName = InputPin->PinName;
			NewConnections.Add(Conn);
		}
	}

	OwningTypeAsset->PinConnections = MoveTemp(NewConnections);
}

void UComposableCameraNodeGraph::SyncPhase_RebuildComputePinConnections(
	const TArray<UComposableCameraNodeGraphNode*>& ComputeGraphNodes)
{
	// Mirror of SyncPhase_RebuildPinConnections for the compute chain.
	// Cross-chain data wires are schema-disallowed, so we only need to look
	// at links whose source is also a compute graph node — anything else
	// indicates corrupted state we'd rather drop than preserve. Variable
	// nodes cannot legally wire into compute pins in v1, so any variable
	// node on the other end is treated as stale and skipped.

	TArray<FComposableCameraPinConnection> NewComputeConnections;

	for (UComposableCameraNodeGraphNode* TargetComputeNode : ComputeGraphNodes)
	{
		for (UEdGraphPin* InputPin : TargetComputeNode->Pins)
		{
			if (InputPin->Direction != EGPD_Input || InputPin->LinkedTo.Num() == 0)
			{
				continue;
			}
			if (InputPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			{
				continue;
			}

			UEdGraphPin* LinkedOutputPin = InputPin->LinkedTo[0];
			if (!LinkedOutputPin)
			{
				continue;
			}

			UComposableCameraNodeGraphNode* SourceGraphNode =
				Cast<UComposableCameraNodeGraphNode>(LinkedOutputPin->GetOwningNode());
			if (!SourceGraphNode || !SourceGraphNode->NodeTemplate)
			{
				continue;
			}
			// Source must also be a compute node — cross-chain wires are
			// impossible in a schema-conformant graph, but an extra check here
			// prevents an accidentally-mismatched source from silently being
			// written into ComputePinConnections with a camera-space NodeIndex.
			if (!SourceGraphNode->NodeTemplate->IsA<UComposableCameraComputeNodeBase>())
			{
				continue;
			}

			FComposableCameraPinConnection Conn;
			Conn.SourceNodeIndex = SourceGraphNode->NodeIndex;
			Conn.SourcePinName = LinkedOutputPin->PinName;
			Conn.TargetNodeIndex = TargetComputeNode->NodeIndex;
			Conn.TargetPinName = InputPin->PinName;
			NewComputeConnections.Add(Conn);
		}
	}

	OwningTypeAsset->ComputePinConnections = MoveTemp(NewComputeConnections);
}

void UComposableCameraNodeGraph::SyncPhase_RebuildExecutionChain(
	const UComposableCameraStartGraphNode* StartNode)
{
	// The exec chain can traverse two kinds of nodes:
	//   - Camera nodes (UComposableCameraNodeGraphNode) → produce a CameraNode
	//     entry in FullExecChain and also append to ExecutionOrder.
	//   - Set variable nodes (UComposableCameraVariableGraphNode with bIsSetter)
	//     → produce a SetVariable entry in FullExecChain only. The entry
	//     captures the variable GUID plus the (CameraNodeIndex, SourcePinName)
	//     that feeds the Set node's Value input.
	//
	// Get variable nodes and unknown node kinds break the walk since they don't
	// participate in exec flow.

	OwningTypeAsset->ExecutionOrder.Empty();
	OwningTypeAsset->FullExecChain.Empty();

	// Helper: resolve a variable GUID to its name and slot size from the
	// owning type asset's variable arrays (InternalVariables ∪ ExposedVariables).
	auto ResolveVariable = [this](const FGuid& Guid, FName& OutName, int32& OutSlotSize)
	{
		auto SearchArray = [&](const TArray<FComposableCameraInternalVariable>& Array) -> bool
		{
			for (const FComposableCameraInternalVariable& Var : Array)
			{
				if (Var.VariableGuid == Guid)
				{
					OutName = Var.VariableName;
					OutSlotSize = GetPinTypeSize(Var.VariableType, Var.StructType);
					return true;
				}
			}
			return false;
		};
		if (!SearchArray(OwningTypeAsset->InternalVariables))
		{
			SearchArray(OwningTypeAsset->ExposedVariables);
		}
	};

	// Start the walk from whatever is wired to Start.ExecOut.
	UEdGraphNode* NextExecNode = nullptr;
	if (StartNode)
	{
		UEdGraphPin* StartExecOut = StartNode->FindPin(
			UComposableCameraStartGraphNode::PN_ExecOut, EGPD_Output);
		if (StartExecOut && StartExecOut->LinkedTo.Num() > 0)
		{
			NextExecNode = StartExecOut->LinkedTo[0]->GetOwningNode();
		}
	}

	// Guard against malformed graphs (cycles, duplicates) by tracking visited
	// EdGraphNode pointers. Camera-node cycles were previously prevented via a
	// NodeIndex set; variable nodes don't have a NodeIndex, so we use the
	// pointer identity instead.
	TSet<UEdGraphNode*> VisitedExecNodes;

	while (NextExecNode && !VisitedExecNodes.Contains(NextExecNode))
	{
		VisitedExecNodes.Add(NextExecNode);

		UEdGraphPin* ExecOutPin = nullptr;

		if (UComposableCameraNodeGraphNode* CameraExecNode = Cast<UComposableCameraNodeGraphNode>(NextExecNode))
		{
			// Defensive cross-chain guard: a compute node reachable through
			// the main Start chain is a schema violation. Terminate the walk
			// rather than appending a compute-space NodeIndex into the
			// camera-space ExecutionOrder array.
			if (CameraExecNode->NodeTemplate &&
				CameraExecNode->NodeTemplate->IsA<UComposableCameraComputeNodeBase>())
			{
				break;
			}

			// Camera node step: record both projections.
			OwningTypeAsset->ExecutionOrder.Add(CameraExecNode->NodeIndex);

			FComposableCameraExecEntry Entry;
			Entry.EntryType = EComposableCameraExecEntryType::CameraNode;
			Entry.CameraNodeIndex = CameraExecNode->NodeIndex;
			OwningTypeAsset->FullExecChain.Add(Entry);

			ExecOutPin = CameraExecNode->FindPin(
				UComposableCameraNodeGraphNode::PN_ExecOut, EGPD_Output);
		}
		else if (UComposableCameraVariableGraphNode* VarExecNode = Cast<UComposableCameraVariableGraphNode>(NextExecNode))
		{
			// Only Set nodes carry exec pins — a Get node being wired into an
			// exec chain is a malformed graph, bail out.
			if (!VarExecNode->bIsSetter)
			{
				break;
			}

			// Resolve the source (camera-node output pin) feeding this Set's
			// Value input. A Set with no Value wire contributes nothing useful
			// to the chain, so skip it but keep walking through its ExecOut.
			FComposableCameraExecEntry Entry;
			Entry.EntryType = EComposableCameraExecEntryType::SetVariable;
			Entry.VariableGuid = VarExecNode->VariableGuid;
			Entry.CameraNodeIndex = INDEX_NONE;

			// Resolve variable name + slot size for runtime dispatch.
			ResolveVariable(Entry.VariableGuid, Entry.VariableName, Entry.VariableSlotSize);

			if (UEdGraphPin* ValuePin = VarExecNode->FindPin(
					UComposableCameraVariableGraphNode::PN_Value, EGPD_Input))
			{
				if (ValuePin->LinkedTo.Num() > 0 && ValuePin->LinkedTo[0])
				{
					UEdGraphPin* SourcePin = ValuePin->LinkedTo[0];
					if (const UComposableCameraNodeGraphNode* SourceCameraNode =
						Cast<UComposableCameraNodeGraphNode>(SourcePin->GetOwningNode()))
					{
						Entry.CameraNodeIndex = SourceCameraNode->NodeIndex;
						Entry.SourcePinName = SourcePin->PinName;
					}
				}
			}

			// Record the entry even if the Value input is unwired — the editor
			// validator will flag it, but round-tripping should preserve the
			// user's partially-authored graph so the warning doesn't silently
			// disappear on save/load.
			OwningTypeAsset->FullExecChain.Add(Entry);

			ExecOutPin = VarExecNode->FindPin(
				UComposableCameraVariableGraphNode::PN_ExecOut, EGPD_Output);
		}
		else
		{
			// Output sentinel or unknown — terminate the walk.
			break;
		}

		if (!ExecOutPin || ExecOutPin->LinkedTo.Num() == 0 || !ExecOutPin->LinkedTo[0])
		{
			break;
		}
		NextExecNode = ExecOutPin->LinkedTo[0]->GetOwningNode();
	}
}

void UComposableCameraNodeGraph::SyncPhase_RebuildComputeExecutionChain(
	const UComposableCameraBeginPlayStartGraphNode* BeginPlayStartNode)
{
	// Walk from BeginPlayStartNode->ExecOut, building both the flat
	// ComputeExecutionOrder (compute-node indices only) and the full
	// ComputeFullExecChain (compute nodes interleaved with Set-variable
	// entries). Mirrors SyncPhase_RebuildExecutionChain for the camera chain.
	//
	// For CameraNode entries, CameraNodeIndex indexes ComputeNodeTemplates
	// (NOT NodeTemplates). For SetVariable entries, CameraNodeIndex is the
	// index of the source compute node in ComputeNodeTemplates whose output
	// pin feeds the Set node's Value input. The runtime applies the
	// offset (NodeTemplates.Num() + ComputeIdx) when looking up output pin
	// slots in the RuntimeDataBlock.
	//
	// There is no terminating sentinel on the compute chain — the walk
	// stops at the first unwired ExecOut.

	OwningTypeAsset->ComputeExecutionOrder.Empty();
	OwningTypeAsset->ComputeFullExecChain.Empty();

	if (!BeginPlayStartNode)
	{
		return;
	}

	// Helper: resolve a variable GUID to its name and slot size from the
	// owning type asset's variable arrays (InternalVariables ∪ ExposedVariables).
	auto ResolveVariable = [this](const FGuid& Guid, FName& OutName, int32& OutSlotSize)
	{
		auto SearchArray = [&](const TArray<FComposableCameraInternalVariable>& Array) -> bool
		{
			for (const FComposableCameraInternalVariable& Var : Array)
			{
				if (Var.VariableGuid == Guid)
				{
					OutName = Var.VariableName;
					OutSlotSize = GetPinTypeSize(Var.VariableType, Var.StructType);
					return true;
				}
			}
			return false;
		};
		if (!SearchArray(OwningTypeAsset->InternalVariables))
		{
			SearchArray(OwningTypeAsset->ExposedVariables);
		}
	};

	UEdGraphNode* NextExecNode = nullptr;
	if (UEdGraphPin* BpExecOut = BeginPlayStartNode->FindPin(
			UComposableCameraBeginPlayStartGraphNode::PN_ExecOut, EGPD_Output))
	{
		if (BpExecOut->LinkedTo.Num() > 0 && BpExecOut->LinkedTo[0])
		{
			NextExecNode = BpExecOut->LinkedTo[0]->GetOwningNode();
		}
	}

	// Cycle guard mirrors the camera exec-chain walk.
	TSet<UEdGraphNode*> VisitedExecNodes;

	while (NextExecNode && !VisitedExecNodes.Contains(NextExecNode))
	{
		VisitedExecNodes.Add(NextExecNode);

		UEdGraphPin* ExecOutPin = nullptr;

		if (UComposableCameraNodeGraphNode* ComputeExecNode =
			Cast<UComposableCameraNodeGraphNode>(NextExecNode))
		{
			if (!ComputeExecNode->NodeTemplate)
			{
				break;
			}
			// A camera graph node reachable through the BeginPlay chain is a
			// schema violation — terminate the walk rather than writing a
			// camera-space NodeIndex into a compute-space array.
			if (!ComputeExecNode->NodeTemplate->IsA<UComposableCameraComputeNodeBase>())
			{
				break;
			}

			// Compute node step: record both projections.
			OwningTypeAsset->ComputeExecutionOrder.Add(ComputeExecNode->NodeIndex);

			FComposableCameraExecEntry Entry;
			Entry.EntryType = EComposableCameraExecEntryType::CameraNode;
			Entry.CameraNodeIndex = ComputeExecNode->NodeIndex;
			OwningTypeAsset->ComputeFullExecChain.Add(Entry);

			ExecOutPin = ComputeExecNode->FindPin(
				UComposableCameraNodeGraphNode::PN_ExecOut, EGPD_Output);
		}
		else if (UComposableCameraVariableGraphNode* VarExecNode =
			Cast<UComposableCameraVariableGraphNode>(NextExecNode))
		{
			// Only Set nodes carry exec pins.
			if (!VarExecNode->bIsSetter)
			{
				break;
			}

			// Resolve the source (compute-node output pin) feeding this Set's
			// Value input.
			FComposableCameraExecEntry Entry;
			Entry.EntryType = EComposableCameraExecEntryType::SetVariable;
			Entry.VariableGuid = VarExecNode->VariableGuid;
			Entry.CameraNodeIndex = INDEX_NONE;

			// Resolve variable name + slot size for runtime dispatch.
			ResolveVariable(Entry.VariableGuid, Entry.VariableName, Entry.VariableSlotSize);

			if (UEdGraphPin* ValuePin = VarExecNode->FindPin(
					UComposableCameraVariableGraphNode::PN_Value, EGPD_Input))
			{
				if (ValuePin->LinkedTo.Num() > 0 && ValuePin->LinkedTo[0])
				{
					UEdGraphPin* SourcePin = ValuePin->LinkedTo[0];
					if (const UComposableCameraNodeGraphNode* SourceComputeNode =
						Cast<UComposableCameraNodeGraphNode>(SourcePin->GetOwningNode()))
					{
						Entry.CameraNodeIndex = SourceComputeNode->NodeIndex;
						Entry.SourcePinName = SourcePin->PinName;
					}
				}
			}

			OwningTypeAsset->ComputeFullExecChain.Add(Entry);

			ExecOutPin = VarExecNode->FindPin(
				UComposableCameraVariableGraphNode::PN_ExecOut, EGPD_Output);
		}
		else
		{
			// Unknown node type — terminate the walk.
			break;
		}

		if (!ExecOutPin || ExecOutPin->LinkedTo.Num() == 0 || !ExecOutPin->LinkedTo[0])
		{
			break;
		}
		NextExecNode = ExecOutPin->LinkedTo[0]->GetOwningNode();
	}
}

void UComposableCameraNodeGraph::SyncPhase_RebuildVariableNodeRecords(
	const TArray<UComposableCameraVariableGraphNode*>& VariableGraphNodes)
{
	// Each variable graph node becomes one record. A Get node's connections
	// are the node input pins its Value output feeds into. A Set node's
	// connections are the node output pins that feed into its Value input.
	// Wires to non-camera/compute nodes (e.g. Output sentinel) are ignored.
	//
	// The record's bIsComputeChain flag is determined by the schema's
	// ClassifyChainForNode — for Set nodes this follows the exec wires
	// backward; for Get nodes it defaults to camera chain (Get nodes are
	// chain-agnostic for data wires, so the flag only affects Set nodes'
	// index-space interpretation). The connection's CameraNodeIndex indexes
	// into the chain-appropriate template array.

	TArray<FComposableCameraVariableNodeRecord> NewVariableRecords;
	NewVariableRecords.Reserve(VariableGraphNodes.Num());

	for (UComposableCameraVariableGraphNode* VarNode : VariableGraphNodes)
	{
		if (!VarNode)
		{
			continue;
		}

		FComposableCameraVariableNodeRecord Record;
		Record.NodeGuid = VarNode->NodeGuid;
		Record.VariableGuid = VarNode->VariableGuid;
		Record.VariableName = VarNode->VariableName;
		Record.bIsSetter = VarNode->bIsSetter;
		Record.Position = FVector2D(VarNode->NodePosX, VarNode->NodePosY);

		// Classify which chain this variable node belongs to.
		const EComposableCameraGraphChain Chain =
			UComposableCameraNodeGraphSchema::ClassifyChainForNode(VarNode);
		Record.bIsComputeChain = (Chain == EComposableCameraGraphChain::Compute);

		UEdGraphPin* ValuePin = VarNode->FindPin(UComposableCameraVariableGraphNode::PN_Value);
		if (ValuePin)
		{
			for (UEdGraphPin* LinkedPin : ValuePin->LinkedTo)
			{
				if (!LinkedPin)
				{
					continue;
				}

				const UComposableCameraNodeGraphNode* EndpointGraphNode =
					Cast<UComposableCameraNodeGraphNode>(LinkedPin->GetOwningNode());
				if (!EndpointGraphNode)
				{
					continue;
				}

				FComposableCameraVariablePinConnection VarConn;
				VarConn.CameraNodeIndex = EndpointGraphNode->NodeIndex;
				VarConn.CameraPinName = LinkedPin->PinName;
				Record.Connections.Add(VarConn);
			}
		}

		NewVariableRecords.Add(Record);
	}

	OwningTypeAsset->VariableNodes = MoveTemp(NewVariableRecords);
}

void UComposableCameraNodeGraph::SyncPhase_MigrateExposedParameters(
	const TMap<int32, UComposableCameraCameraNodeBase*>& OldIndexToTemplate,
	const TMap<const UComposableCameraCameraNodeBase*, int32>& TemplateToNewIndex)
{
	TArray<FComposableCameraExposedParameter> MigratedExposed;
	MigratedExposed.Reserve(OwningTypeAsset->ExposedParameters.Num());

	for (FComposableCameraExposedParameter& Param : OwningTypeAsset->ExposedParameters)
	{
		UComposableCameraCameraNodeBase* const* OldTemplatePtr =
			OldIndexToTemplate.Find(Param.TargetNodeIndex);
		if (!OldTemplatePtr || !*OldTemplatePtr)
		{
			// Old index was invalid (shouldn't happen) — drop the parameter.
			continue;
		}

		const int32* NewIndexPtr = TemplateToNewIndex.Find(*OldTemplatePtr);
		if (!NewIndexPtr)
		{
			// The node this parameter targeted has been deleted — drop it.
			continue;
		}

		Param.TargetNodeIndex = *NewIndexPtr;
		MigratedExposed.Add(Param);
	}

	OwningTypeAsset->ExposedParameters = MoveTemp(MigratedExposed);
}

void UComposableCameraNodeGraph::SyncPhase_RebuildNodePinOverrides(
	const TArray<UComposableCameraNodeGraphNode*>& GraphNodes)
{
	// The overrides live on the graph nodes themselves (RuntimePinOverrides is
	// Transient, so it only persists as long as the editor session does). On
	// sync we snapshot them into a parallel array on the type asset so that
	// the next RebuildFromTypeAsset can hydrate a fresh set of graph nodes
	// with the same per-pin state.
	//
	// Because the input GraphNodes array is iterated in the exact same order
	// as SyncPhase_RebuildNodeTemplatesAndPositions used to write NodeTemplates,
	// NodePinOverrides[i] lines up with NodeTemplates[i] by construction. No
	// identity lookup is needed.

	TArray<FComposableCameraNodeTemplatePinOverrides> NewOverrides;
	NewOverrides.Reserve(GraphNodes.Num());

	for (UComposableCameraNodeGraphNode* GraphNode : GraphNodes)
	{
		FComposableCameraNodeTemplatePinOverrides Entry;
		Entry.Overrides = GraphNode->RuntimePinOverrides;
		NewOverrides.Add(MoveTemp(Entry));
	}

	OwningTypeAsset->NodePinOverrides = MoveTemp(NewOverrides);
}

void UComposableCameraNodeGraph::SyncPhase_RebuildComputeNodePinOverrides(
	const TArray<UComposableCameraNodeGraphNode*>& ComputeGraphNodes)
{
	// Mirror of SyncPhase_RebuildNodePinOverrides for the compute chain.
	// Because ComputeGraphNodes is iterated in the exact same order as
	// SyncPhase_RebuildComputeTemplatesAndPositions wrote ComputeNodeTemplates,
	// ComputeNodePinOverrides[i] lines up with ComputeNodeTemplates[i] by
	// construction — no identity lookup needed.

	TArray<FComposableCameraNodeTemplatePinOverrides> NewComputeOverrides;
	NewComputeOverrides.Reserve(ComputeGraphNodes.Num());

	for (UComposableCameraNodeGraphNode* ComputeGraphNode : ComputeGraphNodes)
	{
		FComposableCameraNodeTemplatePinOverrides Entry;
		Entry.Overrides = ComputeGraphNode->RuntimePinOverrides;
		NewComputeOverrides.Add(MoveTemp(Entry));
	}

	OwningTypeAsset->ComputeNodePinOverrides = MoveTemp(NewComputeOverrides);
}

// =============================================================================
//  RebuildFromTypeAsset phases
// =============================================================================

void UComposableCameraNodeGraph::RebuildPhase_RemoveAllGraphNodes()
{
	TArray<UEdGraphNode*> NodesToRemove = Nodes;
	for (UEdGraphNode* Node : NodesToRemove)
	{
		RemoveNode(Node);
	}
}

UComposableCameraStartGraphNode* UComposableCameraNodeGraph::RebuildPhase_CreateStartNode()
{
	// Sentinel positions are scalars on the type asset, not an array, so
	// they're always "valid" — freshly created assets get the defaults set in
	// the UPROPERTY declarations.

	UComposableCameraStartGraphNode* StartNode = NewObject<UComposableCameraStartGraphNode>(
		this, NAME_None, RF_Transactional);
	StartNode->CreateNewGuid();
	StartNode->NodePosX = static_cast<int32>(OwningTypeAsset->StartNodePosition.X);
	StartNode->NodePosY = static_cast<int32>(OwningTypeAsset->StartNodePosition.Y);
	StartNode->AllocateDefaultPins();
	AddNode(StartNode, /*bFromUI=*/false, /*bSelectNewNode=*/false);
	return StartNode;
}

UComposableCameraBeginPlayStartGraphNode* UComposableCameraNodeGraph::RebuildPhase_CreateBeginPlayStartNode()
{
	// Parallel to RebuildPhase_CreateStartNode — a single instance of the
	// BeginPlay Start sentinel is spawned on every rebuild, positioned from
	// OwningTypeAsset->BeginPlayStartNodePosition. The sentinel is present on
	// every graph regardless of whether any compute nodes exist, so authors
	// can always see the compute-chain root and the schema's default palette
	// can always offer "drop the first compute node" from a visible anchor.

	UComposableCameraBeginPlayStartGraphNode* BeginPlayStartNode =
		NewObject<UComposableCameraBeginPlayStartGraphNode>(
			this, NAME_None, RF_Transactional);
	BeginPlayStartNode->CreateNewGuid();
	BeginPlayStartNode->NodePosX = static_cast<int32>(OwningTypeAsset->BeginPlayStartNodePosition.X);
	BeginPlayStartNode->NodePosY = static_cast<int32>(OwningTypeAsset->BeginPlayStartNodePosition.Y);
	BeginPlayStartNode->AllocateDefaultPins();
	AddNode(BeginPlayStartNode, /*bFromUI=*/false, /*bSelectNewNode=*/false);
	return BeginPlayStartNode;
}

void UComposableCameraNodeGraph::RebuildPhase_CreateCameraGraphNodes(
	TArray<UComposableCameraNodeGraphNode*>& OutCreatedGraphNodes)
{
	// Each camera graph node uses its saved canvas position when available
	// (set by SyncToTypeAsset on the previous save). For legacy assets saved
	// before position persistence existed, NodeTemplatePositions is empty and
	// we fall back to a default horizontal layout. The layout is derived from:
	//   - bUseSavedLayout: true iff the saved position array length matches
	//     the template array length (so every node has a saved position).
	//   - Default layout constants (XSpacing, YCenter) only used as fallback.

	const float XSpacing = 300.0f;
	const float YCenter = 0.0f;

	const bool bUseSavedLayout =
		OwningTypeAsset->NodeTemplatePositions.Num() == OwningTypeAsset->NodeTemplates.Num();

	// Pin overrides are stored parallel to NodeTemplates. Legacy assets saved
	// before this field existed start with an empty array; in that case every
	// pin inherits its class-level defaults, which is exactly the behavior
	// of a sparse zero-override entry, so no special migration path is needed.
	const bool bUseSavedOverrides =
		OwningTypeAsset->NodePinOverrides.Num() == OwningTypeAsset->NodeTemplates.Num();

	for (int32 i = 0; i < OwningTypeAsset->NodeTemplates.Num(); ++i)
	{
		UComposableCameraCameraNodeBase* NodeTemplate = OwningTypeAsset->NodeTemplates[i];
		if (!NodeTemplate)
		{
			continue;
		}

		UComposableCameraNodeGraphNode* GraphNode = NewObject<UComposableCameraNodeGraphNode>(
			this, NAME_None, RF_Transactional);
		GraphNode->NodeTemplate = NodeTemplate;
		GraphNode->NodeIndex = i;
		GraphNode->CreateNewGuid();

		if (bUseSavedLayout)
		{
			const FVector2D& SavedPos = OwningTypeAsset->NodeTemplatePositions[i];
			GraphNode->NodePosX = static_cast<int32>(SavedPos.X);
			GraphNode->NodePosY = static_cast<int32>(SavedPos.Y);
		}
		else
		{
			// Legacy fallback: lay out nodes in a horizontal line after Start.
			// The first subsequent SyncToTypeAsset will migrate this asset forward
			// by populating NodeTemplatePositions from these computed positions.
			GraphNode->NodePosX = i * XSpacing;
			GraphNode->NodePosY = YCenter;
		}

		// Hydrate the per-instance pin override cache BEFORE AllocateDefaultPins
		// runs — the pin materialization loop consults RuntimePinOverrides to
		// decide whether to materialize a declared pin at all (bAsPin) and
		// which default value to seed it with. Doing this in the wrong order
		// would produce a graph where the correct number of pins exist but
		// all of them still show C++ class defaults.
		if (bUseSavedOverrides)
		{
			GraphNode->RuntimePinOverrides = OwningTypeAsset->NodePinOverrides[i].Overrides;
		}
		else
		{
			GraphNode->RuntimePinOverrides.Reset();
		}

		GraphNode->AllocateDefaultPins();
		AddNode(GraphNode, /*bFromUI=*/false, /*bSelectNewNode=*/false);

		OutCreatedGraphNodes.Add(GraphNode);
	}
}

void UComposableCameraNodeGraph::RebuildPhase_CreateComputeGraphNodes(
	TArray<UComposableCameraNodeGraphNode*>& OutCreatedComputeGraphNodes)
{
	// Mirror of RebuildPhase_CreateCameraGraphNodes for the compute chain.
	// Saved positions live in ComputeNodeTemplatePositions (parallel to
	// ComputeNodeTemplates); the legacy fallback places compute nodes in a
	// horizontal row *below* the main camera row so a freshly-opened legacy
	// asset doesn't stack them on top of existing camera nodes. The first
	// subsequent SyncToTypeAsset will migrate the asset forward by writing
	// those computed positions into ComputeNodeTemplatePositions.
	//
	// Per-instance RuntimePinOverrides are hydrated from
	// ComputeNodePinOverrides (parallel to ComputeNodeTemplates). The
	// hydration must happen BEFORE AllocateDefaultPins runs so the pin
	// materialization loop can see the per-pin bAsPin / default-value state
	// — this is the same ordering requirement that camera graph nodes have.

	const float XSpacing = 300.0f;
	const float YBelowCameraRow = 400.0f;

	const bool bUseSavedLayout =
		OwningTypeAsset->ComputeNodeTemplatePositions.Num() == OwningTypeAsset->ComputeNodeTemplates.Num();

	const bool bUseSavedOverrides =
		OwningTypeAsset->ComputeNodePinOverrides.Num() == OwningTypeAsset->ComputeNodeTemplates.Num();

	for (int32 i = 0; i < OwningTypeAsset->ComputeNodeTemplates.Num(); ++i)
	{
		UComposableCameraComputeNodeBase* ComputeTemplate = OwningTypeAsset->ComputeNodeTemplates[i];
		if (!ComputeTemplate)
		{
			continue;
		}

		UComposableCameraNodeGraphNode* ComputeGraphNode = NewObject<UComposableCameraNodeGraphNode>(
			this, NAME_None, RF_Transactional);
		ComputeGraphNode->NodeTemplate = ComputeTemplate;
		ComputeGraphNode->NodeIndex = i;
		ComputeGraphNode->CreateNewGuid();

		if (bUseSavedLayout)
		{
			const FVector2D& SavedPos = OwningTypeAsset->ComputeNodeTemplatePositions[i];
			ComputeGraphNode->NodePosX = static_cast<int32>(SavedPos.X);
			ComputeGraphNode->NodePosY = static_cast<int32>(SavedPos.Y);
		}
		else
		{
			// Legacy fallback: lay out compute nodes horizontally in a row
			// below the main camera row. SyncToTypeAsset will persist these
			// positions on the next save, silently migrating the asset forward.
			ComputeGraphNode->NodePosX = i * XSpacing;
			ComputeGraphNode->NodePosY = YBelowCameraRow;
		}

		if (bUseSavedOverrides)
		{
			ComputeGraphNode->RuntimePinOverrides = OwningTypeAsset->ComputeNodePinOverrides[i].Overrides;
		}
		else
		{
			ComputeGraphNode->RuntimePinOverrides.Reset();
		}

		ComputeGraphNode->AllocateDefaultPins();
		AddNode(ComputeGraphNode, /*bFromUI=*/false, /*bSelectNewNode=*/false);

		OutCreatedComputeGraphNodes.Add(ComputeGraphNode);
	}
}

UComposableCameraOutputGraphNode* UComposableCameraNodeGraph::RebuildPhase_CreateOutputNode()
{
	UComposableCameraOutputGraphNode* OutputNode = NewObject<UComposableCameraOutputGraphNode>(
		this, NAME_None, RF_Transactional);
	OutputNode->CreateNewGuid();
	OutputNode->NodePosX = static_cast<int32>(OwningTypeAsset->OutputNodePosition.X);
	OutputNode->NodePosY = static_cast<int32>(OwningTypeAsset->OutputNodePosition.Y);
	OutputNode->AllocateDefaultPins();
	AddNode(OutputNode, /*bFromUI=*/false, /*bSelectNewNode=*/false);
	return OutputNode;
}

void UComposableCameraNodeGraph::RebuildPhase_RestoreCameraNodePinConnections(
	const TArray<UComposableCameraNodeGraphNode*>& CreatedGraphNodes)
{
	for (const FComposableCameraPinConnection& Connection : OwningTypeAsset->PinConnections)
	{
		if (!CreatedGraphNodes.IsValidIndex(Connection.SourceNodeIndex) ||
			!CreatedGraphNodes.IsValidIndex(Connection.TargetNodeIndex))
		{
			continue;
		}

		UComposableCameraNodeGraphNode* SourceGraphNode = CreatedGraphNodes[Connection.SourceNodeIndex];
		UComposableCameraNodeGraphNode* TargetGraphNode = CreatedGraphNodes[Connection.TargetNodeIndex];

		UEdGraphPin* SourcePin = SourceGraphNode->FindPin(Connection.SourcePinName, EGPD_Output);
		UEdGraphPin* TargetPin = TargetGraphNode->FindPin(Connection.TargetPinName, EGPD_Input);

		if (SourcePin && TargetPin)
		{
			SourcePin->MakeLinkTo(TargetPin);
		}
	}
}

void UComposableCameraNodeGraph::RebuildPhase_RestoreComputeNodePinConnections(
	const TArray<UComposableCameraNodeGraphNode*>& CreatedComputeGraphNodes)
{
	// Mirror of RebuildPhase_RestoreCameraNodePinConnections for the compute
	// chain. The CreatedComputeGraphNodes array is parallel to
	// ComputeNodeTemplates by construction (see RebuildPhase_CreateComputeGraphNodes),
	// so the SourceNodeIndex / TargetNodeIndex fields in each
	// ComputePinConnection index directly into it.

	for (const FComposableCameraPinConnection& Connection : OwningTypeAsset->ComputePinConnections)
	{
		if (!CreatedComputeGraphNodes.IsValidIndex(Connection.SourceNodeIndex) ||
			!CreatedComputeGraphNodes.IsValidIndex(Connection.TargetNodeIndex))
		{
			continue;
		}

		UComposableCameraNodeGraphNode* SourceGraphNode = CreatedComputeGraphNodes[Connection.SourceNodeIndex];
		UComposableCameraNodeGraphNode* TargetGraphNode = CreatedComputeGraphNodes[Connection.TargetNodeIndex];

		UEdGraphPin* SourcePin = SourceGraphNode->FindPin(Connection.SourcePinName, EGPD_Output);
		UEdGraphPin* TargetPin = TargetGraphNode->FindPin(Connection.TargetPinName, EGPD_Input);

		if (SourcePin && TargetPin)
		{
			SourcePin->MakeLinkTo(TargetPin);
		}
	}
}

void UComposableCameraNodeGraph::RebuildPhase_RestoreVariableGraphNodes(
	const TArray<UComposableCameraNodeGraphNode*>& CreatedGraphNodes,
	const TArray<UComposableCameraNodeGraphNode*>& CreatedComputeGraphNodes,
	TMap<FGuid, UComposableCameraVariableGraphNode*>& OutVariableGuidToGraphNode)
{
	for (const FComposableCameraVariableNodeRecord& Record : OwningTypeAsset->VariableNodes)
	{
		UComposableCameraVariableGraphNode* VarNode = NewObject<UComposableCameraVariableGraphNode>(
			this, NAME_None, RF_Transactional);

		// Prefer the record's GUID as identity. For legacy records saved before
		// the GUID migration (Record.VariableGuid is invalid), fall back to
		// looking up the variable by name on the owning type asset so the node
		// still resolves correctly. FindVariable() will then backfill VariableGuid
		// on the first call.
		VarNode->VariableGuid = Record.VariableGuid;
		VarNode->VariableName = Record.VariableName;
		if (!VarNode->VariableGuid.IsValid() && !Record.VariableName.IsNone())
		{
			// Legacy fallback covers variables living in either array —
			// InternalVariables is checked first because it predates
			// ExposedVariables, so any asset old enough to need this fallback
			// almost certainly stores its variables there. If a name-match hits
			// in ExposedVariables instead, that's fine too: both arrays share
			// the same struct type and identity rules.
			auto BackfillFromArray = [&](const TArray<FComposableCameraInternalVariable>& Array) -> bool
			{
				for (const FComposableCameraInternalVariable& Variable : Array)
				{
					if (Variable.VariableName == Record.VariableName && Variable.VariableGuid.IsValid())
					{
						VarNode->VariableGuid = Variable.VariableGuid;
						return true;
					}
				}
				return false;
			};

			if (!BackfillFromArray(OwningTypeAsset->InternalVariables))
			{
				BackfillFromArray(OwningTypeAsset->ExposedVariables);
			}
		}

		VarNode->bIsSetter = Record.bIsSetter;
		if (Record.NodeGuid.IsValid())
		{
			VarNode->NodeGuid = Record.NodeGuid;
		}
		else
		{
			VarNode->CreateNewGuid();
		}
		VarNode->NodePosX = Record.Position.X;
		VarNode->NodePosY = Record.Position.Y;
		VarNode->AllocateDefaultPins();
		AddNode(VarNode, /*bFromUI=*/false, /*bSelectNewNode=*/false);

		// Key the lookup by the resolved GUID (after the legacy fallback above)
		// so the exec-chain phase can wire Set-variable exec pins by GUID. Skip
		// entries whose GUID is still invalid — those can't participate in exec
		// chain restore until the user re-saves the asset.
		if (VarNode->VariableGuid.IsValid())
		{
			OutVariableGuidToGraphNode.Add(VarNode->VariableGuid, VarNode);
		}

		UEdGraphPin* ValuePin = VarNode->FindPin(UComposableCameraVariableGraphNode::PN_Value);
		if (!ValuePin)
		{
			continue;
		}

		// Choose the graph nodes array based on the record's chain.
		const TArray<UComposableCameraNodeGraphNode*>& EndpointGraphNodes =
			Record.bIsComputeChain ? CreatedComputeGraphNodes : CreatedGraphNodes;

		for (const FComposableCameraVariablePinConnection& Conn : Record.Connections)
		{
			if (!EndpointGraphNodes.IsValidIndex(Conn.CameraNodeIndex))
			{
				continue;
			}

			UComposableCameraNodeGraphNode* EndpointGraphNode = EndpointGraphNodes[Conn.CameraNodeIndex];

			// Get node → input pin; Set node → output pin.
			const EEdGraphPinDirection PinDir = Record.bIsSetter ? EGPD_Output : EGPD_Input;
			if (UEdGraphPin* EndpointPin = EndpointGraphNode->FindPin(Conn.CameraPinName, PinDir))
			{
				ValuePin->MakeLinkTo(EndpointPin);
			}
		}
	}
}

void UComposableCameraNodeGraph::RebuildPhase_RestoreExecutionChain(
	UComposableCameraStartGraphNode* StartNode,
	UComposableCameraOutputGraphNode* OutputNode,
	const TArray<UComposableCameraNodeGraphNode*>& CreatedGraphNodes,
	const TMap<FGuid, UComposableCameraVariableGraphNode*>& VariableGuidToGraphNode)
{
	// Prefer FullExecChain (which can interleave camera nodes with Set-variable
	// nodes). Fall back to the legacy ExecutionOrder projection for assets
	// saved before FullExecChain existed.

	// Resolve a chain entry's ExecIn / ExecOut pin. Switch-on-enum rather than
	// if/else so adding a new EComposableCameraExecEntryType alternative trips
	// the compiler's -Wswitch warning at every call site — this project has a
	// rule about keeping exhaustive handling tight (see .auto-memory feedback
	// entry on TVariant exhaustive handling, same principle applies to enums).
	auto FindExecPin = [&](int32 ChainIndex, EEdGraphPinDirection Dir) -> UEdGraphPin*
	{
		if (!OwningTypeAsset->FullExecChain.IsValidIndex(ChainIndex))
		{
			return nullptr;
		}
		const FComposableCameraExecEntry& Entry = OwningTypeAsset->FullExecChain[ChainIndex];
		switch (Entry.EntryType)
		{
		case EComposableCameraExecEntryType::CameraNode:
		{
			if (CreatedGraphNodes.IsValidIndex(Entry.CameraNodeIndex))
			{
				const FName PinName = (Dir == EGPD_Input)
					? UComposableCameraNodeGraphNode::PN_ExecIn
					: UComposableCameraNodeGraphNode::PN_ExecOut;
				return CreatedGraphNodes[Entry.CameraNodeIndex]->FindPin(PinName, Dir);
			}
			return nullptr;
		}
		case EComposableCameraExecEntryType::SetVariable:
		{
			if (UComposableCameraVariableGraphNode* const* VarNodePtr =
				VariableGuidToGraphNode.Find(Entry.VariableGuid))
			{
				const FName PinName = (Dir == EGPD_Input)
					? UComposableCameraVariableGraphNode::PN_ExecIn
					: UComposableCameraVariableGraphNode::PN_ExecOut;
				return (*VarNodePtr)->FindPin(PinName, Dir);
			}
			return nullptr;
		}
		}
		return nullptr;
	};

	auto FindExecInPin  = [&](int32 ChainIndex) { return FindExecPin(ChainIndex, EGPD_Input);  };
	auto FindExecOutPin = [&](int32 ChainIndex) { return FindExecPin(ChainIndex, EGPD_Output); };

	if (OwningTypeAsset->FullExecChain.Num() > 0)
	{
		// Wire Start → first entry.
		if (UEdGraphPin* FirstExecIn = FindExecInPin(0))
		{
			if (UEdGraphPin* StartExecOut = StartNode->FindPin(
					UComposableCameraStartGraphNode::PN_ExecOut, EGPD_Output))
			{
				StartExecOut->MakeLinkTo(FirstExecIn);
			}
		}

		// Wire each entry to the next.
		for (int32 i = 0; i < OwningTypeAsset->FullExecChain.Num() - 1; ++i)
		{
			UEdGraphPin* OutPin = FindExecOutPin(i);
			UEdGraphPin* InPin = FindExecInPin(i + 1);
			if (OutPin && InPin)
			{
				OutPin->MakeLinkTo(InPin);
			}
		}

		// Wire last entry → Output.
		if (UEdGraphPin* LastExecOut = FindExecOutPin(OwningTypeAsset->FullExecChain.Num() - 1))
		{
			if (UEdGraphPin* OutputExecIn = OutputNode->FindPin(
					UComposableCameraOutputGraphNode::PN_ExecIn, EGPD_Input))
			{
				LastExecOut->MakeLinkTo(OutputExecIn);
			}
		}
	}
	else if (OwningTypeAsset->ExecutionOrder.Num() > 0)
	{
		// Legacy fallback: wire the camera-node-only chain. This path is hit
		// for assets saved before FullExecChain was added — the first SyncToTypeAsset
		// after load will rewrite FullExecChain from the restored wires, migrating
		// the asset forward silently.

		// Wire Start → first node in execution order.
		const int32 FirstExecIndex = OwningTypeAsset->ExecutionOrder[0];
		if (CreatedGraphNodes.IsValidIndex(FirstExecIndex))
		{
			UEdGraphPin* StartExecOut = StartNode->FindPin(UComposableCameraStartGraphNode::PN_ExecOut, EGPD_Output);
			UEdGraphPin* FirstExecIn = CreatedGraphNodes[FirstExecIndex]->FindPin(
				UComposableCameraNodeGraphNode::PN_ExecIn, EGPD_Input);
			if (StartExecOut && FirstExecIn)
			{
				StartExecOut->MakeLinkTo(FirstExecIn);
			}
		}

		// Wire each node to the next in the chain.
		for (int32 i = 0; i < OwningTypeAsset->ExecutionOrder.Num() - 1; ++i)
		{
			const int32 CurrentIdx = OwningTypeAsset->ExecutionOrder[i];
			const int32 NextIdx = OwningTypeAsset->ExecutionOrder[i + 1];

			if (CreatedGraphNodes.IsValidIndex(CurrentIdx) && CreatedGraphNodes.IsValidIndex(NextIdx))
			{
				UEdGraphPin* ExecOut = CreatedGraphNodes[CurrentIdx]->FindPin(
					UComposableCameraNodeGraphNode::PN_ExecOut, EGPD_Output);
				UEdGraphPin* ExecIn = CreatedGraphNodes[NextIdx]->FindPin(
					UComposableCameraNodeGraphNode::PN_ExecIn, EGPD_Input);
				if (ExecOut && ExecIn)
				{
					ExecOut->MakeLinkTo(ExecIn);
				}
			}
		}

		// Wire last node → Output.
		const int32 LastExecIndex = OwningTypeAsset->ExecutionOrder.Last();
		if (CreatedGraphNodes.IsValidIndex(LastExecIndex))
		{
			UEdGraphPin* LastExecOut = CreatedGraphNodes[LastExecIndex]->FindPin(
				UComposableCameraNodeGraphNode::PN_ExecOut, EGPD_Output);
			UEdGraphPin* OutputExecIn = OutputNode->FindPin(
				UComposableCameraOutputGraphNode::PN_ExecIn, EGPD_Input);
			if (LastExecOut && OutputExecIn)
			{
				LastExecOut->MakeLinkTo(OutputExecIn);
			}
		}
	}
}

void UComposableCameraNodeGraph::RebuildPhase_RestoreComputeExecutionChain(
	UComposableCameraBeginPlayStartGraphNode* BeginPlayStartNode,
	const TArray<UComposableCameraNodeGraphNode*>& CreatedComputeGraphNodes,
	const TMap<FGuid, UComposableCameraVariableGraphNode*>& VariableGuidToGraphNode)
{
	// Mirror of RebuildPhase_RestoreExecutionChain for the compute chain.
	// Prefers ComputeFullExecChain (which interleaves compute nodes with
	// Set-variable nodes) and falls back to the flat ComputeExecutionOrder
	// for legacy assets. The compute chain has no Output sentinel — the walk
	// stops at the last entry.

	if (!BeginPlayStartNode)
	{
		return;
	}

	if (OwningTypeAsset->ComputeFullExecChain.Num() > 0)
	{
		// Resolve a chain entry's ExecIn / ExecOut pin. Mirrors the camera
		// chain's lambda in RebuildPhase_RestoreExecutionChain.
		auto FindExecPin = [&](int32 ChainIndex, EEdGraphPinDirection Dir) -> UEdGraphPin*
		{
			if (!OwningTypeAsset->ComputeFullExecChain.IsValidIndex(ChainIndex))
			{
				return nullptr;
			}
			const FComposableCameraExecEntry& Entry = OwningTypeAsset->ComputeFullExecChain[ChainIndex];
			switch (Entry.EntryType)
			{
			case EComposableCameraExecEntryType::CameraNode:
			{
				if (CreatedComputeGraphNodes.IsValidIndex(Entry.CameraNodeIndex))
				{
					const FName PinName = (Dir == EGPD_Input)
						? UComposableCameraNodeGraphNode::PN_ExecIn
						: UComposableCameraNodeGraphNode::PN_ExecOut;
					return CreatedComputeGraphNodes[Entry.CameraNodeIndex]->FindPin(PinName, Dir);
				}
				return nullptr;
			}
			case EComposableCameraExecEntryType::SetVariable:
			{
				if (UComposableCameraVariableGraphNode* const* VarNodePtr =
					VariableGuidToGraphNode.Find(Entry.VariableGuid))
				{
					const FName PinName = (Dir == EGPD_Input)
						? UComposableCameraVariableGraphNode::PN_ExecIn
						: UComposableCameraVariableGraphNode::PN_ExecOut;
					return (*VarNodePtr)->FindPin(PinName, Dir);
				}
				return nullptr;
			}
			}
			return nullptr;
		};

		// Wire BeginPlayStart → first entry.
		if (UEdGraphPin* FirstExecIn = FindExecPin(0, EGPD_Input))
		{
			if (UEdGraphPin* BpExecOut = BeginPlayStartNode->FindPin(
					UComposableCameraBeginPlayStartGraphNode::PN_ExecOut, EGPD_Output))
			{
				BpExecOut->MakeLinkTo(FirstExecIn);
			}
		}

		// Wire each entry to the next.
		for (int32 i = 0; i < OwningTypeAsset->ComputeFullExecChain.Num() - 1; ++i)
		{
			UEdGraphPin* OutPin = FindExecPin(i, EGPD_Output);
			UEdGraphPin* InPin = FindExecPin(i + 1, EGPD_Input);
			if (OutPin && InPin)
			{
				OutPin->MakeLinkTo(InPin);
			}
		}

		// No terminating sentinel — the chain simply stops at the last entry.
	}
	else if (OwningTypeAsset->ComputeExecutionOrder.Num() > 0)
	{
		// Legacy fallback: flat int32 array, compute-nodes only.
		auto FindComputeExecPin = [&](int32 ComputeIdx, EEdGraphPinDirection Dir) -> UEdGraphPin*
		{
			if (!CreatedComputeGraphNodes.IsValidIndex(ComputeIdx))
			{
				return nullptr;
			}
			const FName PinName = (Dir == EGPD_Input)
				? UComposableCameraNodeGraphNode::PN_ExecIn
				: UComposableCameraNodeGraphNode::PN_ExecOut;
			return CreatedComputeGraphNodes[ComputeIdx]->FindPin(PinName, Dir);
		};

		// Wire BeginPlayStart → first entry.
		const int32 FirstComputeIdx = OwningTypeAsset->ComputeExecutionOrder[0];
		if (UEdGraphPin* FirstExecIn = FindComputeExecPin(FirstComputeIdx, EGPD_Input))
		{
			if (UEdGraphPin* BpExecOut = BeginPlayStartNode->FindPin(
					UComposableCameraBeginPlayStartGraphNode::PN_ExecOut, EGPD_Output))
			{
				BpExecOut->MakeLinkTo(FirstExecIn);
			}
		}

		// Wire each entry to the next.
		for (int32 i = 0; i < OwningTypeAsset->ComputeExecutionOrder.Num() - 1; ++i)
		{
			const int32 CurIdx = OwningTypeAsset->ComputeExecutionOrder[i];
			const int32 NextIdx = OwningTypeAsset->ComputeExecutionOrder[i + 1];

			UEdGraphPin* OutPin = FindComputeExecPin(CurIdx, EGPD_Output);
			UEdGraphPin* InPin  = FindComputeExecPin(NextIdx, EGPD_Input);
			if (OutPin && InPin)
			{
				OutPin->MakeLinkTo(InPin);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
