// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "Debug/ComposableCameraDebugPanelData.h"
#include "UObject/Object.h"
#include "ComposableCameraEvaluationTree.generated.h"

class UComposableCameraTransitionBase;
class AComposableCameraCameraBase;
class UComposableCameraDirector;
class FComposableCameraDebugDrawSink;

// Forward declaration: inner nodes hold TSharedPtr to tree nodes.
struct FComposableCameraEvaluationTreeNode;

/**
 * Leaf node wrapper: wraps a single running camera.
 *
 * No per-wrapper memoization is kept here. Multiple TSharedPtrs may point
 * at the SAME Leaf-wrapped node (that's exactly what makes the evaluation
 * graph a DAG under the snapshot-RefLeaf scheme) but they all call into
 * the same `AComposableCameraCameraBase::TickCamera`, which itself
 * short-circuits on `LastTickedFrameCounter == GFrameCounter` and returns
 * the cached pose without re-walking the node chain. Caching at that
 * layer is authoritative for any caller.
 */
struct FComposableCameraEvaluationTreeLeafNodeWrapper
{
	TObjectPtr<AComposableCameraCameraBase> RunningCamera { nullptr };

	/** When true, Evaluate returns the camera's cached pose without ticking it. */
	bool bFrozen { false };

	FComposableCameraPose Evaluate(float DeltaTime);
};

/**
 * Reference leaf node wrapper: a lightweight leaf that re-evaluates a
 * captured SUBTREE SNAPSHOT, not a live Director.
 *
 * Semantics (snapshot, not live):
 *   The RefLeaf holds a `TSharedPtr` to the node that was the source
 *   director's tree root AT THE TIME THE REFLEAF WAS CREATED. Even if the
 *   source director's tree root is later swapped out (e.g. wrapped by a
 *   pop Inner), the RefLeaf keeps pointing at the original snapshot.
 *
 * Why snapshot instead of live:
 *   A live reference produces self-recursion during a pop-while-push
 *   scenario: A.tree.root is now Inner(pop, RefLeaf A, OldA), B.tree.root
 *   is still Inner(push, RefLeaf B, CamB); evaluating A->B -> A loops.
 *   A snapshot captures exactly the subtree that should contribute to the
 *   blend (the original OldA leaf, not the new wrapped root), so the
 *   topology becomes a DAG with no cycles.
 *
 * Ownership:
 *   Does NOT own cameras. Shared `TSharedPtr<Node>` means the subtree
 *   stays alive as long as any RefLeaf references it. Cameras inside the
 *   snapshot are owned by their original Director and destroyed on that
 *   Director's DestroyAllCameras (usually fired from the transition-
 *   finished delegate after this RefLeaf has already been collapsed).
 *
 * Debug-only Director pointer:
 *   `DebugSourceDirector` is kept purely for label display
 *   (e.g. "[RefLeaf] -> Director_Gameplay" in the debug panel). The
 *   runtime path never calls through it. Use only the SnapshotRoot.
 */
struct FComposableCameraEvaluationTreeReferenceLeafNodeWrapper
{
	/** Captured subtree to re-evaluate. Non-owning in the UObject sense - `TSharedPtr` keeps the tree struct alive; UObject references inside
	 *  the subtree are tracked via `AddTreeReferencedObjects`. */
	TSharedPtr<FComposableCameraEvaluationTreeNode> SnapshotRoot;

	/** Debug-only: the director that provided the snapshot. Used for
	 *  human-readable labels; never dereferenced on the evaluation path. */
	TWeakObjectPtr<UComposableCameraDirector> DebugSourceDirector;

	/** When true, Evaluate returns the last cached pose from the snapshot
	 *  without re-evaluating it. The whole captured subtree is held
	 *  frozen for the duration of this RefLeaf's life. */
	bool bFrozen { false };

	/** Cached pose from the last live evaluation. Used both for the
	 *  `bFrozen == true` path and as the cheap answer when the caller
	 *  already retrieved it earlier this frame. */
	FComposableCameraPose CachedPose;

	/** GFrameCounter at the last time the snapshot was evaluated. */
	uint64 LastEvaluatedFrameCounter { 0 };

	FComposableCameraPose Evaluate(float DeltaTime);
};

/**
 * Inner node wrapper: wraps a transition that blends between a source (left) and target (right) subtree.
 * Owns its child nodes via shared pointers.
 *
 * Per-frame memoization (same rationale as LeafNodeWrapper): a snapshot
 * DAG can traverse the same Inner node twice in one frame, and
 * `Transition->Evaluate` decrements `RemainingTime`. So a naive re-entry
 * would make the transition run at 2x speed. Cache the blended pose on
 * first access and return it verbatim on every subsequent access in the
 * same frame.
 */
struct FComposableCameraEvaluationTreeInnerNodeWrapper
{
	TObjectPtr<UComposableCameraTransitionBase> Transition { nullptr };
	TSharedPtr<FComposableCameraEvaluationTreeNode> LeftNode;
	TSharedPtr<FComposableCameraEvaluationTreeNode> RightNode;

	/** GFrameCounter at the last time this node's Evaluate advanced the transition. */
	uint64 LastEvaluatedFrameCounter { 0 };

	/** Blended pose from the last Evaluate at `LastEvaluatedFrameCounter`. */
	FComposableCameraPose CachedBlendedPose;

	FComposableCameraPose Evaluate(float DeltaTime);
};

/** A node in the evaluation tree. Can be a leaf (camera), a reference leaf (another context's Director), or an inner (transition) node. */
USTRUCT()
struct FComposableCameraEvaluationTreeNode
{
	GENERATED_BODY()

public:
	// This node can wrap a running camera (leaf), a reference to another context (reference leaf), or a transition with children (inner).
	TVariant<FComposableCameraEvaluationTreeLeafNodeWrapper, FComposableCameraEvaluationTreeReferenceLeafNodeWrapper, FComposableCameraEvaluationTreeInnerNodeWrapper> Wrapper;

public:
	FComposableCameraPose Evaluate(float DeltaTime);

	/** Returns true if this is a leaf node (wraps a camera). */
	bool IsLeaf() const;

	/** Returns true if this is a reference leaf node (wraps another context's Director). */
	bool IsReferenceLeaf() const;

	/** Returns true if this is an inner node (wraps a transition). */
	bool IsInner() const;

	/** Access the leaf wrapper. Only valid when IsLeaf() is true. */
	FComposableCameraEvaluationTreeLeafNodeWrapper& AsLeaf();
	const FComposableCameraEvaluationTreeLeafNodeWrapper& AsLeaf() const;

	/** Access the reference leaf wrapper. Only valid when IsReferenceLeaf() is true. */
	FComposableCameraEvaluationTreeReferenceLeafNodeWrapper& AsReferenceLeaf();
	const FComposableCameraEvaluationTreeReferenceLeafNodeWrapper& AsReferenceLeaf() const;

	/** Access the inner wrapper. Only valid when IsInner() is true. */
	FComposableCameraEvaluationTreeInnerNodeWrapper& AsInner();
	const FComposableCameraEvaluationTreeInnerNodeWrapper& AsInner() const;
};

/**
 * Evaluation tree for composable cameras. Manages the blending tree of active cameras and transitions.
 *
 * The tree is structured as follows:
 *   - Leaf nodes wrap a single active camera.
 *   - Inner nodes wrap a transition that blends between a source (left child) and target (right child) subtree.
 *
 * When a new camera is activated:
 *   - With a transition: the current tree becomes the left (source) subtree, the new camera becomes
 *     a new right (target) leaf, and an inner node wrapping the transition becomes the new root.
 *   - Without a transition (camera cut): the tree is replaced with a single leaf node for the new camera.
 *
 * When a transition finishes, the tree is collapsed: the inner node is replaced by its right (target) subtree.
 *
 * You should be very careful about *transient* cameras, because they may break the camera chain you'd expect.
 */
UCLASS(Classgroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraEvaluationTree
	: public UObject
{
	GENERATED_BODY()

public:
	UComposableCameraEvaluationTree(const FObjectInitializer& ObjectInitializer);

	/** Evaluate the full tree for this frame and return the final blended camera pose. */
	[[nodiscard]] FComposableCameraPose Evaluate(float DeltaTime);

	/** Called when a new camera is activated, optionally with a transition from the current state.
	 *
	 * When the root is an inter-context transition (left child is a reference leaf),
	 * the activation nests under the right (target) subtree instead of wrapping the
	 * entire tree. This preserves the inter-context blend at the root while allowing
	 * intra-context camera switches to happen underneath:
	 *
	 *   Before:                                After (nested):
	 *   [InterCtx Transition]                  [InterCtx Transition]
	 *    /               \                      /               \
	 *  [RefLeaf]      [CamB1]                [RefLeaf]     [IntraCtx Transition]
	 *                                                        /              \
	 *                                                    [CamB1]         [CamB2]
	 *
	 * When the inter-context transition finishes (the reference leaf side collapses),
	 * only the right subtree survives. Which is the intra-context blend, exactly as expected.
	 */
	void OnActivateNewCamera(AComposableCameraCameraBase* NewCamera, UComposableCameraTransitionBase* Transition, bool bFreezeSourceCamera = false);

	/**
	 * Activate a new camera with a snapshot of another context's tree as the
	 * transition source. Used for inter-context PUSH transitions.
	 *
	 * The reference leaf built here captures
	 * `SourceDirector->GetEvaluationTree()->GetRootNode()` by `TSharedPtr` at
	 * call time. Subsequent mutations to the source director's root (e.g. the
	 * source later being popped and wrapped by its own pop Inner) do NOT
	 * follow into this RefLeaf. It keeps evaluating the captured subtree
	 * verbatim. That is what prevents cycles during pop-while-push-still-
	 * active (see DesignDoc Section "Inter-Context Transitions").
	 *
	 * @param NewCamera The new camera to activate in this context.
	 * @param Transition The transition to blend from the captured subtree's output to NewCamera.
	 * @param SourceDirector The source director whose tree is being snapshotted RIGHT NOW.
	 * @param bFreezeSourceCamera When true, the snapshot holds its last pose
	 *                            instead of re-evaluating each frame. Purely
	 *                            a semantic option for authors who want
	 *                            freeze-on-push behaviour; NOT required for
	 *                            correctness.
	 */
	void OnActivateNewCameraWithReferenceSource(
		AComposableCameraCameraBase* NewCamera,
		UComposableCameraTransitionBase* Transition,
		UComposableCameraDirector* SourceDirector,
		bool bFreezeSourceCamera = false);

	/**
	 * Wrap the current tree's `RootNode` (the resuming camera, intact with
	 * all its accumulated per-node state) as the TARGET of a new pop
	 * transition, with a reference leaf capturing `SourceDirector`'s tree
	 * root as a snapshot on the SOURCE side. Used by context-stack pops so
	 * the resumed camera is the ORIGINAL instance that was running before
	 * the push. Not a fresh instance spawned at pop time with zeroed
	 * damping / interpolator / spline-progress / etc. state.
	 *
	 * The snapshot-based RefLeaf (see comments on the RefLeaf wrapper and
	 * `OnActivateNewCameraWithReferenceSource`) is the reason this is safe
	 * even when the source director's tree still holds a push-side RefLeaf
	 * pointing back to us: snapshots don't follow root mutations, so the
	 * resulting reachable graph is a DAG, not a cycle.
	 *
	 * Preserves the invariant "every camera is an instance; same-type
	 * cameras don't share lifecycle": the pre-push camera keeps ticking
	 * through the push period (via the pushed context's reference leaf
	 * snapshot) and at pop it is re-wrapped. Never destroyed, never
	 * replaced by a sibling instance.
	 *
	 * Precondition: `RootNode` is non-null (there IS a camera to resume).
	 * Callers that don't have a pre-existing camera should use
	 * `OnActivateNewCameraWithReferenceSource` instead.
	 */
	void OnResumeCurrentTreeWithReferenceSource(
		UComposableCameraTransitionBase* Transition,
		UComposableCameraDirector* SourceDirector,
		bool bFreezeSourceCamera = false);

	/** Returns true if the tree has at least one active camera. */
	bool HasActiveCamera() const;

	/** Get the current running camera (set when a camera is activated, updated on tree rebuild). */
	AComposableCameraCameraBase* GetRunningCamera() const { return RunningCamera; }

	/** Read-only access to the current root node. Used by director-to-director
	 *  inter-context APIs to capture a `TSharedPtr` snapshot of this tree's
	 *  current shape when creating a reference leaf in another tree. The
	 *  returned pointer is shared (not copied), so the snapshot keeps the
	 *  captured subtree alive even if THIS tree later swaps its root.
	 *  Returns null if no camera has been activated yet. */
	const TSharedPtr<FComposableCameraEvaluationTreeNode>& GetRootNode() const { return RootNode; }

	/** Destroy all cameras in the tree and reset to empty. */
	void DestroyAll();

	/** Build a flat DFS pre-order snapshot of the tree for every debug consumer
	 *  (2D panel, `showdebug camera`, dump commands. All render from the
	 *  snapshot through `ComposableCameraDebug::AppendTreeNodeLine`). Appends to
	 *  OutNodes (caller is responsible for Reset if starting fresh). Computes
	 *  bIsDominantLeaf as part of the walk (root->Right* ->leaf). */
	void BuildDebugSnapshot(TArray<FComposableCameraTreeNodeSnapshot>& OutNodes) const;

#if !UE_BUILD_SHIPPING
	/**
	 * Walk every Inner (transition) node reachable from the tree's root
	 * and invoke `UComposableCameraTransitionBase::DrawTransitionDebug` on
	 * each. Each transition self-gates on its own
	 * `CCS.Debug.Viewport.Transitions.<Name>` CVar. If none are enabled,
	 * this call is effectively free.
	 *
	 * When the walk hits a reference leaf, it recurses into the captured
	 * `SnapshotRoot` so any intra-context transition still in flight inside
	 * the snapshot is also drawn. Since snapshot RefLeaves form a DAG
	 * (multiple RefLeaves can share a SnapshotRoot; one Inner inside a
	 * snapshot can be reached via two paths), visited-node deduplication
	 * inside the recursive helper ensures each transition draws once per
	 * frame.
	 *
	 * @param World                  World to draw into (routed through the
	 *                               LineBatcher -> visible in every viewport).
	 * @param bViewerIsOutsideCamera Same convention as node debug: true
	 *                               while the player is NOT looking through
	 *                               the blended camera. Passed through to
	 *                               each transition's override so it can
	 *                               gate frustum pieces.
	 *
	 * Compiled out in shipping builds.
	 */
	void DrawTransitionsDebug(class UWorld* World, bool bViewerIsOutsideCamera) const;
	void DrawTransitionsDebug(FComposableCameraDebugDrawSink& Draw, bool bViewerIsOutsideCamera) const;
#endif

	// UObject interface.
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

private:
	/** Root of the evaluation tree. Null if no camera has been activated yet. */
	TSharedPtr<FComposableCameraEvaluationTreeNode> RootNode;

	/** Currently running (target) camera. */
	UPROPERTY(Transient)
	TObjectPtr<AComposableCameraCameraBase> RunningCamera;

	/**
	 * Subtrees detached from `RootNode` by a previous activation but whose
	 * camera actors must stay alive until the activation's transition
	 * finishes.
	 *
	 * Why defer: `OnActivateNewCameraWithReferenceSource` replaces the root
	 * with `Inner(T, RefLeaf->source director snapshot, NewLeaf)`. If the
	 * source director had been PUSHED onto us earlier, its snapshot was
	 * captured FROM our old root. So the source director's RefLeaf holds a
	 * TSharedPtr to our pre-replacement RootNode. Destroying that subtree's
	 * cameras immediately would leave the source director's Tick path
	 * walking into `Leaf.RunningCamera` actors that were marked PendingKill
	 * mid-blend. Symptom (before fix): `[leaf] (destroyed)` rows in both
	 * context trees in the Debug Panel + `"RunningCamera is null or
	 * destroyed when evaluating leaf node."` spam from
	 * `FComposableCameraEvaluationTreeLeafNodeWrapper::Evaluate` while T is
	 * still in flight.
	 *
	 * When it is safe to destroy: once T finishes, `CollapseFinishedTransitions`
	 * drops the RefLeaf branch from our new root. At that moment the old
	 * subtree is no longer reachable from *our* tree, so its actors can be
	 * destroyed without affecting anything this director walks.
	 *
	 * Cleanup triggers (in priority order):
	 *   1. `InTransition->OnTransitionFinishesDelegate`. Normal completion.
	 *   2. `DestroyAll()`. Shutdown / context pop.
	 * Edge case: if the transition is replaced (another activation fires
	 * before T completes), the pending subtree lingers here until (2)
	 * eventually fires. That's a memory cost, not a correctness bug.
	 *
	 * GC: the entries here are registered with the reference collector via
	 * `AddReferencedObjects` so the cameras/transitions inside stay alive
	 * until we explicitly destroy them below.
	 */
	TArray<TSharedPtr<FComposableCameraEvaluationTreeNode>> PendingDestroyOldRoots;

	/**
	 * Collapse finished transitions in the tree.
	 *
	 * When an inner node's transition is finished (or its source is destroyed),
	 * the node is replaced by its right (target) subtree and the left (source)
	 * subtree's cameras are destroyed.
	 *
	 * Transient cameras are not managed here. They live in separate contexts
	 * and their lifecycle is handled by the context stack's auto-pop mechanism.
	 *
	 * @return The node that should replace the input node in the tree.
	 */
	TSharedPtr<FComposableCameraEvaluationTreeNode> CollapseFinishedTransitions(
		const TSharedPtr<FComposableCameraEvaluationTreeNode>& Node);

	/**
	 * Recursively destroy all camera actors referenced by a subtree.
	 */
	void DestroySubtreeCameras(const TSharedPtr<FComposableCameraEvaluationTreeNode>& Node);

	/**
	 * Recursively set bFrozen on all leaf and reference-leaf nodes in a subtree.
	 * Used when bFreezeSourceCamera is set on activation. The entire outgoing
	 * blend tree holds its last pose during the transition.
	 */
	static void FreezeSubtree(const TSharedPtr<FComposableCameraEvaluationTreeNode>& Node, bool bFrozen);

	/** Recursively flatten a subtree into the snapshot node array.
	 *  DominantNodePtr marks the one node along the root->Right* ->leaf chain
	 *  that should be tagged bIsDominantLeaf = true.
	 *  bIsLastSibling is false for Left children of Inner nodes, true for Right
	 *  children and for the tree root. AncestorLastFlagsBitmask carries the
	 *  ancestor chain's last-child flags so the renderer can draw proper
	 *  ``|/L`/  connectors and ` continuations.
	 *
	 *  When the walk hits a ReferenceLeaf with a valid SnapshotRoot, it emits
	 *  the RefLeaf node first and then recursively flattens the referenced
	 *  subtree inline (depth bumped by 1). All nodes flattened from the
	 *  referenced subtree get `bInReferencedSubtree = true`, and the subtree's
	 *  root additionally gets `bIsReferencedRoot = true` so the renderer can
	 *  suppress the `[from]/[to]` role prefix at the RefLeaf seam (the
	 *  invariant "Depth > 0 means transition parent" does not hold across that
	 *  boundary). `bInReferencedSubtree` is propagated through recursive calls
	 *  via the extra argument; callers from a regular tree pass the default
	 *  false. Dominant-leaf tagging is skipped inside referenced subtrees - the frozen source snapshot does not participate in the active tree's
	 *  collapse chain. */
	static void BuildNodeDebugSnapshot(
		const TSharedPtr<FComposableCameraEvaluationTreeNode>& Node,
		int32 Depth,
		bool bIsLastSibling,
		uint32 AncestorLastFlagsBitmask,
		const FComposableCameraEvaluationTreeNode* DominantNodePtr,
		TArray<FComposableCameraTreeNodeSnapshot>& OutNodes,
		bool bInReferencedSubtree = false);

	/** Recursively register UObject references in the tree for garbage collection. */
	static void AddTreeReferencedObjects(
		const TSharedPtr<FComposableCameraEvaluationTreeNode>& Node,
		FReferenceCollector& Collector);

#if !UE_BUILD_SHIPPING
	/** Recursive companion to `DrawTransitionsDebug`. Inner nodes invoke the
	 *  transition's override and recurse into both children. ReferenceLeaf
	 *  nodes recurse into the captured SnapshotRoot so intra-blend transitions
	 *  inside the snapshot still draw. Leaf nodes terminate.
	 *
	 *  `VisitedNodes` is a DAG-deduplication set: two RefLeaves can share
	 *  the same SnapshotRoot (or an Inner inside a snapshot can be reached
	 *  via two paths). Skipping already-visited nodes prevents a transition
	 *  from drawing its gizmos twice per frame. */
	static void DrawTransitionsNodeDebug(
		const TSharedPtr<FComposableCameraEvaluationTreeNode>& Node,
		FComposableCameraDebugDrawSink& Draw,
		bool bViewerIsOutsideCamera,
		TSet<const FComposableCameraEvaluationTreeNode*>& VisitedNodes);
#endif
};
