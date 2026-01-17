// Copyright Sulley. All rights reserved.

#include "Transitions/ComposableCameraPathGuidedTransition.h"
#include "Transitions/ComposableCameraInertializedTransition.h"
#include "CameraRig_Rail.h"
#include "ComposableCameraSystemModule.h"
#include "Core/ComposableCameraPlayerCamaraManager.h"
#include "Nodes/ComposableCameraSplineNode.h"

void UComposableCameraPathGuidedTransition::OnBeginPlay_Implementation(float DeltaTime,
                                                                       const FComposableCameraPose& CurrentTargetPose)
{
	if (DrivingTransition)
	{
		DrivingTransition->TransitionEnabled(SourceCamera, TargetCamera, StartCameraPose);
		DrivingTransition->SetTransitionTime(TransitionTime);
		DrivingTransition->ResetTransitionState();
	}
	
	if (RailActor.IsValid())
	{
		Rail = RailActor.Get();
	}

	// IntermediateCamera
	IntermediateCamera = GetWorld()->SpawnActorDeferred<AComposableCameraCameraBase>(AComposableCameraCameraBase::StaticClass(), FTransform{});
	IntermediateCamera->bIsRunning = true;
	IntermediateCamera->bIsTransient = false;
	IntermediateCamera->LifeTime = -1.f;
	IntermediateCamera->RemainingLifeTime = -1.f;
	IntermediateCamera->Initialize(SourceCamera->GetOwningPlayerCameraManager(), nullptr);
	
	UComposableCameraSplineNode* SplineNode = NewObject<UComposableCameraSplineNode>(IntermediateCamera, UComposableCameraSplineNode::StaticClass());
	SplineNode->SplineType = EComposableCameraSplineNodeSplineType::BuiltInSpline;
	SplineNode->Rail = Rail;
	SplineNode->MoveMethod = EComposableCameraSplineNodeMoveMethod::Automatic;
	SplineNode->AutomaticMoveCurve = SplineMoveCurve;
	SplineNode->Duration = TransitionTime;
	IntermediateCamera->CameraNodes.Add(SplineNode);
	IntermediateCamera->FinishSpawning(FTransform{});
	IntermediateCamera->Rename(TEXT("PathGuidedTransition_IntermediateCameraOnSpline"));
#if WITH_EDITOR
	IntermediateCamera->SetActorLabel(
		TEXT("PathGuidedTransition_IntermediateCameraOnSpline"),
		false
	);
#endif
}

FComposableCameraPose UComposableCameraPathGuidedTransition::OnEvaluate_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentTargetPose)
{
	if (!DrivingTransition)
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT("DrivingTransition is not valid in ComposableCameraPathGuidedTransition."));
		return CurrentTargetPose;
	}
	if (!Rail)
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT("SplineActor is not valid in ComposableCameraPathGuidedTransition."));
		return CurrentTargetPose;
	}
	
	float DurationPct = (GetTransitionTime() - GetRemainingTime()) / GetTransitionTime();

	// Base pose.
	FComposableCameraPose BasePose = DrivingTransition->Evaluate(DeltaTime, CurrentTargetPose);
	FComposableCameraPose SplinePose = IntermediateCamera->TickCamera(DeltaTime);
	FComposableCameraPose ResultPose = BasePose;

	if (DurationPct < GuideRange.X)
	{
		if (!EnterTransition)
		{
			EnterTransition = NewObject<UComposableCameraInertializedTransition>(this, UComposableCameraInertializedTransition::StaticClass());
			EnterTransition->TransitionEnabled(SourceCamera, IntermediateCamera, SplinePose);
			EnterTransition->SetTransitionTime(GuideRange.X * TransitionTime);
			EnterTransition->ResetTransitionState();
		}
		
		FComposableCameraPose EnterPose = EnterTransition->Evaluate(DeltaTime, SplinePose);
		ResultPose.Position = EnterPose.Position;
	}
	else if (DurationPct <= GuideRange.Y)
	{
		ResultPose.Position = SplinePose.Position;
	}
	else
	{
		if (!ExitTransition)
		{
			ExitTransition = NewObject<UComposableCameraInertializedTransition>(this, UComposableCameraInertializedTransition::StaticClass());
			ExitTransition->TransitionEnabled(IntermediateCamera, TargetCamera, CurrentTargetPose);
			ExitTransition->SetTransitionTime(GetTransitionTime() * (1.f - GuideRange.Y));
			ExitTransition->ResetTransitionState();
			OnTransitionFinishesDelegate.AddLambda(
				[InCamera = IntermediateCamera]()
				{
					if (InCamera)
					{
						InCamera->Destroy();
					}
				});
		}
		
		FComposableCameraPose ExitPose = ExitTransition->Evaluate(DeltaTime, CurrentTargetPose /*BasePose*/);
		ResultPose.Position = ExitPose.Position;
	}
	
	// Draw debug spline points.
	if (TargetCamera && TargetCamera->GetOwningPlayerCameraManager())
	{
		if (TargetCamera->GetOwningPlayerCameraManager()->bDrawDebugInformation)
		{
			DrawDebugSplinePoints(TArray<FVector>{ ResultPose.Position });
		}
	}

	PreviousResultPose = ResultPose;
	return ResultPose;
}

void UComposableCameraPathGuidedTransition::DrawDebugSplinePoints(const TArray<FVector>& SplinePoints)
{
	for (const FVector& Point : SplinePoints)
	{
		DrawDebugPoint(GetWorld(), Point, 8.f, FColor::Cyan, false, 2.f, 1.f);
	}
}
