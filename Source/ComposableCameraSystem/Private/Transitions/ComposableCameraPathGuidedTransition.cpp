// Copyright Sulley. All rights reserved.

#include "Transitions/ComposableCameraPathGuidedTransition.h"
#include "Transitions/ComposableCameraInertializedTransition.h"
#include "CameraRig_Rail.h"
#include "ComposableCameraSystemModule.h"
#include "Components/SplineComponent.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Nodes/ComposableCameraSplineNode.h"
#include "Utils/ComposableCameraBlueprintLibrary.h"

void UComposableCameraPathGuidedTransition::OnBeginPlay_Implementation(float DeltaTime,
                                                                       const FComposableCameraPose& CurrentSourcePose,
                                                                       const FComposableCameraPose& CurrentTargetPose)
{
	if (DrivingTransition)
	{
		DrivingTransition->TransitionEnabled(InitParams);
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
			IntermediateCamera->bIsTransient = false;
			IntermediateCamera->LifeTime = -1.f;
			IntermediateCamera->RemainingLifeTime = -1.f;
			IntermediateCamera->Initialize(UComposableCameraBlueprintLibrary::GetComposableCameraPlayerCameraManager(this, 0));

			UComposableCameraSplineNode* SplineNode = NewObject<UComposableCameraSplineNode>(IntermediateCamera, UComposableCameraSplineNode::StaticClass());
			SplineNode->SplineType = EComposableCameraSplineNodeSplineType::BuiltInSpline;
			SplineNode->Rail = Rail;
			SplineNode->MoveMethod = EComposableCameraSplineNodeMoveMethod::Automatic;
			SplineNode->AutomaticMoveCurve = SplineMoveCurve;
			SplineNode->Duration = TransitionTime;
			IntermediateCamera->CameraNodes.Add(SplineNode);
			IntermediateCamera->FinishSpawning(FTransform{});
			IntermediateCamera->Rename(*MakeUniqueObjectName(IntermediateCamera->GetOuter(),
				IntermediateCamera->GetClass(), TEXT("PathGuidedTransition_IntermediateCameraOnSpline")).ToString());
#if WITH_EDITOR
			IntermediateCamera->SetActorLabel(
				TEXT("PathGuidedTransition_IntermediateCameraOnSpline"),
				false
			);
#endif
			
			// Enter transition.
			EnterTransition = NewObject<UComposableCameraInertializedTransition>(this, UComposableCameraInertializedTransition::StaticClass());
			FComposableCameraTransitionInitParams EnterInitParams = InitParams;
			EnterTransition->TransitionEnabled(EnterInitParams);
			EnterTransition->SetTransitionTime(GuideRange.X * TransitionTime);
			EnterTransition->ResetTransitionState();
			
			break;
		}
	case EComposableCameraPathGuidedTransitionType::Auto:
		{

			DebugSplineActor = GetWorld()->SpawnActor<AActor>();
			USceneComponent* Root = NewObject<USceneComponent>(DebugSplineActor, USceneComponent::StaticClass(), TEXT("RootComponent"));
			Root->RegisterComponent();
			DebugSplineActor->SetRootComponent(Root);

			BuildInternalSpline(CurrentTargetPose, DeltaTime);
			DebugSplineActor->Rename(*MakeUniqueObjectName(DebugSplineActor->GetOuter(),
				DebugSplineActor->GetClass(), TEXT("PathGuidedTransition_DebugSplineActor")).ToString());
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
	const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose)
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
	FComposableCameraPose BasePose = DrivingTransition->Evaluate(DeltaTime, CurrentSourcePose, CurrentTargetPose);
	FComposableCameraPose ResultPose = BasePose;
	Percentage = DrivingTransition->GetPercentage();

	switch (Type)
	{
	case EComposableCameraPathGuidedTransitionType::Inertialized:
		{
			FComposableCameraPose SplinePose = IntermediateCamera->TickCamera(DeltaTime);
			if (DurationPct < GuideRange.X)
			{
				FComposableCameraPose EnterPose = EnterTransition->Evaluate(DeltaTime, CurrentSourcePose, SplinePose);
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
					FComposableCameraTransitionInitParams ExitInitParams;
					ExitInitParams.CurrentSourcePose = IntermediateCamera->CameraPose;
					ExitInitParams.PreviousSourcePose = IntermediateCamera->LastFrameCameraPose;
					ExitInitParams.DeltaTime = DeltaTime;
					ExitTransition->TransitionEnabled(ExitInitParams);
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

				FComposableCameraPose ExitPose = ExitTransition->Evaluate(DeltaTime, SplinePose, CurrentTargetPose);
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

#if ENABLE_DRAW_DEBUG
	// Draw debug point — single DrawDebugPoint call, no intermediate TArray.
	if (AComposableCameraPlayerCameraManager* PCM = UComposableCameraBlueprintLibrary::GetComposableCameraPlayerCameraManager(this, 0))
	{
		if (PCM->bDrawDebugInformation)
		{
			DrawDebugPoint(GetWorld(), ResultPose.Position, 8.f, FColor::Cyan, false, 2.f, 1.f);
		}
	}
#endif

	return ResultPose;
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

	InternalSpline->bAllowDiscontinuousSpline = true;
	InternalSpline->ClearSplinePoints(true);

	// Prepend and append control points (as long as their tangents)
	FVector P0 = Points[0].Position;
	FVector D0 = Points[0].LeaveTangent.GetSafeNormal();
	FVector P1 = UKismetMathLibrary::InverseTransformLocation(DebugSplineActor->GetActorTransform(), InitParams.CurrentSourcePose.Position);
	FVector D1 = (P0 - P1).GetSafeNormal();
	float L = (P0 - P1).Size();
	float C = L * FMath::Sqrt((1.f + D1.Dot(-D0)) / 2.f) / 3.f;

	FSplinePoint FirstPoint;
	FirstPoint.Position = P1;
	FirstPoint.LeaveTangent = C * D1;
	FirstPoint.ArriveTangent = FirstPoint.LeaveTangent;
	FirstPoint.Type = ESplinePointType::CurveCustomTangent;
	Points.Insert(FirstPoint, 0);

	Num = Points.Num();
	P0 = Points[Num - 1].Position;
	D0 = Points[Num - 1].ArriveTangent.GetSafeNormal();
	P1 = UKismetMathLibrary::InverseTransformLocation(DebugSplineActor->GetActorTransform(), CurrentTargetPose.Position);
	D1 = (P0 - P1).GetSafeNormal();
	L = (P0 - P1).Size();
	C = L * FMath::Sqrt((1.f + D1.Dot(D0)) / 2.f) / 3.f;
	
	FSplinePoint LastPoint;
	LastPoint.Position = P1;
	LastPoint.ArriveTangent = C * D1;
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
