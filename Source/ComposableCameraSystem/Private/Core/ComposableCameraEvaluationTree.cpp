// Copyright Sulley. All rights reserved.

#include "Core/ComposableCameraEvaluationTree.h"

#include "ComposableCameraSystemModule.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "Core/ComposableCameraDirector.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Transitions/ComposableCameraTransitionBase.h"

// -------------------------------------------------------
// FComposableCameraEvaluationTreeLeafNodeWrapper
// -------------------------------------------------------

FComposableCameraPose FComposableCameraEvaluationTreeLeafNodeWrapper::Evaluate(float DeltaTime)
{
	if (!IsValid(RunningCamera))
	{
		UE_LOG(LogComposableCameraSystem, Error, TEXT("RunningCamera is null or destroyed when evaluating leaf node."));
		return FComposableCameraPose{};
	}

	if (bFrozen)
	{
		// Source camera is frozen: return its last pose without ticking nodes or updating state.
		return RunningCamera->CameraPose;
	}

	// TickCamera internally short-circuits on same-frame re-entry — see
	// `AComposableCameraCameraBase::TickCamera` + `LastTickedFrameCounter`.
	// Under the DAG evaluation topology the same camera can be reached via
	// multiple paths (e.g. pop-Inner.Right and pop-Inner.Left.RefLeaf
	// snapshot both bottom out at the same original leaf); that guard
	// ensures per-node state advances exactly once per frame regardless.
	return RunningCamera->TickCamera(DeltaTime);
}

// -------------------------------------------------------
// FComposableCameraEvaluationTreeReferenceLeafNodeWrapper
// -------------------------------------------------------

FComposableCameraPose FComposableCameraEvaluationTreeReferenceLeafNodeWrapper::Evaluate(float DeltaTime)
{
	if (!SnapshotRoot.IsValid())
	{
		// Legitimate when the source director had no running camera at the
		// moment the RefLeaf was created (e.g. unit tests that wire a
		// RefLeaf to an empty director on purpose). Warn, don't error —
		// the old director-forwarding path logged at the same severity
		// via UComposableCameraEvaluationTree::Evaluate's empty-root branch.
		UE_LOG(LogComposableCameraSystem, Warning, TEXT("SnapshotRoot is null when evaluating reference leaf node."));
		return FComposableCameraPose{};
	}

	if (bFrozen)
	{
		// Snapshot subtree is frozen: return the cached pose without walking
		// it again. CachedPose is set by the first unfrozen evaluation (if
		// any) or left default — callers that freeze on creation should
		// prime it via the owning director's LastEvaluatedPose if they need
		// a meaningful starting value.
		return CachedPose;
	}

	// Per-frame memoization: two RefLeaves can share the same SnapshotRoot
	// TSharedPtr (same captured subtree referenced from both sides of a
	// bidirectional cross-reference). Cache the result so the subtree is
	// walked at most once per frame per RefLeaf.
	//
	// Note: nodes inside the snapshot subtree have their own per-frame
	// caches too, so even if the snapshot is walked via a different RefLeaf
	// first, the inner Leaf/Inner caches short-circuit the re-walk.
	const uint64 CurrentFrame = GFrameCounter;
	if (LastEvaluatedFrameCounter == CurrentFrame)
	{
		return CachedPose;
	}

	CachedPose = SnapshotRoot->Evaluate(DeltaTime);
	LastEvaluatedFrameCounter = CurrentFrame;
	return CachedPose;
}

// -------------------------------------------------------
// FComposableCameraEvaluationTreeInnerNodeWrapper
// -------------------------------------------------------

DECLARE_CYCLE_STAT(TEXT("EvalTree Evaluate"),           STAT_CCS_EvaluationTree_Evaluate,     STATGROUP_CCS);
DECLARE_CYCLE_STAT(TEXT("EvalTree InnerNode Evaluate"), STAT_CCS_EvalTree_InnerNode_Evaluate, STATGROUP_CCS);

FComposableCameraPose FComposableCameraEvaluationTreeInnerNodeWrapper::Evaluate(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_CCS_EvalTree_InnerNode_Evaluate);
	TRACE_CPUPROFILER_EVENT_SCOPE(CCS_EvalTree_InnerNode_Evaluate);

	if (!RightNode)
	{
		UE_LOG(LogComposableCameraSystem, Error, TEXT("RightNode is null in inner evaluation node."));
		return FComposableCameraPose{};
	}

	// Per-frame memoization: prevents double-advancing the transition's
	// RemainingTime when the DAG routes through this Inner twice in a
	// frame. Must happen BEFORE the subtree walks (so child nodes also
	// only get hit once via this path — though their own per-node caches
	// would catch duplicates anyway, we save unnecessary traversal).
	const uint64 CurrentFrame = GFrameCounter;
	if (LastEvaluatedFrameCounter == CurrentFrame)
	{
		return CachedBlendedPose;
	}

	// Always evaluate the target (right) subtree.
	const FComposableCameraPose TargetPose = RightNode->Evaluate(DeltaTime);

	// Evaluate the source (left) subtree.
	if (!LeftNode)
	{
		CachedBlendedPose = TargetPose;
		LastEvaluatedFrameCounter = CurrentFrame;
		return TargetPose;
	}
	const FComposableCameraPose SourcePose = LeftNode->Evaluate(DeltaTime);

	if (!Transition)
	{
		CachedBlendedPose = TargetPose;
		LastEvaluatedFrameCounter = CurrentFrame;
		return TargetPose;
	}

#if !UE_BUILD_SHIPPING
	// Snapshot source / target for DrawTransitionDebug. We capture only the
	// scalar fields the debug helper reads — NOT the full FComposableCameraPose
	// — because pose.PostProcessSettings embeds TObjectPtr members that
	// wouldn't be GC-tracked through our (non-UPROPERTY) cache. See the
	// FTransitionDebugSnapshot comment in ComposableCameraTransitionBase.h.
	Transition->LastDebugSource = {
		SourcePose.Position,
		SourcePose.Rotation,
		static_cast<float>(SourcePose.GetEffectiveFieldOfView())
	};
	Transition->LastDebugTarget = {
		TargetPose.Position,
		TargetPose.Rotation,
		static_cast<float>(TargetPose.GetEffectiveFieldOfView())
	};
#endif

	// Blend source and target using the transition.
	const FComposableCameraPose BlendedPose = Transition->Evaluate(DeltaTime, SourcePose, TargetPose);

#if !UE_BUILD_SHIPPING
	Transition->LastDebugBlended = {
		BlendedPose.Position,
		BlendedPose.Rotation,
		static_cast<float>(BlendedPose.GetEffectiveFieldOfView())
	};
#endif

	CachedBlendedPose = BlendedPose;
	LastEvaluatedFrameCounter = CurrentFrame;
	return BlendedPose;
}

// -------------------------------------------------------
// FComposableCameraEvaluationTreeNode
// -------------------------------------------------------

FComposableCameraPose FComposableCameraEvaluationTreeNode::Evaluate(float DeltaTime)
{
	return Visit([DeltaTime](auto& Node) -> FComposableCameraPose
	{
		return Node.Evaluate(DeltaTime);
	}, Wrapper);
}

bool FComposableCameraEvaluationTreeNode::IsLeaf() const
{
	return Wrapper.IsType<FComposableCameraEvaluationTreeLeafNodeWrapper>();
}

bool FComposableCameraEvaluationTreeNode::IsReferenceLeaf() const
{
	return Wrapper.IsType<FComposableCameraEvaluationTreeReferenceLeafNodeWrapper>();
}

bool FComposableCameraEvaluationTreeNode::IsInner() const
{
	return Wrapper.IsType<FComposableCameraEvaluationTreeInnerNodeWrapper>();
}

FComposableCameraEvaluationTreeReferenceLeafNodeWrapper& FComposableCameraEvaluationTreeNode::AsReferenceLeaf()
{
	return Wrapper.Get<FComposableCameraEvaluationTreeReferenceLeafNodeWrapper>();
}

const FComposableCameraEvaluationTreeReferenceLeafNodeWrapper& FComposableCameraEvaluationTreeNode::AsReferenceLeaf() const
{
	return Wrapper.Get<FComposableCameraEvaluationTreeReferenceLeafNodeWrapper>();
}

FComposableCameraEvaluationTreeLeafNodeWrapper& FComposableCameraEvaluationTreeNode::AsLeaf()
{
	return Wrapper.Get<FComposableCameraEvaluationTreeLeafNodeWrapper>();
}

const FComposableCameraEvaluationTreeLeafNodeWrapper& FComposableCameraEvaluationTreeNode::AsLeaf() const
{
	return Wrapper.Get<FComposableCameraEvaluationTreeLeafNodeWrapper>();
}

FComposableCameraEvaluationTreeInnerNodeWrapper& FComposableCameraEvaluationTreeNode::AsInner()
{
	return Wrapper.Get<FComposableCameraEvaluationTreeInnerNodeWrapper>();
}

const FComposableCameraEvaluationTreeInnerNodeWrapper& FComposableCameraEvaluationTreeNode::AsInner() const
{
	return Wrapper.Get<FComposableCameraEvaluationTreeInnerNodeWrapper>();
}

// -------------------------------------------------------
// UComposableCameraEvaluationTree
// -------------------------------------------------------

UComposableCameraEvaluationTree::UComposableCameraEvaluationTree(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FComposableCameraPose UComposableCameraEvaluationTree::Evaluate(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_CCS_EvaluationTree_Evaluate);
	TRACE_CPUPROFILER_EVENT_SCOPE(CCS_EvaluationTree_Evaluate);

	if (!RootNode)
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT("EvaluationTree has no root node. Has a camera been activated?"));
		return FComposableCameraPose{};
	}

	// Evaluate the tree to get the final blended pose.
	FComposableCameraPose ResultPose = RootNode->Evaluate(DeltaTime);

	// Post-evaluation: collapse any finished transitions.
	// This replaces inner nodes whose transition has finished with their right (target) subtree.
	RootNode = CollapseFinishedTransitions(RootNode);

	return ResultPose;
}

void UComposableCameraEvaluationTree::OnActivateNewCamera(
	AComposableCameraCameraBase* NewCamera,
	UComposableCameraTransitionBase* InTransition,
	bool bFreezeSourceCamera)
{
	check(NewCamera);

	// Create a leaf node for the new camera.
	TSharedPtr<FComposableCameraEvaluationTreeNode> NewLeaf = MakeShared<FComposableCameraEvaluationTreeNode>();
	NewLeaf->Wrapper.Set<FComposableCameraEvaluationTreeLeafNodeWrapper>(
		FComposableCameraEvaluationTreeLeafNodeWrapper{ NewCamera });

	if (!RootNode)
	{
		// First camera ever: just set it as root.
		RootNode = NewLeaf;
	}
	else if (!InTransition)
	{
		// Camera cut (no transition). If the root is an inter-context transition
		// (left child is a reference leaf), replace only the RIGHT subtree to
		// preserve the inter-context blend. Otherwise destroy the whole tree.
		if (RootNode->IsInner())
		{
			FComposableCameraEvaluationTreeInnerNodeWrapper& RootInner = RootNode->AsInner();
			if (RootInner.LeftNode && RootInner.LeftNode->IsReferenceLeaf())
			{
				// Hard cut within an inter-context blend: destroy the right
				// subtree's cameras and replace with the new leaf.
				DestroySubtreeCameras(RootInner.RightNode);
				RootInner.RightNode = NewLeaf;
				RunningCamera = NewCamera;
				return;
			}
		}

		// No inter-context root: full tree replacement (standard hard cut).
		DestroySubtreeCameras(RootNode);
		RootNode = NewLeaf;
	}
	else
	{
		// Check if the root is an inter-context transition (left child is a reference leaf).
		// If so, nest the new intra-context activation under the RIGHT subtree instead of
		// wrapping the entire tree. This preserves the inter-context blend at the root.
		//
		// Example: Gameplay --(inter-ctx)--> CamB1, then LS fires CamB2:
		//   Before:  [InterCtx] → RefLeaf | CamB1
		//   After:   [InterCtx] → RefLeaf | [IntraCtx] → CamB1 | CamB2
		//
		// When the inter-context transition finishes, the right subtree (intra-blend) survives.
		// When the intra-context transition finishes, it collapses to CamB2.
		if (RootNode->IsInner())
		{
			FComposableCameraEvaluationTreeInnerNodeWrapper& RootInner = RootNode->AsInner();
			if (RootInner.LeftNode && RootInner.LeftNode->IsReferenceLeaf())
			{
				// Nested activation: wrap the current right subtree with the new camera.
				TSharedPtr<FComposableCameraEvaluationTreeNode> CurrentRight = RootInner.RightNode;

				if (bFreezeSourceCamera && CurrentRight)
				{
					FreezeSubtree(CurrentRight, true);
				}

				FComposableCameraEvaluationTreeInnerNodeWrapper IntraWrapper;
				IntraWrapper.Transition = InTransition;
				IntraWrapper.LeftNode = CurrentRight;
				IntraWrapper.RightNode = NewLeaf;

				TSharedPtr<FComposableCameraEvaluationTreeNode> NewIntraNode = MakeShared<FComposableCameraEvaluationTreeNode>();
				NewIntraNode->Wrapper.Set<FComposableCameraEvaluationTreeInnerNodeWrapper>(MoveTemp(IntraWrapper));

				// Replace the right subtree with the new intra-context blend.
				RootInner.RightNode = NewIntraNode;

				RunningCamera = NewCamera;
				return;
			}
		}

		// Default path: no inter-context root, or root isn't an inner node.
		// Freeze the source subtree if requested — all leaves hold their last pose
		// and stop ticking for the duration of the transition.
		if (bFreezeSourceCamera)
		{
			FreezeSubtree(RootNode, true);
		}

		// Transition: create an inner node that blends from the current tree (source) to the new camera (target).
		FComposableCameraEvaluationTreeInnerNodeWrapper InnerWrapper;
		InnerWrapper.Transition = InTransition;
		InnerWrapper.LeftNode = RootNode;
		InnerWrapper.RightNode = NewLeaf;

		TSharedPtr<FComposableCameraEvaluationTreeNode> NewRoot = MakeShared<FComposableCameraEvaluationTreeNode>();
		NewRoot->Wrapper.Set<FComposableCameraEvaluationTreeInnerNodeWrapper>(MoveTemp(InnerWrapper));
		RootNode = NewRoot;
	}

	RunningCamera = NewCamera;
}

void UComposableCameraEvaluationTree::OnActivateNewCameraWithReferenceSource(
	AComposableCameraCameraBase* NewCamera,
	UComposableCameraTransitionBase* InTransition,
	UComposableCameraDirector* SourceDirector,
	bool bFreezeSourceCamera)
{
	check(NewCamera);
	check(SourceDirector);

	// Create a leaf node for the new camera.
	TSharedPtr<FComposableCameraEvaluationTreeNode> NewLeaf = MakeShared<FComposableCameraEvaluationTreeNode>();
	NewLeaf->Wrapper.Set<FComposableCameraEvaluationTreeLeafNodeWrapper>(
		FComposableCameraEvaluationTreeLeafNodeWrapper{ NewCamera });

	if (!InTransition)
	{
		// Camera cut — no blending needed. Just replace with the new leaf.
		DestroySubtreeCameras(RootNode);
		RootNode = NewLeaf;
	}
	else
	{
		// Capture a snapshot of the source director's current tree shape.
		// The RefLeaf evaluates THIS captured subtree (not the director's
		// live tree), so future mutations to the source director's root
		// — e.g. the source context later being popped and getting its own
		// RefLeaf→us installed at its root — don't feed back into our
		// evaluation. Shared-pointer capture keeps the subtree alive as
		// long as this RefLeaf holds it.
		FComposableCameraEvaluationTreeReferenceLeafNodeWrapper RefWrapper;
		RefWrapper.SnapshotRoot        = SourceDirector->GetEvaluationTree()
			? SourceDirector->GetEvaluationTree()->GetRootNode()
			: nullptr;
		RefWrapper.DebugSourceDirector = SourceDirector;
		RefWrapper.bFrozen             = bFreezeSourceCamera;
		if (bFreezeSourceCamera)
		{
			RefWrapper.CachedPose = SourceDirector->GetLastEvaluatedPose();
		}

		TSharedPtr<FComposableCameraEvaluationTreeNode> RefLeaf = MakeShared<FComposableCameraEvaluationTreeNode>();
		RefLeaf->Wrapper.Set<FComposableCameraEvaluationTreeReferenceLeafNodeWrapper>(MoveTemp(RefWrapper));

		// Build the inner node: reference leaf (source) → transition → new camera (target).
		FComposableCameraEvaluationTreeInnerNodeWrapper InnerWrapper;
		InnerWrapper.Transition = InTransition;
		InnerWrapper.LeftNode = RefLeaf;
		InnerWrapper.RightNode = NewLeaf;

		TSharedPtr<FComposableCameraEvaluationTreeNode> NewRoot = MakeShared<FComposableCameraEvaluationTreeNode>();
		NewRoot->Wrapper.Set<FComposableCameraEvaluationTreeInnerNodeWrapper>(MoveTemp(InnerWrapper));

		// Defer destruction of the old root. Scenario the deferral addresses:
		// SourceDirector was previously PUSHED on top of us via
		// `SourceDirector->ActivateNewCameraWithReferenceSource(…, this)` — at
		// that moment, SourceDirector's RefLeaf snapshotted OUR then-current
		// root (= `OldRoot` here) as a TSharedPtr. Destroying OldRoot's
		// cameras NOW would make SourceDirector's still-live Tick walk into
		// destroyed `Leaf.RunningCamera` actors during the T_ba blend. See
		// the `PendingDestroyOldRoots` comment for the full symptom list.
		//
		// Instead: stash OldRoot and destroy only once `InTransition`
		// finishes — at that point `CollapseFinishedTransitions` drops the
		// RefLeaf branch of our new root and OldRoot becomes unreachable
		// from this tree.
		TSharedPtr<FComposableCameraEvaluationTreeNode> OldRoot = RootNode;
		RootNode = NewRoot;

		if (OldRoot)
		{
			PendingDestroyOldRoots.Add(OldRoot);
			InTransition->OnTransitionFinishesDelegate.AddWeakLambda(this,
				[this, OldRoot]()
				{
					DestroySubtreeCameras(OldRoot);
					PendingDestroyOldRoots.Remove(OldRoot);
				});
		}
	}

	RunningCamera = NewCamera;
}

void UComposableCameraEvaluationTree::OnResumeCurrentTreeWithReferenceSource(
	UComposableCameraTransitionBase* InTransition,
	UComposableCameraDirector* SourceDirector,
	bool bFreezeSourceCamera)
{
	check(SourceDirector);

	// Pre-existing tree is required. Without RootNode there's nothing
	// to "resume" — caller should take the ActivateNew path instead.
	if (!RootNode)
	{
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("OnResumeCurrentTreeWithReferenceSource called with empty RootNode — nothing to resume. Did you mean OnActivateNewCameraWithReferenceSource?"));
		return;
	}

	if (!InTransition)
	{
		// Cut path: no transition needed. Our current RootNode stays as
		// the root — the resumed context keeps rendering its existing
		// camera as-is. SourceDirector is not referenced (no blend to
		// feed back from) and the popped context's cleanup is the
		// caller's responsibility.
		return;
	}

	// Capture the popped director's current tree as a snapshot. This is
	// the key to breaking the cycle that used to form during pop-while-
	// push: the popped director's tree may contain a RefLeaf back at us
	// (created during the original push), but THAT RefLeaf is also a
	// snapshot — it captured OUR tree's root from before this pop-wrap
	// (i.e. the raw leaf `RunningCamera` node). So the resulting DAG is:
	//
	//     A.new_root (this Inner)
	//       ├─ Left: RefLeaf ──► B.tree (captured at pop moment)
	//       │                       └─ push_Inner
	//       │                           ├─ Left: RefLeaf ──► A.OLD leaf
	//       │                           └─ Right: B leaf
	//       └─ Right: A.OLD leaf (same TSharedPtr as the innermost A ref)
	//
	// No self-reference back into A.new_root — two paths reach the A leaf
	// and Leaf-wrapper memoization ensures it's only ticked once.
	FComposableCameraEvaluationTreeReferenceLeafNodeWrapper RefWrapper;
	RefWrapper.SnapshotRoot        = SourceDirector->GetEvaluationTree()
		? SourceDirector->GetEvaluationTree()->GetRootNode()
		: nullptr;
	RefWrapper.DebugSourceDirector = SourceDirector;
	RefWrapper.bFrozen             = bFreezeSourceCamera;
	if (bFreezeSourceCamera)
	{
		RefWrapper.CachedPose = SourceDirector->GetLastEvaluatedPose();
	}

	TSharedPtr<FComposableCameraEvaluationTreeNode> RefLeaf = MakeShared<FComposableCameraEvaluationTreeNode>();
	RefLeaf->Wrapper.Set<FComposableCameraEvaluationTreeReferenceLeafNodeWrapper>(MoveTemp(RefWrapper));

	// KEY DIFFERENCE from OnActivateNewCameraWithReferenceSource: we do
	// NOT destroy the current tree's cameras. The current RootNode
	// (holding the resuming camera as a Leaf, or an already-transitioning
	// Inner) is preserved verbatim as the new transition's target. This
	// keeps the resuming camera ticking with its accumulated per-node
	// state — no "fresh instance snap" artifact on first post-pop tick.
	FComposableCameraEvaluationTreeInnerNodeWrapper InnerWrapper;
	InnerWrapper.Transition = InTransition;
	InnerWrapper.LeftNode   = RefLeaf;
	InnerWrapper.RightNode  = RootNode;   // preserved, NOT destroyed

	TSharedPtr<FComposableCameraEvaluationTreeNode> NewRoot = MakeShared<FComposableCameraEvaluationTreeNode>();
	NewRoot->Wrapper.Set<FComposableCameraEvaluationTreeInnerNodeWrapper>(MoveTemp(InnerWrapper));
	RootNode = NewRoot;

	// `RunningCamera` stays the same — the resuming camera was already
	// running and still is, just nested under a new transition.
}

bool UComposableCameraEvaluationTree::HasActiveCamera() const
{
	return RootNode.IsValid();
}

void UComposableCameraEvaluationTree::DestroyAll()
{
	// Flush any deferred subtrees whose wrapping transition never fired its
	// finish delegate (e.g. context torn down mid-blend, or the transition
	// replaced by another activation before it could complete).
	for (TSharedPtr<FComposableCameraEvaluationTreeNode>& Pending : PendingDestroyOldRoots)
	{
		DestroySubtreeCameras(Pending);
	}
	PendingDestroyOldRoots.Reset();

	DestroySubtreeCameras(RootNode);
	RootNode.Reset();
	RunningCamera = nullptr;
}

TSharedPtr<FComposableCameraEvaluationTreeNode> UComposableCameraEvaluationTree::CollapseFinishedTransitions(
	const TSharedPtr<FComposableCameraEvaluationTreeNode>& Node)
{
	if (!Node)
	{
		return nullptr;
	}

	// Leaf and reference leaf nodes need no collapsing.
	if (Node->IsLeaf() || Node->IsReferenceLeaf())
	{
		return Node;
	}

	// Transient cameras are only activated in separate contexts (managed by the context stack).
	// Within a single context, all camera transitions are explicit replacements.
	// So the evaluation tree only needs to handle the normal collapse case:
	// transition finished → promote target, destroy source.
	FComposableCameraEvaluationTreeInnerNodeWrapper& Inner = Node->AsInner();

	// Check if the transition is finished or missing, or the source was destroyed externally.
	bool bTransitionDone = !Inner.Transition || Inner.Transition->IsFinished();
	bool bSourceDestroyed = false;
	if (!bTransitionDone && Inner.LeftNode)
	{
		if (Inner.LeftNode->IsLeaf())
		{
			bSourceDestroyed = !IsValid(Inner.LeftNode->AsLeaf().RunningCamera.Get());
		}
		else if (Inner.LeftNode->IsReferenceLeaf())
		{
			// RefLeaf holds a TSharedPtr snapshot — null means nothing to
			// blend from. "Source destroyed" isn't a thing for a snapshot
			// in the live-director sense; the snapshot's owning directors
			// may still be alive. We only force-collapse when the capture
			// itself is empty (caller failed to seed the snapshot, or
			// passed a director that had no tree yet).
			bSourceDestroyed = !Inner.LeftNode->AsReferenceLeaf().SnapshotRoot.IsValid();
		}
	}

	// Transition finished or source destroyed: destroy the entire source subtree
	// and promote the target (right) subtree.
	if (bTransitionDone || bSourceDestroyed)
	{
		DestroySubtreeCameras(Inner.LeftNode);
		Inner.LeftNode.Reset();

		return CollapseFinishedTransitions(Inner.RightNode);
	}

	// Transition still active — collapse finished sub-transitions in both subtrees
	// so they don't linger while we keep this node alive.
	//
	// Left subtree collapse: e.g., D->B, E->B, B->A, C->A: when B finishes,
	// B collapses to E even while A is still blending.
	//
	// Right subtree collapse: with nested activation, the right child can be an
	// intra-context inner node (e.g., inter-ctx root's right child is CamB1->CamB2).
	// When CamB1->CamB2 finishes, it should collapse to CamB2 while the inter-ctx
	// transition is still active.
	Inner.LeftNode = CollapseFinishedTransitions(Inner.LeftNode);
	Inner.RightNode = CollapseFinishedTransitions(Inner.RightNode);

	return Node;
}

void UComposableCameraEvaluationTree::DestroySubtreeCameras(
	const TSharedPtr<FComposableCameraEvaluationTreeNode>& Node)
{
	if (!Node)
	{
		return;
	}

	if (Node->IsLeaf())
	{
		AComposableCameraCameraBase* Camera = Node->AsLeaf().RunningCamera.Get();
		if (IsValid(Camera))
		{
			Camera->Destroy();
		}
	}
	else if (Node->IsReferenceLeaf())
	{
		// Reference leaves don't own the cameras reachable through their
		// SnapshotRoot — those are owned by the source director that the
		// snapshot was captured from. Recursing here would destroy cameras
		// that still belong to a live director. Ownership is deliberately
		// asymmetric: RefLeaf keeps the SUBTREE struct alive via TSharedPtr,
		// but camera UObjects inside are destroyed only by their home
		// director's own DestroyAllCameras (typically fired from the
		// post-transition pending-destroy cleanup).
	}
	else if (Node->IsInner())
	{
		FComposableCameraEvaluationTreeInnerNodeWrapper& Inner = Node->AsInner();
		DestroySubtreeCameras(Inner.LeftNode);
		DestroySubtreeCameras(Inner.RightNode);
	}
}

void UComposableCameraEvaluationTree::FreezeSubtree(
	const TSharedPtr<FComposableCameraEvaluationTreeNode>& Node, bool bFrozen)
{
	if (!Node)
	{
		return;
	}

	if (Node->IsLeaf())
	{
		Node->AsLeaf().bFrozen = bFrozen;
	}
	else if (Node->IsReferenceLeaf())
	{
		Node->AsReferenceLeaf().bFrozen = bFrozen;
	}
	else if (Node->IsInner())
	{
		FComposableCameraEvaluationTreeInnerNodeWrapper& Inner = Node->AsInner();
		FreezeSubtree(Inner.LeftNode, bFrozen);
		FreezeSubtree(Inner.RightNode, bFrozen);
	}
}

void UComposableCameraEvaluationTree::BuildDebugSnapshot(TArray<FComposableCameraTreeNodeSnapshot>& OutNodes) const
{
	if (!RootNode)
	{
		return;
	}

	// Determine the dominant leaf: walk root → always follow the Right child
	// through InnerTransition nodes until we hit a (non-inner) terminal. This
	// is the node that would remain if every in-flight transition collapsed.
	const FComposableCameraEvaluationTreeNode* Dominant = RootNode.Get();
	while (Dominant && Dominant->IsInner())
	{
		const auto& Inner = Dominant->AsInner();
		if (!Inner.RightNode) { break; }
		Dominant = Inner.RightNode.Get();
	}

	// Root has no siblings → bIsLastSibling = true trivially; no ancestors → mask 0.
	BuildNodeDebugSnapshot(RootNode, /*Depth=*/0, /*bIsLastSibling=*/true, /*AncestorMask=*/0u, Dominant, OutNodes);
}

void UComposableCameraEvaluationTree::BuildNodeDebugSnapshot(
	const TSharedPtr<FComposableCameraEvaluationTreeNode>& Node,
	int32 Depth,
	bool bIsLastSibling,
	uint32 AncestorLastFlagsBitmask,
	const FComposableCameraEvaluationTreeNode* DominantNodePtr,
	TArray<FComposableCameraTreeNodeSnapshot>& OutNodes,
	bool bInReferencedSubtree)
{
	if (!Node)
	{
		return;
	}

	FComposableCameraTreeNodeSnapshot Entry;
	Entry.Depth = Depth;
	Entry.bIsLastSibling = bIsLastSibling;
	Entry.AncestorLastFlagsBitmask = AncestorLastFlagsBitmask;
	Entry.bInReferencedSubtree = bInReferencedSubtree;

	if (Node->IsLeaf())
	{
		Entry.Kind = EComposableCameraTreeNodeKind::Leaf;

		const auto& Leaf = Node->AsLeaf();
		const AComposableCameraCameraBase* Camera = Leaf.RunningCamera.Get();
		if (IsValid(Camera))
		{
			if (const UComposableCameraTypeAsset* TA = Camera->SourceTypeAsset.Get())
			{
				Entry.DisplayLabel = TA->GetName();
			}
			else if (Camera->CameraTag.IsValid())
			{
				Entry.DisplayLabel = Camera->CameraTag.ToString();
			}
			else
			{
				Entry.DisplayLabel = Camera->GetName();
			}
			if (Camera->IsTransient())
			{
				Entry.bIsTransient = true;
				Entry.LifeTotal   = Camera->GetLifeTime();
				Entry.LifeElapsed = Entry.LifeTotal - Camera->GetRemainingLifeTime();
			}
		}
		else
		{
			Entry.bDestroyed = true;
			Entry.DisplayLabel = TEXT("(destroyed)");
		}
		Entry.bIsDominantLeaf = (Node.Get() == DominantNodePtr);
		OutNodes.Add(MoveTemp(Entry));
	}
	else if (Node->IsReferenceLeaf())
	{
		Entry.Kind = EComposableCameraTreeNodeKind::ReferenceLeaf;
		const auto& RefLeaf = Node->AsReferenceLeaf();
		const bool bHasSnapshot = RefLeaf.SnapshotRoot.IsValid();
		if (bHasSnapshot)
		{
			Entry.DisplayLabel = RefLeaf.DebugSourceDirector.IsValid()
				? RefLeaf.DebugSourceDirector->GetName()
				: TEXT("(director gone)");
		}
		else
		{
			Entry.bDestroyed = true;
			Entry.DisplayLabel = TEXT("(empty snapshot)");
		}
		// A reference leaf can also be the dominant terminal when the root is
		// itself a reference leaf (no transitions yet). Record that — but
		// dominance applies ONLY to the outer (active) tree; nested referenced
		// subtrees do not participate in the outer collapse chain, so
		// downstream recursions pass DominantPtr = nullptr.
		Entry.bIsDominantLeaf = !bInReferencedSubtree && (Node.Get() == DominantNodePtr);
		OutNodes.Add(MoveTemp(Entry));

		// Inline-expand the referenced subtree under this RefLeaf. Child depth
		// is +1; the child has no siblings (RefLeaf is a 1-child visual parent),
		// so bIsLastSibling = true. The ancestor mask picks up the RefLeaf's own
		// last-sibling bit so the renderer draws the continuation stem correctly.
		// Flattening nested RefLeaves recursively is fine: `bInReferencedSubtree`
		// stays true all the way down, so every descendant carries the tint flag.
		if (bHasSnapshot)
		{
			uint32 ChildMask = AncestorLastFlagsBitmask;
			if (bIsLastSibling && Depth < 32)
			{
				ChildMask |= (1u << Depth);
			}
			const int32 SubtreeRootIdx = OutNodes.Num();
			BuildNodeDebugSnapshot(
				RefLeaf.SnapshotRoot,
				Depth + 1,
				/*bIsLastSibling=*/true,
				ChildMask,
				/*DominantNodePtr=*/nullptr,
				OutNodes,
				/*bInReferencedSubtree=*/true);
			if (OutNodes.IsValidIndex(SubtreeRootIdx))
			{
				OutNodes[SubtreeRootIdx].bIsReferencedRoot = true;
			}
		}
	}
	else if (Node->IsInner())
	{
		Entry.Kind = EComposableCameraTreeNodeKind::InnerTransition;
		const auto& Inner = Node->AsInner();
		if (Inner.Transition)
		{
			Entry.DisplayLabel       = Inner.Transition->GetClass()->GetName();
			Entry.TransitionProgress = Inner.Transition->GetPercentage();
			Entry.TransitionTotal    = Inner.Transition->GetTransitionTime();
			Entry.TransitionElapsed  = Entry.TransitionTotal - Inner.Transition->GetRemainingTime();

			// Sample the timing curve so the panel can render a sparkline.
			// 24 segments → 25 sample values — enough for Ease-In-Out
			// shoulders and Smoother curves to read distinctly while
			// keeping the per-frame allocation trivial. Pure math call on
			// the transition's authored UPROPERTYs only, no state reads.
			constexpr int32 CurveSamples = 24;
			Entry.BlendCurveSamples.Reserve(CurveSamples + 1);
			for (int32 i = 0; i <= CurveSamples; ++i)
			{
				const float NormalizedT = static_cast<float>(i) / static_cast<float>(CurveSamples);
				Entry.BlendCurveSamples.Add(Inner.Transition->GetBlendWeightAt(NormalizedT));
			}
		}
		else
		{
			Entry.bDestroyed = true;
			Entry.DisplayLabel = TEXT("(null transition)");
		}
		OutNodes.Add(MoveTemp(Entry));

		// Compute the ancestor mask to pass to children. Children's ancestors
		// include self at depth Depth, so set bit Depth if self is last sibling.
		// Guard against overflow for pathological deep trees (>32 levels).
		uint32 NewMask = AncestorLastFlagsBitmask;
		if (bIsLastSibling && Depth < 32)
		{
			NewMask |= (1u << Depth);
		}

		// Recurse: Left (source) first, then Right (target) — matches the
		// "Source:" / "Target:" ordering in the string builder and keeps
		// DFS pre-order stable for the renderer. Left is never last sibling;
		// Right always is (Inner has exactly two children). `bInReferencedSubtree`
		// is threaded through so the tint flag is preserved when we recurse
		// inside a referenced source tree that has its own inner transitions.
		BuildNodeDebugSnapshot(Inner.LeftNode,  Depth + 1, /*bIsLastSibling=*/false, NewMask, DominantNodePtr, OutNodes, bInReferencedSubtree);
		BuildNodeDebugSnapshot(Inner.RightNode, Depth + 1, /*bIsLastSibling=*/true,  NewMask, DominantNodePtr, OutNodes, bInReferencedSubtree);
	}
}

#if !UE_BUILD_SHIPPING
void UComposableCameraEvaluationTree::DrawTransitionsDebug(UWorld* World, bool bViewerIsOutsideCamera) const
{
	// Track snapshot subtree nodes already walked on this call. With the
	// new RefLeaf snapshot semantics the whole reachable graph is a DAG
	// (no Director cross-references), but two RefLeaves can still share
	// the same SnapshotRoot — and any Inner inside a snapshot could be
	// reachable via two paths through the DAG. Deduplicate on node
	// identity so we don't redraw the same transition twice per frame.
	TSet<const FComposableCameraEvaluationTreeNode*> VisitedNodes;
	DrawTransitionsNodeDebug(RootNode, World, bViewerIsOutsideCamera, VisitedNodes);
}

void UComposableCameraEvaluationTree::DrawTransitionsNodeDebug(
	const TSharedPtr<FComposableCameraEvaluationTreeNode>& Node,
	UWorld* World,
	bool bViewerIsOutsideCamera,
	TSet<const FComposableCameraEvaluationTreeNode*>& VisitedNodes)
{
	if (!Node)
	{
		return;
	}

	// DAG deduplication: a node reachable via two paths should draw only
	// once. Evaluation-time memoization handles this transparently; the
	// draw path has to be explicit because it has observable side effects.
	bool bAlreadyInSet = false;
	VisitedNodes.Add(Node.Get(), &bAlreadyInSet);
	if (bAlreadyInSet)
	{
		return;
	}

	if (Node->IsInner())
	{
		const FComposableCameraEvaluationTreeInnerNodeWrapper& Inner = Node->AsInner();
		if (Inner.Transition)
		{
			// Transition self-gates on its own CVar — we always invoke the
			// virtual; the override returns immediately if its CVar is off.
			Inner.Transition->DrawTransitionDebug(World, bViewerIsOutsideCamera);
		}
		DrawTransitionsNodeDebug(Inner.LeftNode,  World, bViewerIsOutsideCamera, VisitedNodes);
		DrawTransitionsNodeDebug(Inner.RightNode, World, bViewerIsOutsideCamera, VisitedNodes);
	}
	else if (Node->IsReferenceLeaf())
	{
		// Descend into the snapshot so any intra-blend transitions captured
		// inside the RefLeaf's subtree still participate in debug draw.
		// Snapshot is a TSharedPtr — no cross-director traversal needed.
		const FComposableCameraEvaluationTreeReferenceLeafNodeWrapper& RefLeaf = Node->AsReferenceLeaf();
		DrawTransitionsNodeDebug(RefLeaf.SnapshotRoot, World, bViewerIsOutsideCamera, VisitedNodes);
	}
	// Leaf: nothing to do — leaves don't hold transitions.
}
#endif // !UE_BUILD_SHIPPING

void UComposableCameraEvaluationTree::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UComposableCameraEvaluationTree* This = CastChecked<UComposableCameraEvaluationTree>(InThis);
	AddTreeReferencedObjects(This->RootNode, Collector);
	// Deferred-destruction subtrees must stay GC-alive until we explicitly
	// destroy them — otherwise the actor gets collected out from under the
	// `TObjectPtr` in the Leaf wrapper and our `DestroySubtreeCameras` call
	// at transition finish becomes a no-op on an already-collected pointer.
	for (const TSharedPtr<FComposableCameraEvaluationTreeNode>& Pending : This->PendingDestroyOldRoots)
	{
		AddTreeReferencedObjects(Pending, Collector);
	}
	Super::AddReferencedObjects(InThis, Collector);
}

void UComposableCameraEvaluationTree::AddTreeReferencedObjects(
	const TSharedPtr<FComposableCameraEvaluationTreeNode>& Node,
	FReferenceCollector& Collector)
{
	if (!Node)
	{
		return;
	}

	if (Node->IsLeaf())
	{
		FComposableCameraEvaluationTreeLeafNodeWrapper& Leaf = Node->AsLeaf();
		Collector.AddReferencedObject(Leaf.RunningCamera);
	}
	else if (Node->IsReferenceLeaf())
	{
		FComposableCameraEvaluationTreeReferenceLeafNodeWrapper& RefLeaf = Node->AsReferenceLeaf();
		// RefLeaf owns cameras / transitions indirectly through its
		// captured SnapshotRoot subtree — we must recurse so everything
		// reachable from the snapshot stays GC-pinned, even if the
		// originating director has already been popped off the stack.
		// DebugSourceDirector is a weak ref and isn't collected here.
		AddTreeReferencedObjects(RefLeaf.SnapshotRoot, Collector);
	}
	else if (Node->IsInner())
	{
		FComposableCameraEvaluationTreeInnerNodeWrapper& Inner = Node->AsInner();
		Collector.AddReferencedObject(Inner.Transition);
		AddTreeReferencedObjects(Inner.LeftNode, Collector);
		AddTreeReferencedObjects(Inner.RightNode, Collector);
	}
}
