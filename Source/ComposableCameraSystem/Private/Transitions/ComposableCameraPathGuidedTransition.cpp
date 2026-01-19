// Copyright Sulley. All rights reserved.

#include "Transitions/ComposableCameraPathGuidedTransition.h"
#include "Transitions/ComposableCameraInertializedTransition.h"
#include "CameraRig_Rail.h"
#include "ComposableCameraSystemModule.h"
#include "Components/SplineComponent.h"
#include "Core/ComposableCameraPlayerCamaraManager.h"
#include "Kismet/GameplayStatics.h"
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
			
			DebugSplineActor = GetWorld()->SpawnActor<AActor>();
			USceneComponent* Root = NewObject<USceneComponent>(DebugSplineActor, USceneComponent::StaticClass(), TEXT("RootComponent"));
			Root->RegisterComponent();
			DebugSplineActor->SetRootComponent(Root);
			
			BuildInternalSpline(CurrentTargetPose, DeltaTime);
			DebugSplineActor->Rename(TEXT("PathGuidedTransition_DebugSplineActor"));
	#if WITH_EDITOR
			DebugSplineActor->SetActorLabel(
				TEXT("PathGuidedTransition_DebugSplineActor"),
				false
			);
	#endif
			
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
	Percentage = DrivingTransition->GetPercentage();
	
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
			break;
		}	
	case EComposableCameraPathGuidedTransitionType::Auto:
		{
			if (!InternalSpline)
			{
				UE_LOG(LogComposableCameraSystem, Warning, TEXT("InternalSpline is not valid in ComposableCameraPathGuidedTransition."));
				return CurrentTargetPose;
			}
			
			const float SplineLen = InternalSpline->GetSplineLength();
			const FVector Position = InternalSpline->GetLocationAtDistanceAlongSpline(Percentage * SplineLen, ESplineCoordinateSpace::World);
			ResultPose.Position = Position;
			break;
		}
	}

	// Draw debug spline.
	if (TargetCamera && TargetCamera->GetOwningPlayerCameraManager())
	{
		if (TargetCamera->GetOwningPlayerCameraManager()->bDrawDebugInformation)
		{
			DrawDebugSplinePoints(TArray{ ResultPose.Position });
		}
	}

	return ResultPose;
}

void UComposableCameraPathGuidedTransition::DrawDebugSplinePoints(const TArray<FVector>& SplinePoints)
{
	for (const FVector& Point : SplinePoints)
	{
		DrawDebugPoint(GetWorld(), Point, 8.f, FColor::Cyan, false, 2.f, 1.f);
	}
}

void UComposableCameraPathGuidedTransition::BuildInternalSpline(const FComposableCameraPose& CurrentTargetPose, float DeltaTime)
{
	InternalSpline = DuplicateObject(Rail->GetRailSplineComponent(), DebugSplineActor, TEXT("InternalSplineForPathGuidedTransition"));
	InternalSpline->RegisterComponent();
	DebugSplineActor->SetActorTransform(Rail->GetActorTransform());
	DebugSplineActor->AddInstanceComponent(InternalSpline);
	InternalSpline->AttachToComponent(
		DebugSplineActor->GetRootComponent(),
		FAttachmentTransformRules::KeepRelativeTransform
	);
	InternalSpline->SetRelativeTransform(FTransform::Identity);
	
	TArray<FSplinePoint> Points;
	Points.Reserve(8);

	int32 Num = InternalSpline->GetNumberOfSplinePoints();
	for (int32 i = 0; i < Num; ++i)
	{
		Points.Add(
			InternalSpline->GetSplinePointAt(i, ESplineCoordinateSpace::Local)
		);
	}

	InternalSpline->ClearSplinePoints(true);

	// Prepend and append control points (as long as their tangents)
	FVector P0 = Points[1].Position;
	FVector P1 = Points[0].Position;
	FVector P2 = UKismetMathLibrary::InverseTransformLocation(DebugSplineActor->GetActorTransform(), StartCameraPose.Position);
	FVector P3 = UKismetMathLibrary::InverseTransformLocation(DebugSplineActor->GetActorTransform(), SourceCamera->LastFrameCameraPose.Position);

	FSplinePoint FirstPoint;
	FirstPoint.Position = P2;
	FirstPoint.LeaveTangent = (P2 - P3) / DeltaTime;
	FirstPoint.ArriveTangent = FirstPoint.LeaveTangent;
	FirstPoint.Type = ESplinePointType::CurveCustomTangent;
	Points.Insert(FirstPoint, 0);

	Num = Points.Num();
	P0 = Points[Num - 2].Position;
	P1 = Points[Num - 1].Position;
	P2 = UKismetMathLibrary::InverseTransformLocation(DebugSplineActor->GetActorTransform(), CurrentTargetPose.Position);
	P3 = UKismetMathLibrary::InverseTransformLocation(DebugSplineActor->GetActorTransform(), TargetCamera->LastFrameCameraPose.Position);

	FSplinePoint LastPoint;
	LastPoint.Position = P2;
	LastPoint.ArriveTangent = (P2 - P3) / DeltaTime;
	LastPoint.LeaveTangent = LastPoint.ArriveTangent;
	LastPoint.Type = ESplinePointType::CurveCustomTangent;
	Points.Add(LastPoint);

	// Re-add points
	for (Num = 0; auto& P : Points)
	{
		P.InputKey = Num;
		InternalSpline->AddPoint(P, false);
		++Num;
	}

	InternalSpline->UpdateSpline();
}
