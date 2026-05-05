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

#if !UE_BUILD_SHIPPING
#include "Debug/ComposableCameraViewportDebug.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "SceneManagement.h"   // SDPG_Foreground

namespace
{
	static TAutoConsoleVariable<int32> CVarShowPathGuidedTransitionGizmo(
		TEXT("CCS.Debug.Viewport.Transitions.PathGuided"),
		0,
		TEXT("Show PathGuidedTransition gizmo:\n")
		TEXT("  - Standard source/target/progress triplet in coral accent.\n")
		TEXT("  - The rail spline the camera travels along, sampled as a\n")
		TEXT("    32-segment polyline. For Type=Inertialized this is the\n")
		TEXT("    authored RailActor's spline; for Type=Auto it is the\n")
		TEXT("    internally-generated spline that blends into / out of the\n")
		TEXT("    rail at the source/target positions.\n")
		TEXT("Requires `CCS.Debug.Viewport 1`. Gizmo disappears when the transition finishes."),
		ECVF_Default);
}
#endif

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
					TWeakObjectPtr<AComposableCameraCameraBase> WeakIntermediateCamera = IntermediateCamera;
					OnTransitionFinishesDelegate.AddLambda(
						[WeakIntermediateCamera]()
						{
							if (AComposableCameraCameraBase* Camera = WeakIntermediateCamera.Get())
							{
								Camera->Destroy();
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

	// Transition-level debug draws were previously gated on the legacy
	// `PCM->bDrawDebugInformation` flag. That flag is removed as part of the
	// unified `CCS.Debug.Viewport` framework; transition-specific gizmos can
	// be re-added via a future `DrawTransitionDebug(UWorld*)` virtual on
	// UComposableCameraTransitionBase, mirroring the per-node pattern on
	// UComposableCameraCameraNodeBase.

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

float UComposableCameraPathGuidedTransition::GetBlendWeightAt(float NormalizedTime) const
{
	// Timing curve delegates to the inner driving transition since
	// PathGuided only substitutes the spatial path. No driving transition
	// set (authoring incomplete) → fall back to linear so the panel still
	// renders something rather than a flat zero.
	if (DrivingTransition)
	{
		return DrivingTransition->GetBlendWeightAt(NormalizedTime);
	}
	return FMath::Clamp(NormalizedTime, 0.f, 1.f);
}

#if !UE_BUILD_SHIPPING
void UComposableCameraPathGuidedTransition::DrawTransitionDebug(UWorld* World, bool bViewerIsOutsideCamera) const
{
	if (!World) { return; }
	if (CVarShowPathGuidedTransitionGizmo.GetValueOnGameThread() == 0
		&& !FComposableCameraViewportDebug::ShouldShowAllTransitionGizmos()) { return; }

	// Coral accent — warm but clearly distinct from Ease orange and
	// DynamicDeocclusion red.
	static const FColor AccentColor { 255, 130, 130 };

	DrawStandardTransitionDebug(World, bViewerIsOutsideCamera, AccentColor);

	// Pick whichever spline is actually driving motion this frame.
	// Inertialized → IntermediateCamera's spline node owns the rail; we
	// simply walk the RailActor's spline here, same effect.
	// Auto        → InternalSpline, built at OnBeginPlay in world space.
	const USplineComponent* SplineToDraw = nullptr;
	if (Type == EComposableCameraPathGuidedTransitionType::Auto)
	{
		SplineToDraw = InternalSpline;
	}
	else if (Rail)
	{
		SplineToDraw = Rail->GetRailSplineComponent();
	}

	if (!SplineToDraw)
	{
		return;
	}

	// Sample the spline end-to-end in world space. 32 segments matches the
	// Spline transition's resolution so the two look visually similar when
	// both are enabled.
	constexpr int32 NumSamples = 32;
	const float SplineLength = SplineToDraw->GetSplineLength();
	if (SplineLength <= 0.f)
	{
		return;
	}

	FVector PrevPoint = SplineToDraw->GetLocationAtDistanceAlongSpline(0.f, ESplineCoordinateSpace::World);
	for (int32 i = 1; i <= NumSamples; ++i)
	{
		const float D = SplineLength * (static_cast<float>(i) / static_cast<float>(NumSamples));
		const FVector NextPoint = SplineToDraw->GetLocationAtDistanceAlongSpline(D, ESplineCoordinateSpace::World);
		DrawDebugLine(World, PrevPoint, NextPoint, AccentColor,
			/*bPersistent=*/false, /*LifeTime=*/-1.f,
			/*DepthPriority=*/SDPG_Foreground, /*Thickness=*/1.f);
		PrevPoint = NextPoint;
	}
}
#endif
