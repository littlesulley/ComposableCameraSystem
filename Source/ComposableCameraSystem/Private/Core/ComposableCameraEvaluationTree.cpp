// Copyright Sulley. All rights reserved.

#include "Core/ComposableCameraEvaluationTree.h"
#include "Transitions/ComposableCameraTransitionBase.h"

FComposableCameraPose UComposableCameraEvaluationTree::Evaluate(float DeltaTime)
{
	FComposableCameraPose CurrentPose;

	if (!RunningCamera)
	{
		return CurrentPose;
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
