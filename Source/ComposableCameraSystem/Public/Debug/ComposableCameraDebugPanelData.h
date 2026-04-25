// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Cameras/ComposableCameraCameraBase.h"  // for FComposableCameraPose

/**
 * Runtime debug snapshot structures consumed by FComposableCameraDebugPanel.
 *
 * These are distinct from the editor-side FComposableCameraDebugSnapshot
 * (Core/ComposableCameraDebugSnapshot.h, WITH_EDITOR only). The editor one
 * captures a SINGLE camera's per-node state for the Type Asset Editor's
 * graph overlay. These structs capture the entire Tier-1 context stack +
 * each context's Tier-2 evaluation tree, for the in-viewport debug panel
 * (runtime, always available).
 *
 * Design:
 *   - Tree nodes are flattened DFS pre-order with a Depth field, so the
 *     renderer does not need recursion and can pick connector glyphs
 *     (vertical stem + elbow) from a single pass.
 *   - All pointer data is resolved eagerly into display strings at
 *     snapshot time — consumers never deref anything runtime-owned.
 *     This makes the snapshot safe to cache and freeze.
 *   - Progress / lifetime fields are captured as floats, not pre-formatted
 *     strings, so the renderer can draw real progress bars instead of
 *     parsing text.
 */

/** Kind of an evaluation-tree node. Parallels the TVariant in FComposableCameraEvaluationTreeNode. */
enum class EComposableCameraTreeNodeKind : uint8
{
	Leaf,              // Wraps a running camera actor.
	ReferenceLeaf,     // Wraps another context's Director (inter-context blend).
	InnerTransition,   // Wraps a transition + Left (source) / Right (target) children.
};

/** One node in an evaluation tree, flattened. Produced by
 *  UComposableCameraEvaluationTree::BuildDebugSnapshot. */
struct FComposableCameraTreeNodeSnapshot
{
	/** Node kind. */
	EComposableCameraTreeNodeKind Kind = EComposableCameraTreeNodeKind::Leaf;

	/** Depth in the tree (root of this director's tree is 0). */
	int32 Depth = 0;

	/** Primary display label (camera name / director name / transition class name). */
	FString DisplayLabel;

	/** True if the underlying pointer was IsValid-false at snapshot time. */
	bool bDestroyed = false;

	/** True for the single "dominant" leaf in this tree — the one that would
	 *  remain if every transition collapsed immediately. Computed by walking
	 *  root → Right → Right → ... → leaf. Leaves that are part of an active
	 *  transition's source (Left) side will have this as false. */
	bool bIsDominantLeaf = false;

	/** True if this node is the last child of its parent. For an InnerTransition
	 *  parent, the Left (source) child has this false and the Right (target)
	 *  child has this true. Trivially true for tree roots. Drives whether the
	 *  connector glyph is drawn as `└` (last) or `├` (middle). */
	bool bIsLastSibling = true;

	/** Bitmask where bit L is set iff the ancestor at depth L was the last child
	 *  of its parent. Drives whether a continuation stem `│` is drawn at column L
	 *  for this line: stem present iff bit (L+1) is 0 (i.e. the ancestor at
	 *  depth L+1 was NOT last, so its parent's subtree is still incomplete).
	 *
	 *  A 32-bit mask caps tree depth at 32 for debug visualization, which is
	 *  ~30 levels beyond anything a real camera composition produces. */
	uint32 AncestorLastFlagsBitmask = 0;

	// ---- Leaf-only fields (Kind == Leaf) ----
	/** True if the leaf's camera is marked transient. */
	bool bIsTransient = false;
	/** Lifetime elapsed in seconds (only meaningful when bIsTransient). */
	float LifeElapsed = 0.f;
	/** Total lifetime in seconds (only meaningful when bIsTransient). */
	float LifeTotal = 0.f;

	// ---- InnerTransition-only fields (Kind == InnerTransition) ----
	/** Transition progress in [0..1]. -1 if transition is null. */
	float TransitionProgress = -1.f;
	/** Elapsed transition time in seconds. */
	float TransitionElapsed = 0.f;
	/** Total transition time in seconds. */
	float TransitionTotal = 0.f;

	/** True if this node was flattened from a ReferenceLeaf's `SnapshotRoot` —
	 *  i.e. it belongs to the referenced *source* director's tree that has
	 *  been inlined under a ReferenceLeaf during the snapshot build.
	 *  Panel renderer uses this to pick a dimmer color (the referenced tree
	 *  is a frozen source snapshot, not the active target tree). */
	bool bInReferencedSubtree = false;

	/** True only on the single node that is the direct child of a
	 *  ReferenceLeaf (the referenced subtree's root). Suppresses the
	 *  `[from]/[to]` role prefix at that seam — the usual
	 *  "Depth > 0 ⇒ transition parent" invariant does not hold across the
	 *  RefLeaf boundary (a RefLeaf is a leaf in the outer tree with a
	 *  synthetic 1-child visual expansion; it is not a transition). */
	bool bIsReferencedRoot = false;

	/** Blend-weight curve sampled at N+1 evenly spaced points in [0, 1],
	 *  produced by calling `GetBlendWeightAt(i/N)` on the live transition
	 *  at snapshot-build time. Drives the debug panel's in-row sparkline:
	 *  amber "area-under-curve" filled up to `TransitionProgress` shows
	 *  how far the blend has gone along its timing curve, and a thin
	 *  outline shows the entire curve shape.
	 *
	 *  Empty for non-transition nodes and for transitions whose pointer
	 *  was null at snapshot time. 24 samples (25 values) is the
	 *  convention used by `BuildNodeDebugSnapshot` — enough for Ease /
	 *  Cubic / Smoother shoulders to read distinctly at typical panel
	 *  widths, while staying a trivial allocation cost. */
	TArray<float> BlendCurveSamples;
};

/** One active Camera Patch on a Director. Produced by
 *  UComposableCameraPatchManager::BuildDebugSnapshot. Sorted in iteration order
 *  (LayerIndex asc, PushSequence asc — same order Apply runs them). */
struct FComposableCameraPatchSnapshot
{
	/** Patch asset display name; "(missing)" if the weak ptr resolved null. */
	FString AssetName;

	/** Effective layer index (resolved from asset default + AddPatch override). */
	int32 LayerIndex = 0;

	/** Lifecycle phase (0 = Entering, 1 = Active, 2 = Exiting, 3 = Expired).
	 *  Stored as int8 to keep the snapshot decoupled from the runtime enum. */
	int8 Phase = 0;

	/** Current effect alpha — drives BlendBy(InputPose, Evaluated, alpha). */
	float Alpha = 0.f;

	/** Time spent in the current Phase (resets on every transition). */
	float ElapsedInPhase = 0.f;

	/** Time spent in Active phase total (drives the Duration channel). */
	float ElapsedTimeActive = 0.f;

	/** Authored EnterDuration / ExitDuration (in seconds) for ramp progress display. */
	float EnterDuration = 0.f;
	float ExitDuration = 0.f;

	/** Active-phase Duration cap (0 if Duration channel is not enabled). */
	float Duration = 0.f;

	/** Bitmask of EComposableCameraPatchExpirationType — what channels can fire. */
	uint8 ExpirationType = 0;

	/** Auxiliary "expire when running camera changes" flag. */
	bool bExpireOnCameraChange = false;
};

/** One context entry in the stack snapshot. Produced by
 *  UComposableCameraDirector::BuildDebugSnapshot (via the stack). */
struct FComposableCameraContextSnapshot
{
	/** Name of this context (from project settings). */
	FName ContextName = NAME_None;

	/** True if this is the active (top-of-stack) context. */
	bool bIsActive = false;

	/** True if this is the base (index 0) context. */
	bool bIsBase = false;

	/** True if this entry is in PendingDestroyEntries (popped but still
	 *  evaluating as a reference leaf during a transition). */
	bool bIsPendingDestroy = false;

	/** DFS pre-order flattened tree nodes for this context's Director. */
	TArray<FComposableCameraTreeNodeSnapshot> TreeNodes;

	/** Active patches on this context's Director, in Apply iteration order. */
	TArray<FComposableCameraPatchSnapshot> Patches;

	/** Display name of the director's RunningCamera ("(none)" if null). */
	FString RunningCameraDisplay;

	/** Last evaluated pose from this director (for Director::LastEvaluatedPose). */
	FComposableCameraPose LastPose;
};

/** Top-level context stack + tree snapshot consumed by the debug panel.
 *  Produced by UComposableCameraContextStack::BuildDebugSnapshot. */
struct FComposableCameraContextStackSnapshot
{
	/** All contexts — live entries first (LIFO, index 0 = base), then
	 *  pending-destroy entries. The UI walks this as a single flat list. */
	TArray<FComposableCameraContextSnapshot> Contexts;

	/** Total number of live (non-pending-destroy) entries. */
	int32 LiveStackDepth = 0;

	/** Number of pending-destroy entries. */
	int32 PendingDestroyCount = 0;
};
