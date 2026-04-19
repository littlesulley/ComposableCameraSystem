// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "UObject/Object.h"
#include "ComposableCameraEvaluationTree.generated.h"

class UComposableCameraTransitionBase;
class AComposableCameraCameraBase;
class UComposableCameraDirector;

// Forward declaration: inner nodes hold TSharedPtr to tree nodes.
struct FComposableCameraEvaluationTreeNode;

/**
 * Leaf node wrapper: wraps a single running camera.
 */
struct FComposableCameraEvaluationTreeLeafNodeWrapper
{
	TObjectPtr<AComposableCameraCameraBase> RunningCamera { nullptr };

	/** When true, Evaluate returns the camera's cached pose without ticking it. */
	bool bFrozen { false };

	FComposableCameraPose Evaluate(float DeltaTime);
};

/**
 * Reference leaf node wrapper: a lightweight leaf that evaluates another context's Director
 * rather than owning a camera. Used for inter-context transitions so that the source context
 * continues to tick live while the target context blends in.
 *
 * This node does NOT own any cameras — it just forwards evaluation to the referenced Director.
 * When the transition collapses, this node is simply discarded (no cameras to destroy).
 */
struct FComposableCameraEvaluationTreeReferenceLeafNodeWrapper
{
	/** The Director from another context that this node evaluates. */
	TObjectPtr<UComposableCameraDirector> ReferencedDirector { nullptr };

	/** When true, Evaluate returns the Director's last evaluated pose without re-evaluating it. */
	bool bFrozen { false };

	FComposableCameraPose Evaluate(float DeltaTime);
};

/**
 * Inner node wrapper: wraps a transition that blends between a source (left) and target (right) subtree.
 * Owns its child nodes via shared pointers.
 */
struct FComposableCameraEvaluationTreeInnerNodeWrapper
{
	TObjectPtr<UComposableCameraTransitionBase> Transition { nullptr };
	TSharedPtr<FComposableCameraEvaluationTreeNode> LeftNode;
	TSharedPtr<FComposableCameraEvaluationTreeNode> RightNode;

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
	 * only the right subtree survives — which is the intra-context blend, exactly as expected.
	 */
	void OnActivateNewCamera(AComposableCameraCameraBase* NewCamera, UComposableCameraTransitionBase* Transition, bool bFreezeSourceCamera = false);

	/**
	 * Activate a new camera with a reference to another context's Director as the transition source.
	 * Used for inter-context transitions: the reference leaf evaluates the source context live
	 * (not frozen), producing smooth blending between contexts.
	 *
	 * @param NewCamera The new camera to activate in this context.
	 * @param Transition The transition to blend from the referenced Director's output to NewCamera.
	 * @param SourceDirector The Director from the source context to reference as the left (source) side.
	 */
	void OnActivateNewCameraWithReferenceSource(
		AComposableCameraCameraBase* NewCamera,
		UComposableCameraTransitionBase* Transition,
		UComposableCameraDirector* SourceDirector,
		bool bFreezeSourceCamera = false);

	/** Returns true if the tree has at least one active camera. */
	bool HasActiveCamera() const;

	/** Get the current running camera (set when a camera is activated, updated on tree rebuild). */
	AComposableCameraCameraBase* GetRunningCamera() const { return RunningCamera; }

	/** Destroy all cameras in the tree and reset to empty. */
	void DestroyAll();

	/** Build a debug string representation of the evaluation tree structure. */
	void BuildDebugString(TStringBuilder<1024>& OutString, int32 IndentLevel = 0) const;

	// UObject interface.
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

private:
	/** Root of the evaluation tree. Null if no camera has been activated yet. */
	TSharedPtr<FComposableCameraEvaluationTreeNode> RootNode;

	/** Currently running (target) camera. */
	UPROPERTY(Transient)
	TObjectPtr<AComposableCameraCameraBase> RunningCamera;

	/**
	 * Collapse finished transitions in the tree.
	 *
	 * When an inner node's transition is finished (or its source is destroyed),
	 * the node is replaced by its right (target) subtree and the left (source)
	 * subtree's cameras are destroyed.
	 *
	 * Transient cameras are not managed here — they live in separate contexts
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
	 * Used when bFreezeSourceCamera is set on activation — the entire outgoing
	 * blend tree holds its last pose during the transition.
	 */
	static void FreezeSubtree(const TSharedPtr<FComposableCameraEvaluationTreeNode>& Node, bool bFrozen);

	/** Recursively build a debug string for a subtree. */
	static void BuildNodeDebugString(
		const TSharedPtr<FComposableCameraEvaluationTreeNode>& Node,
		TStringBuilder<1024>& OutString,
		int32 IndentLevel);

	/** Recursively register UObject references in the tree for garbage collection. */
	static void AddTreeReferencedObjects(
		const TSharedPtr<FComposableCameraEvaluationTreeNode>& Node,
		FReferenceCollector& Collector);
};
