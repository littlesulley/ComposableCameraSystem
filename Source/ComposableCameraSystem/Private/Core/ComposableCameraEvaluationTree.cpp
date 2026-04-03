// Copyright Sulley. All rights reserved.

#include "Core/ComposableCameraEvaluationTree.h"

#include "ComposableCameraSystemModule.h"
#include "InteractiveToolManager.h"
#include "Transitions/ComposableCameraTransitionBase.h"
#include "Utils/ComposableCameraBlueprintLibrary.h"

FComposableCameraPose FComposableCameraEvaluationTreeLeafNodeWrapper::Evaluate(float DeltaTime)
{
	if (!RunningCamera)
	{
		UE_LOG(LogComposableCameraSystem, Error, TEXT("RunningCamera is null when evaluating FComposableCameraEvaluationTreeLeafNode."));
		return FComposableCameraPose{};
	}

	return RunningCamera->TickCamera(DeltaTime);
}

FComposableCameraPose FComposableCameraEvaluationTreeInnerNodeWrapper::Evaluate(float DeltaTime)
{
	if (!&LeftNode || !&RightNode)
	{
		UE_LOG(LogComposableCameraSystem, Error, TEXT("LeftNode or RightNode in FComposableCameraEvaluationTreeInnerNodeWrapper is not valid."));
		return FComposableCameraPose{};
	}
	
	const FComposableCameraPose TargetPose = RightNode.Evaluate(DeltaTime);
	
	if (!Transition)
	{
		return TargetPose;
	}
	
	const FComposableCameraPose SourcePose = bFreezePreviousCamera ? FreezedCameraPose : LeftNode.Evaluate(DeltaTime);
	const FComposableCameraPose CurrentPose = Transition->EvaluateBySource(DeltaTime, SourcePose, TargetPose);
	
	if (Transition->IsFinished())
	{
		Transition = nullptr;
	}
	
	return CurrentPose;	
}

FComposableCameraPose FComposableCameraEvaluationTreeNode::Evaluate(float DeltaTime)
{
	return Visit([=](auto& Node)
	{
		return Node.Evaluate(DeltaTime);
	}, Wrapper);
}

UComposableCameraEvaluationTree::UComposableCameraEvaluationTree(const FObjectInitializer& ObjectInitializer)
{
	EvaluationTree.Reserve(16);
}

FComposableCameraPose UComposableCameraEvaluationTree::Evaluate(float DeltaTime)
{
	auto& RootNode = GetRootNode();
	FComposableCameraPose ResultPose = Visit(
		[DeltaTime](const auto& RootNode) -> FComposableCameraPose
		{
			return RootNode.Evaluate(DeltaTime);
		},
		RootNode);

	return ResultPose;

	FComposableCameraPose CurrentPose;

	if (RunningCamera->IsFinished())
	{
		EComposableCameraResumeCameraTransformSchema ResumeTransformSchema
			= RunningCamera->bDefaultPreserveCameraPose
			? EComposableCameraResumeCameraTransformSchema::PreserveCurrent
			: EComposableCameraResumeCameraTransformSchema::PreserveResumed;
		
		UComposableCameraBlueprintLibrary::TerminateCurrentCamera(
			this, RunningCamera->GetOwningPlayerCameraManager(), nullptr, ResumeTransformSchema, FTransform{}, false);
	}
	
	if (Transition)
	{
		CurrentPose = Transition->Evaluate(
			DeltaTime, RunningCamera->TickCamera(DeltaTime));

		if (Transition->IsFinished())
		{
			Transition = nullptr;
		}
	}
	else
	{
		CurrentPose = RunningCamera->TickCamera(DeltaTime);
	}

	return CurrentPose;
}

void UComposableCameraEvaluationTree::OnActivateNewCamera(AComposableCameraCameraBase* NewCamera, UComposableCameraTransitionBase* InTransition)
{
	EvaluationTree.
	RunningCamera = NewCamera;
	Transition = InTransition;
}

FComposableCameraEvaluationTreeNode& UComposableCameraEvaluationTree::GetRootNode()
{
	checkf(EvaluationTree.Num() > 0, TEXT("EvaluationTree is empty!"));
	return EvaluationTree[0];
}

const FComposableCameraEvaluationTreeNode& UComposableCameraEvaluationTree::GetRootNode() const
{
	checkf(EvaluationTree.Num() > 0, TEXT("EvaluationTree is empty!"));
	return EvaluationTree[0];
}
