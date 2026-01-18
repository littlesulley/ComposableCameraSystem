// Copyright Sulley. All rights reserved.

#include "Transitions/ComposableCameraPathGuidedTransition.h"
#include "Transitions/ComposableCameraInertializedTransition.h"
#include "CameraRig_Rail.h"
#include "ComposableCameraSystemModule.h"
#include "Components/SplineComponent.h"
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

	switch (Type)
	{
	case EComposableCameraPathGuidedTransitionType::Inertialized:
		{
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
			break;
		}	
	case EComposableCameraPathGuidedTransitionType::Auto:
		{
			InternalSpline = DuplicateObject(Rail->GetRailSplineComponent(), this, TEXT("InternalSplineForPathGuidedTransition"));
			BuildInternalSpline(CurrentTargetPose);
			break;
		}
	}
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
	FComposableCameraPose ResultPose = BasePose;
	
	switch (Type)
	{
	case EComposableCameraPathGuidedTransitionType::Inertialized:
		{
			FComposableCameraPose SplinePose = IntermediateCamera->TickCamera(DeltaTime);
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
					DrawDebugSplinePoints(TArray{ ResultPose.Position });
				}
			}
			break;
		}	
	case EComposableCameraPathGuidedTransitionType::Auto:
		{
			
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

void UComposableCameraPathGuidedTransition::BuildInternalSpline(const FComposableCameraPose& CurrentTargetPose)
{
	TArray<FSplinePoint> Points;

	const int32 Num = InternalSpline->GetNumberOfSplinePoints();
	for (int32 i = 0; i < Num; ++i)
	{
		Points.Add(
			InternalSpline->GetSplinePointAt(i, ESplineCoordinateSpace::World)
		);
	}

	InternalSpline->ClearSplinePoints(true);

	// Prepend and append control points (as long as their tangents)
	FVector P0 = Points[1].Position;
	FVector P3 = Points[0].Position;
	FVector P1 = Points[1].ArriveTangent + P0;
	FVector P2 = Points[0].LeaveTangent + P3;
	FVector P4 = 2. * P3 - P2;
	FVector P5 = P1 + 4. * (P3 - P2);
	FVector P6 = StartCameraPose.Position;
	Points[0].ArriveTangent = P4 - P3;

	FSplinePoint FirstPoint;
	FirstPoint.Position = P6;
	FirstPoint.LeaveTangent = P5 - P6;
	FirstPoint.Type = ESplinePointType::CurveCustomTangent;
	Points.Insert(FirstPoint, 0);
	

	// Re-add points
	for (const auto& P : Points)
	{
		InternalSpline->AddPoint(P, false);
	}

	InternalSpline->UpdateSpline();
}
