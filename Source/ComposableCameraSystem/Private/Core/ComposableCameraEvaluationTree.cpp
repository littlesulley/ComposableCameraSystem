// Copyright Sulley. All rights reserved.

#include "Core/ComposableCameraEvaluationTree.h"

#include "ComposableCameraSystemModule.h"
#include "Transitions/ComposableCameraTransitionBase.h"
#include "Utils/ComposableCameraBlueprintLibrary.h"

FComposableCameraPose FComposableCameraEvaluationTreeLeafNode::Evaluate(float DeltaTime)
{
	if (!RunningCamera)
	{
		UE_LOG(LogComposableCameraSystem, Error, TEXT("RunningCamera is null when evaluating FComposableCameraEvaluationTreeLeafNode."));
		return FComposableCameraPose{};
	}

	return RunningCamera->TickCamera(DeltaTime);
}

FComposableCameraPose FComposableCameraEvaluationTreeInnerNode::Evaluate(float DeltaTime)
{
	
}

UComposableCameraEvaluationTree::UComposableCameraEvaluationTree(const FObjectInitializer& ObjectInitializer)
{
	EvaluationTree.Reserve(16);
}

FComposableCameraPose UComposableCameraEvaluationTree::Evaluate(float DeltaTime)
{
	Node& RootNode = GetRootNode();
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
	RunningCamera = NewCamera;
	Transition = InTransition;
}

UComposableCameraEvaluationTree::Node& UComposableCameraEvaluationTree::GetRootNode()
{
	checkf(EvaluationTree.Num() > 0, TEXT("EvaluationTree is empty!"));
	return EvaluationTree[0];
}
