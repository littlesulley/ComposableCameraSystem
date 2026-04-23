// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "ComposableCameraNodeGraph.generated.h"

class UComposableCameraTypeAsset;
class UComposableCameraCameraNodeBase;
class UComposableCameraNodeGraphNode;
class UComposableCameraVariableGraphNode;
class UComposableCameraStartGraphNode;
class UComposableCameraBeginPlayStartGraphNode;
class UComposableCameraOutputGraphNode;

/**
 * EdGraph subclass that represents the visual node graph for a Camera Type Asset.
 * Each node in this graph corresponds to a node template in the type asset's NodeTemplates array.
 *
 * RebuildFromTypeAsset and SyncToTypeAsset are the two halves of the editor ↔ asset
 * round-trip. Both are decomposed into named phase member functions so that the
 * numbered steps in EditorDesignDoc Section 8 map 1:1 onto function names, and so
 * that any phase can be re-read or modified without scrolling through the whole
 * procedural block. The orchestrators below own all the working data as locals
 * and pass it to phases by reference — phase functions never read or write the
 * reentrancy guard flags themselves; that's the orchestrator's job.
 */
UCLASS()
class COMPOSABLECAMERASYSTEMEDITOR_API UComposableCameraNodeGraph : public UEdGraph
{
	GENERATED_BODY()

public:
	/** The camera type asset that owns this graph. */
	UPROPERTY()
	TObjectPtr<UComposableCameraTypeAsset> OwningTypeAsset;

	/**
	 * Mark the graph as having just received a pin right-click. The schema's
	 * GetContextMenuActions calls this when the user right-clicks an existing
	 * pin, and the toolkit's deferred selection-change handler consults the
	 * flag (via ConsumePinContextMenuRequested below) so it can decline to
	 * repoint the details panel at the pin's owning node — the user was only
	 * opening a pin context menu, not trying to inspect the node.
	 *
	 * This works around SGraphEditor's ordering: a pin right-click fires
	 * OnSelectionChanged BEFORE it calls GetContextMenuActions, so we can
	 * only know in retrospect that the selection change was incidental.
	 *
	 * The flag is single-shot: it stays set until the next
	 * ConsumePinContextMenuRequested call clears it.
	 */
	void MarkPinContextMenuRequested();

	/**
	 * Consume the pin-context-menu flag. Returns true exactly once after each
	 * MarkPinContextMenuRequested call, and false on every subsequent call
	 * until the flag is set again. The toolkit's deferred selection handler
	 * uses this to suppress details-panel updates that were caused by a pin
	 * right-click rather than a genuine selection change.
	 */
	bool ConsumePinContextMenuRequested();

	/**
	 * Reentrancy guard set while RebuildFromTypeAsset is executing. The toolkit's
	 * OnGraphChanged handler (which calls SyncToTypeAsset) checks this flag and
	 * bails out if it's true — otherwise the RemoveNode calls in Step 1 of
	 * RebuildFromTypeAsset would fire NotifyGraphChanged, trigger a sync, and
	 * clobber the TypeAsset's durable data with a half-drained NodeTemplates
	 * array. Also guards against the symmetric case (SyncToTypeAsset triggering
	 * a Rebuild mid-sync, which would cause infinite recursion).
	 *
	 * This is belt-and-braces protection: the primary fix is that
	 * UComposableCameraTypeAsset::EditorGraph is now Transient, so load-time
	 * rebuilds always start from an empty graph. But this guard also catches
	 * any subsequent Rebuild-during-Sync paths that might exist now or be
	 * introduced later.
	 */
	bool bIsRebuildingFromTypeAsset = false;

	/** Symmetric guard: set while SyncToTypeAsset is running, so a re-entrant
	 *  OnGraphChanged fired by SyncToTypeAsset itself doesn't recurse. */
	bool bIsSyncingToTypeAsset = false;

	/**
	 * Depth of the currently-active "Details rebuild" scopes. Nonzero means
	 * a property-change lambda in the Details customization is mid-flight and
	 * the toolkit's `OnGraphChanged` handler should *coalesce* — record that a
	 * sync is pending (via `bPendingSyncDuringDetailsRebuild` below) and skip
	 * the immediate `SyncToTypeAsset` call. The outermost scope runs the
	 * coalesced sync exactly once on destruction when the pending flag is set.
	 *
	 * Why a counter and not a bool: a single user property edit on a compound
	 * pin triggers `DetailBuilder.ForceRefreshDetails` inside the outer
	 * `SetOnPropertyValueChanged` lambda. The refresh re-binds every sub-row
	 * and re-fires child property change events, which land in *other* Details
	 * lambdas that also open a scope. Depth-counting lets those nested scopes
	 * participate in the same coalescing window without the outer scope's
	 * pending flag being clobbered on nested construction / destruction.
	 *
	 * Use `FComposableCameraDetailsRebuildScope` at every call site; do not
	 * touch these fields directly. See comment above that struct for the
	 * intended usage pattern.
	 */
	int32 DetailsRebuildScopeDepth = 0;

	/** Set by `OnGraphChanged` when a sync is requested while
	 *  `DetailsRebuildScopeDepth > 0`. Cleared and consumed by the outermost
	 *  `FComposableCameraDetailsRebuildScope` destructor. */
	bool bPendingSyncDuringDetailsRebuild = false;

	/** Rebuild the graph from the type asset's NodeTemplates and PinConnections.
	 *  Called when the asset is first opened or after structural changes.
	 *  Internally orchestrates the RebuildPhase_* functions in order. */
	void RebuildFromTypeAsset();

	/** Synchronize the type asset's data from the graph state.
	 *  Called after graph edits (node add/remove/reorder, pin connect/disconnect).
	 *  Internally orchestrates the SyncPhase_* functions in order.
	 *
	 *  Also runs a silent validation pass (`OwningTypeAsset->Build(false)`) at
	 *  the tail and pushes the resulting BuildMessages onto the graph nodes'
	 *  UEdGraphNode `ErrorMsg` / `bHasCompilerMessage` / `ErrorType` fields,
	 *  which `SComposableCameraGraphNode::OnPaint` reads to render inline
	 *  warning / error badges during editing. */
	void SyncToTypeAsset();

	/** Map the current `OwningTypeAsset->BuildMessages` back onto the live graph
	 *  nodes' UEdGraphNode error fields. Clears those fields on every node
	 *  first, then walks BuildMessages and groups them by `NodeIndex` — each
	 *  camera graph node's `ErrorType` becomes the highest severity among its
	 *  messages and `ErrorMsg` is the concatenation (one per line). Messages
	 *  with `NodeIndex == INDEX_NONE` are asset-level and ignored here; they
	 *  remain visible in the Build Messages tab.
	 *
	 *  Safe to call without `OwningTypeAsset` — becomes a no-op. Does not
	 *  trigger any graph-change notification of its own; the OnPaint widget
	 *  path reads the fields every frame. */
	void ApplyBuildMessagesToGraphNodes();

private:
	/**
	 * Backing storage for the pin-context-menu signal. Only MarkPinContextMenuRequested
	 * and ConsumePinContextMenuRequested touch this field — nothing else reads or
	 * writes it, which is the whole point of the Mark/Consume pair (the field
	 * used to be set and cleared from multiple sites, which made the lifecycle
	 * hard to follow). UPROPERTY(Transient) keeps it out of save files; the
	 * reflection registration is immaterial beyond that.
	 */
	UPROPERTY(Transient)
	bool bPinContextMenuRequested = false;

	/** Denormalized per-variable metadata used by the exec-chain phases to
	 *  translate a variable's `FGuid` to its name + data-block slot size when
	 *  emitting `SetVariable` entries into the asset's FullExecChain.
	 *
	 *  Kept as a stand-alone struct rather than reusing
	 *  `FComposableCameraInternalVariable` so the lookup doesn't pull in the
	 *  full variable record (metadata, default values, categorisation) when
	 *  only the two fields below are needed on the hot path. */
	struct FVariableLookupInfo
	{
		FName Name;
		int32 SlotSize = 0;
	};

	/** Build a `{FGuid -> (Name, SlotSize)}` map from
	 *  `OwningTypeAsset->InternalVariables` ∪ `ExposedVariables`.
	 *
	 *  The two exec-chain sync phases (`SyncPhase_RebuildExecutionChain` and
	 *  `SyncPhase_RebuildComputeExecutionChain`) each need to resolve a
	 *  variable GUID once per Set-variable exec entry they encounter while
	 *  walking the chain. Before this helper existed, each phase inlined a
	 *  nested-lambda linear scan, producing O(N_nodes * M_vars) scans per
	 *  sync. This helper builds the map once (O(N_vars)) and hands it to
	 *  both phases for O(1) GUID lookups. Kept const because it only reads
	 *  the type asset and does not mutate graph state. */
	void BuildVariableLookup(TMap<FGuid, FVariableLookupInfo>& OutLookup) const;

	// ─── SyncToTypeAsset phases ────────────────────────────────────────────
	//
	// Each phase corresponds to one numbered step in the original procedural
	// implementation, preserved in EditorDesignDoc Section 8. Working data
	// (collected node arrays, sentinel pointers, identity maps) lives as
	// orchestrator-local variables and is threaded through the phases by
	// reference, so the data flow between phases is explicit at the call site.

	/** Sync Step 1: walk Nodes, classify each into camera / compute / variable /
	 *  Start / BeginPlay Start / Output buckets, and sort each graph-node bucket
	 *  by its currently-assigned NodeIndex so the previous order is preserved
	 *  (newly created nodes with NodeIndex == INDEX_NONE sort to the end).
	 *
	 *  Camera and compute graph nodes share the UComposableCameraNodeGraphNode
	 *  class; classification happens here by inspecting NodeTemplate's C++
	 *  class identity. The NodeIndex field on each graph node is scoped to its
	 *  own bucket — a camera node's NodeIndex indexes NodeTemplates, a compute
	 *  node's NodeIndex indexes ComputeNodeTemplates, and the two spaces do not
	 *  overlap in the editor. The runtime applies a camera-count offset to the
	 *  compute NodeIndex only when materializing the keyed pin layout. */
	void SyncPhase_CollectGraphNodes(
		TArray<UComposableCameraNodeGraphNode*>& OutCameraGraphNodes,
		TArray<UComposableCameraNodeGraphNode*>& OutComputeGraphNodes,
		TArray<UComposableCameraVariableGraphNode*>& OutVariableGraphNodes,
		UComposableCameraStartGraphNode*& OutStartNode,
		UComposableCameraBeginPlayStartGraphNode*& OutBeginPlayStartNode,
		UComposableCameraOutputGraphNode*& OutOutputNode) const;

	/** Sync Step 2: snapshot the current OwningTypeAsset->NodeTemplates array as
	 *  an old-index → template-pointer map. This is consumed by phase 8
	 *  (MigrateExposedParameters) to translate stale parameter target indices to
	 *  the new indices assigned in phase 3. */
	void SyncPhase_SnapshotOldTemplateIndices(
		TMap<int32, UComposableCameraCameraNodeBase*>& OutOldIndexToTemplate) const;

	/** Sync Step 3: rebuild OwningTypeAsset->NodeTemplates and the parallel
	 *  NodeTemplatePositions array from the collected camera GraphNodes,
	 *  reassigning each GraphNode's NodeIndex to its new position. Also writes
	 *  the Start / Output sentinel positions back to the type asset, and
	 *  populates the template-pointer → new-index map for parameter migration
	 *  in phase 8. */
	void SyncPhase_RebuildNodeTemplatesAndPositions(
		const TArray<UComposableCameraNodeGraphNode*>& GraphNodes,
		const UComposableCameraStartGraphNode* StartNode,
		const UComposableCameraOutputGraphNode* OutputNode,
		TMap<const UComposableCameraCameraNodeBase*, int32>& OutTemplateToNewIndex);

	/** Compute analogue of step 3: rebuild OwningTypeAsset->ComputeNodeTemplates
	 *  and ComputeNodeTemplatePositions from the collected compute graph nodes,
	 *  reassigning each compute GraphNode's NodeIndex to its new position in
	 *  ComputeNodeTemplates. Also writes the BeginPlay Start sentinel position
	 *  back to the type asset. The compute chain has no exposed-parameter
	 *  migration in v1, so no template→index map is produced. */
	void SyncPhase_RebuildComputeTemplatesAndPositions(
		const TArray<UComposableCameraNodeGraphNode*>& ComputeGraphNodes,
		const UComposableCameraBeginPlayStartGraphNode* BeginPlayStartNode);

	/** Sync Step 4: rebuild OwningTypeAsset->PinConnections from camera-node ↔
	 *  camera-node data wires only. Wires whose endpoint is a variable graph
	 *  node are captured separately in phase 7 (RebuildVariableNodeRecords). */
	void SyncPhase_RebuildPinConnections(
		const TArray<UComposableCameraNodeGraphNode*>& GraphNodes);

	/** Compute analogue of step 4: rebuild OwningTypeAsset->ComputePinConnections
	 *  from compute-node ↔ compute-node data wires. Cross-chain wires are
	 *  schema-disallowed in CanCreateConnection, so this phase can assume both
	 *  endpoints live inside ComputeNodeTemplates and index straight into it.
	 *  Variable nodes cannot be wired to compute nodes in v1 — authors who
	 *  need compute→variable dataflow must use SetInternalVariable from inside
	 *  the compute node's Execute() C++ body. */
	void SyncPhase_RebuildComputePinConnections(
		const TArray<UComposableCameraNodeGraphNode*>& ComputeGraphNodes);

	/** Sync Step 5: rebuild the execution chain by walking from Start.ExecOut.
	 *  Camera nodes contribute both an entry to ExecutionOrder (legacy
	 *  camera-only projection) and a CameraNode entry to FullExecChain. Set
	 *  variable nodes contribute only a SetVariable entry to FullExecChain,
	 *  capturing the source camera-node pin that feeds their Value input.
	 *  Get variable nodes and unknown node kinds terminate the walk. */
	void SyncPhase_RebuildExecutionChain(
		const UComposableCameraStartGraphNode* StartNode);

	/** Compute analogue of step 6: rebuild OwningTypeAsset->ComputeExecutionOrder
	 *  by walking from BeginPlayStartNode->ExecOut. The compute chain is
	 *  compute-nodes-only in v1 — variable Set interleave is not supported, so
	 *  there is no FullExecChain-style parallel array for compute, just the
	 *  flat int32 ExecutionOrder. The walk terminates when it runs out of exec
	 *  wires, hits a non-compute node, or revisits a compute node (cycle
	 *  guard). Each traversed node's NodeIndex (which indexes
	 *  ComputeNodeTemplates, not NodeTemplates) is appended in order. */
	void SyncPhase_RebuildComputeExecutionChain(
		const UComposableCameraBeginPlayStartGraphNode* BeginPlayStartNode);

	/** Sync Step 6b (numbered after exec for parity with the original step
	 *  ordering): rebuild OwningTypeAsset->VariableNodes from each variable
	 *  graph node's pin wires. A Get node records the camera-node input pins
	 *  its Value output feeds; a Set node records the camera-node output pins
	 *  feeding into its Value input. */
	void SyncPhase_RebuildVariableNodeRecords(
		const TArray<UComposableCameraVariableGraphNode*>& VariableGraphNodes);

	/** Sync Step 7: walk OwningTypeAsset->ExposedParameters and migrate each
	 *  TargetNodeIndex from the snapshotted old index space (phase 2) to the
	 *  new index space assigned in phase 3, looked up by template-pointer
	 *  identity. Parameters whose template no longer exists are dropped. */
	void SyncPhase_MigrateExposedParameters(
		const TMap<int32, UComposableCameraCameraNodeBase*>& OldIndexToTemplate,
		const TMap<const UComposableCameraCameraNodeBase*, int32>& TemplateToNewIndex);

	/** Sync Step 8: rebuild OwningTypeAsset->NodePinOverrides (parallel array
	 *  to NodeTemplates) from each camera graph node's cached
	 *  RuntimePinOverrides. Writing one entry per GraphNode in the same order
	 *  used by SyncPhase_RebuildNodeTemplatesAndPositions preserves the
	 *  parallel-array invariant by construction — no template-identity
	 *  migration is needed here because the overrides travel on the graph
	 *  node, not on a separate index-keyed array. */
	void SyncPhase_RebuildNodePinOverrides(
		const TArray<UComposableCameraNodeGraphNode*>& GraphNodes);

	/** Compute analogue of step 8: rebuild OwningTypeAsset->ComputeNodePinOverrides
	 *  from each compute graph node's cached RuntimePinOverrides. Must run
	 *  after SyncPhase_RebuildComputeTemplatesAndPositions so
	 *  ComputeNodePinOverrides[i] lines up with ComputeNodeTemplates[i] by
	 *  iteration order. */
	void SyncPhase_RebuildComputeNodePinOverrides(
		const TArray<UComposableCameraNodeGraphNode*>& ComputeGraphNodes);

	// ─── RebuildFromTypeAsset phases ───────────────────────────────────────
	//
	// Mirror image of the Sync phases. Each one creates or wires a piece of
	// the live graph from the durable asset state. The orchestrator owns the
	// CreatedGraphNodes array (parallel to NodeTemplates by index) and the
	// VariableGuidToGraphNode lookup, and threads them through the phases.

	/** Rebuild Step 1: drain every existing UEdGraphNode from this graph via
	 *  RemoveNode. Snapshots Nodes into a local array first so the iteration
	 *  doesn't mutate underfoot. Reentrancy is handled by the orchestrator. */
	void RebuildPhase_RemoveAllGraphNodes();

	/** Rebuild Step 2: instantiate the Start sentinel node, position it from
	 *  OwningTypeAsset->StartNodePosition, and add it to the graph. Returned
	 *  pointer is needed by phase 8 (RestoreExecutionChain). */
	UComposableCameraStartGraphNode* RebuildPhase_CreateStartNode();

	/** Rebuild Step 2b: instantiate the BeginPlay Start sentinel, position it
	 *  from OwningTypeAsset->BeginPlayStartNodePosition, and add it to the
	 *  graph. Returned pointer is threaded into the compute rebuild phases to
	 *  anchor the compute exec chain. Runs unconditionally — the BeginPlay
	 *  sentinel is present on every graph, even one with zero compute nodes,
	 *  so the palette can always offer "drop the first compute node" from a
	 *  visible root. */
	UComposableCameraBeginPlayStartGraphNode* RebuildPhase_CreateBeginPlayStartNode();

	/** Rebuild Step 3: instantiate one camera graph node per entry in
	 *  OwningTypeAsset->NodeTemplates, in order. Uses the saved
	 *  NodeTemplatePositions when its length matches NodeTemplates; otherwise
	 *  falls back to a default horizontal layout (and the next sync will
	 *  silently migrate the asset forward by populating positions). The output
	 *  array is parallel to NodeTemplates by index, so phases 5 and 7 can
	 *  resolve connection endpoints by raw index lookup.
	 *
	 *  This phase also hydrates each graph node's RuntimePinOverrides cache
	 *  from the parallel OwningTypeAsset->NodePinOverrides array (when it
	 *  exists and has the expected length). The hydration must happen
	 *  *before* AllocateDefaultPins is called for the node so that the
	 *  pin-materialization loop can already see the bAsPin toggle and the
	 *  per-asset default values. */
	void RebuildPhase_CreateCameraGraphNodes(
		TArray<UComposableCameraNodeGraphNode*>& OutCreatedGraphNodes);

	/** Compute analogue of step 3: instantiate one compute graph node per
	 *  entry in OwningTypeAsset->ComputeNodeTemplates, hydrating saved canvas
	 *  positions from ComputeNodeTemplatePositions (or a fallback horizontal
	 *  layout below the main camera row when positions are missing) and
	 *  per-instance RuntimePinOverrides from ComputeNodePinOverrides. The
	 *  output array is parallel to ComputeNodeTemplates by index, so compute
	 *  rebuild phases 5 and 7 can resolve connection endpoints by raw index
	 *  lookup within the compute space.
	 *
	 *  Compute graph nodes are instances of UComposableCameraNodeGraphNode,
	 *  the same class used for regular camera graph nodes — they're
	 *  distinguished only by their NodeTemplate's C++ class identity
	 *  (UComposableCameraComputeNodeBase subclass vs plain camera node). */
	void RebuildPhase_CreateComputeGraphNodes(
		TArray<UComposableCameraNodeGraphNode*>& OutCreatedComputeGraphNodes);

	/** Rebuild Step 4: instantiate the Output sentinel node, position it from
	 *  OwningTypeAsset->OutputNodePosition, and add it to the graph. Returned
	 *  pointer is needed by phase 8 (RestoreExecutionChain). */
	UComposableCameraOutputGraphNode* RebuildPhase_CreateOutputNode();

	/** Rebuild Step 5: replay each entry in OwningTypeAsset->PinConnections by
	 *  resolving (SourceNodeIndex, SourcePinName) and (TargetNodeIndex,
	 *  TargetPinName) against the CreatedGraphNodes array and calling
	 *  MakeLinkTo. Out-of-range indices are silently skipped. */
	void RebuildPhase_RestoreCameraNodePinConnections(
		const TArray<UComposableCameraNodeGraphNode*>& CreatedGraphNodes);

	/** Compute analogue of step 5: replay each entry in
	 *  OwningTypeAsset->ComputePinConnections by resolving endpoints against
	 *  the CreatedComputeGraphNodes array (indexed in ComputeNodeTemplates
	 *  space) and calling MakeLinkTo. Cross-chain wires cannot exist in a
	 *  well-formed saved asset, so this phase never needs to look at the
	 *  camera graph-node array. */
	void RebuildPhase_RestoreComputeNodePinConnections(
		const TArray<UComposableCameraNodeGraphNode*>& CreatedComputeGraphNodes);

	/** Rebuild Step 6: instantiate one variable graph node per record in
	 *  OwningTypeAsset->VariableNodes, restore its identity (with the legacy
	 *  GUID-then-name fallback for assets predating the GUID migration),
	 *  position, and Value-pin wires to the appropriate chain's nodes. Uses
	 *  the record's bIsComputeChain flag to choose between CreatedGraphNodes
	 *  (camera chain) and CreatedComputeGraphNodes (compute chain) for
	 *  connection endpoint lookup. Also populates the VariableGuid →
	 *  graph-node lookup so phases 7/7b can wire SetVariable exec entries by
	 *  GUID without rescanning. Variable nodes whose GUID can't be resolved
	 *  are still added to the graph but excluded from the lookup. */
	void RebuildPhase_RestoreVariableGraphNodes(
		const TArray<UComposableCameraNodeGraphNode*>& CreatedGraphNodes,
		const TArray<UComposableCameraNodeGraphNode*>& CreatedComputeGraphNodes,
		TMap<FGuid, UComposableCameraVariableGraphNode*>& OutVariableGuidToGraphNode);

	/** Rebuild Step 7: replay the execution chain. Prefers
	 *  OwningTypeAsset->FullExecChain (which interleaves camera nodes and Set
	 *  variable nodes) and walks it as Start → entry[0] → entry[1] → … → Output.
	 *  For assets saved before FullExecChain existed, falls back to the
	 *  legacy camera-node-only ExecutionOrder array; the first sync after load
	 *  will silently rewrite FullExecChain and migrate the asset forward. */
	void RebuildPhase_RestoreExecutionChain(
		UComposableCameraStartGraphNode* StartNode,
		UComposableCameraOutputGraphNode* OutputNode,
		const TArray<UComposableCameraNodeGraphNode*>& CreatedGraphNodes,
		const TMap<FGuid, UComposableCameraVariableGraphNode*>& VariableGuidToGraphNode);

	/** Compute analogue of step 7: replay the compute execution chain.
	 *  Prefers OwningTypeAsset->ComputeFullExecChain (which interleaves
	 *  compute nodes with Set-variable nodes) and falls back to the flat
	 *  ComputeExecutionOrder for legacy assets. The compute chain has no
	 *  terminating sentinel, so the walk simply stops at the last entry.
	 *  Out-of-range indices are silently skipped. */
	void RebuildPhase_RestoreComputeExecutionChain(
		UComposableCameraBeginPlayStartGraphNode* BeginPlayStartNode,
		const TArray<UComposableCameraNodeGraphNode*>& CreatedComputeGraphNodes,
		const TMap<FGuid, UComposableCameraVariableGraphNode*>& VariableGuidToGraphNode);
};

/**
 * RAII helper that brackets a Details-panel property-change lambda so every
 * `NotifyGraphChanged` it transitively triggers is coalesced into a single
 * `SyncToTypeAsset` call when the outermost scope unwinds.
 *
 * Motivation. A single user edit on a compound pin (e.g. `Interpolator.Speed`)
 * flows through: one top-level `SetOnPropertyValueChanged` lambda runs, which
 * calls `ReconstructPins` → `NotifyGraphChanged` → toolkit `OnGraphChanged`
 * → `SyncToTypeAsset` (the 12-phase graph walk), then finishes by calling
 * `DetailBuilder.ForceRefreshDetails`. The forced refresh re-binds every
 * subobject/child row and re-fires child property-change events, which in turn
 * invoke a *different* `SetOnPropertyValueChanged` lambda that also calls
 * `NotifyGraphChanged`. Three to four sync passes per compound edit is typical.
 *
 * Mechanism. The scope increments a counter on the graph; while the counter is
 * nonzero the toolkit's `OnGraphChanged` handler flips the pending flag and
 * returns without syncing. When the outermost scope unwinds and the pending
 * flag is set, it runs `SyncToTypeAsset` once — collapsing the cascade into a
 * single 12-phase pass. Nested scopes (when `ForceRefreshDetails` re-enters
 * via a child lambda) participate in the same coalescing window thanks to the
 * counter.
 *
 * Intended usage at each `SetOnPropertyValueChanged` call site that calls
 * `NotifyGraphChanged` + `ForceRefreshDetails`:
 *
 *     FSimpleDelegate::CreateLambda([this, &DetailBuilder]()
 *     {
 *         if (UComposableCameraNodeGraphNode* GN = GetGraphNode())
 *         {
 *             FComposableCameraDetailsRebuildScope Scope(
 *                 Cast<UComposableCameraNodeGraph>(GN->GetGraph()));
 *             GN->ReconstructPins();
 *             if (UEdGraph* Graph = GN->GetGraph())
 *             {
 *                 Graph->NotifyGraphChanged();
 *             }
 *             DetailBuilder.ForceRefreshDetails();
 *             // ~Scope runs the coalesced SyncToTypeAsset here.
 *         }
 *     });
 *
 * The scope must wrap `ForceRefreshDetails` — that is the call that re-enters
 * child lambdas. A scope that ends before `ForceRefreshDetails` would defeat
 * the entire coalescing, because the re-entered sync would happen outside the
 * active scope.
 *
 * Null graph is tolerated; the scope becomes a no-op, preserving the existing
 * null-graph fallthrough in the original lambdas.
 */
struct COMPOSABLECAMERASYSTEMEDITOR_API FComposableCameraDetailsRebuildScope
{
	FComposableCameraDetailsRebuildScope(UComposableCameraNodeGraph* InGraph);
	~FComposableCameraDetailsRebuildScope();

	FComposableCameraDetailsRebuildScope(const FComposableCameraDetailsRebuildScope&) = delete;
	FComposableCameraDetailsRebuildScope& operator=(const FComposableCameraDetailsRebuildScope&) = delete;

private:
	TWeakObjectPtr<UComposableCameraNodeGraph> Graph;
};
