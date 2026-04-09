// Copyright Sulley. All rights reserved.

#include "Core/ComposableCameraEvaluationTree.h"

#include "ComposableCameraSystemModule.h"
#include "Core/ComposableCameraDirector.h"
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

	return RunningCamera->TickCamera(DeltaTime);
}

// -------------------------------------------------------
// FComposableCameraEvaluationTreeReferenceLeafNodeWrapper
// -------------------------------------------------------

FComposableCameraPose FComposableCameraEvaluationTreeReferenceLeafNodeWrapper::Evaluate(float DeltaTime)
{
	if (!IsValid(ReferencedDirector))
	{
		UE_LOG(LogComposableCameraSystem, Error, TEXT("ReferencedDirector is null or destroyed when evaluating reference leaf node."));
		return FComposableCameraPose{};
	}

	// Evaluate the source context's Director live. This keeps the source context ticking
	// during inter-context transitions rather than freezing it.
	return ReferencedDirector->Evaluate(DeltaTime);
}

// -------------------------------------------------------
// FComposableCameraEvaluationTreeInnerNodeWrapper
// -------------------------------------------------------

FComposableCameraPose FComposableCameraEvaluationTreeInnerNodeWrapper::Evaluate(float DeltaTime)
{
	if (!RightNode)
	{
		UE_LOG(LogComposableCameraSystem, Error, TEXT("RightNode is null in inner evaluation node."));
		return FComposableCameraPose{};
	}

	// Always evaluate the target (right) subtree.
	const FComposableCameraPose TargetPose = RightNode->Evaluate(DeltaTime);

	// Evaluate the source (left) subtree.
	if (!LeftNode)
	{
		return TargetPose;
	}
	const FComposableCameraPose SourcePose = LeftNode->Evaluate(DeltaTime);

	if (!Transition)
	{
		return TargetPose;
	}

	// Blend source and target using the transition.
	return Transition->Evaluate(DeltaTime, SourcePose, TargetPose);
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
	UComposableCameraTransitionBase* InTransition)
{
	check(NewCamera);

	// Create a leaf node for the new camera.
	TSharedPtr<FComposableCameraEvaluationTreeNode> NewLeaf = MakeShared<FComposableCameraEvaluationTreeNode>();
	NewLeaf->Wrapper.Set<FComposableCameraEvaluationTreeLeafNodeWrapper>(
		FComposableCameraEvaluationTreeLeafNodeWrapper{ NewCamera });

	if (!InTransition || !RootNode)
	{
		// Camera cut (no transition) or first camera: destroy old tree cameras, then replace with the new leaf.
		DestroySubtreeCameras(RootNode);
		RootNode = NewLeaf;
	}
	else
	{
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
	UComposableCameraDirector* SourceDirector)
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
		// Create a reference leaf that evaluates the source context's Director live.
		TSharedPtr<FComposableCameraEvaluationTreeNode> RefLeaf = MakeShared<FComposableCameraEvaluationTreeNode>();
		RefLeaf->Wrapper.Set<FComposableCameraEvaluationTreeReferenceLeafNodeWrapper>(
			FComposableCameraEvaluationTreeReferenceLeafNodeWrapper{ SourceDirector });

		// Build the inner node: reference leaf (source) → transition → new camera (target).
		FComposableCameraEvaluationTreeInnerNodeWrapper InnerWrapper;
		InnerWrapper.Transition = InTransition;
		InnerWrapper.LeftNode = RefLeaf;
		InnerWrapper.RightNode = NewLeaf;

		TSharedPtr<FComposableCameraEvaluationTreeNode> NewRoot = MakeShared<FComposableCameraEvaluationTreeNode>();
		NewRoot->Wrapper.Set<FComposableCameraEvaluationTreeInnerNodeWrapper>(MoveTemp(InnerWrapper));

		// Destroy existing cameras in the current tree before replacing it.
		DestroySubtreeCameras(RootNode);
		RootNode = NewRoot;
	}

	RunningCamera = NewCamera;
}

bool UComposableCameraEvaluationTree::HasActiveCamera() const
{
	return RootNode.IsValid();
}

void UComposableCameraEvaluationTree::DestroyAll()
{
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
			bSourceDestroyed = !IsValid(Inner.LeftNode->AsReferenceLeaf().ReferencedDirector.Get());
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

	// Transition still active — collapse finished sub-transitions in the source chain
	// so they don't linger while we keep this node alive.
	// e.g., D->B, E->B, B->A, C->A: when B finishes, B collapses to E even while A is still blending.
	Inner.LeftNode = CollapseFinishedTransitions(Inner.LeftNode);

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
		// Reference leaves don't own cameras — nothing to destroy.
	}
	else if (Node->IsInner())
	{
		FComposableCameraEvaluationTreeInnerNodeWrapper& Inner = Node->AsInner();
		DestroySubtreeCameras(Inner.LeftNode);
		DestroySubtreeCameras(Inner.RightNode);
	}
}

void UComposableCameraEvaluationTree::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UComposableCameraEvaluationTree* This = CastChecked<UComposableCameraEvaluationTree>(InThis);
	AddTreeReferencedObjects(This->RootNode, Collector);
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
		Collector.AddReferencedObject(RefLeaf.ReferencedDirector);
	}
	else if (Node->IsInner())
	{
		FComposableCameraEvaluationTreeInnerNodeWrapper& Inner = Node->AsInner();
		Collector.AddReferencedObject(Inner.Transition);
		AddTreeReferencedObjects(Inner.LeftNode, Collector);
		AddTreeReferencedObjects(Inner.RightNode, Collector);
	}
}