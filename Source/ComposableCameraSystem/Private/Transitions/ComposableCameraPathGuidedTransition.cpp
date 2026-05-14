// Copyright Sulley. All rights reserved.

#include "Transitions/ComposableCameraPathGuidedTransition.h"
#include "Transitions/ComposableCameraInertializedTransition.h"
#include "CameraRig_Rail.h"
#include "ComposableCameraSystemModule.h"
#include "Components/SplineComponent.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Nodes/ComposableCameraSplineNode.h"
#include "Utils/ComposableCameraBlueprintLibrary.h"

#if !UE_BUILD_SHIPPING
#include "Debug/ComposableCameraViewportDebug.h"
#include "DrawDebugHelpers.h"
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

bool UComposableCameraPathGuidedTransition::ResolveAndValidateRail()
{
	// Sync-load the soft pointer; the user authored this slot so we want the
	// actor available now, not "eventually". Null/unloaded soft pointer is a
	// data error (transition wired with no rail). Log and bail so OnEvaluate's
	// nullcheck hard-cuts to the target pose.
	if (RailActor.IsNull())
	{
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("PathGuidedTransition: RailActor is unset. Transition will hard-cut to target."));
		return false;
	}

	Rail = RailActor.LoadSynchronous();
	if (!Rail)
	{
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("PathGuidedTransition: RailActor '%s' failed to load. Transition will hard-cut to target."),
			*RailActor.ToString());
		return false;
	}

	const USplineComponent* RailSpline = Rail->GetRailSplineComponent();
	if (!RailSpline)
	{
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("PathGuidedTransition: Rail '%s' has no spline component. Transition will hard-cut to target."),
			*Rail->GetName());
		Rail = nullptr;
		return false;
	}

	// One spline point is the absolute minimum for BuildInternalSpline's
	// `Points[0]` access and for SplineNode's distance sampling. Two would be
	// a degenerate straight line; one falls back to whatever the source/target
	// tangents synthesize. Either is recoverable; zero is not.
	if (RailSpline->GetNumberOfSplinePoints() < 1)
	{
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("PathGuidedTransition: Rail '%s' spline has no points. Transition will hard-cut to target."),
			*Rail->GetName());
		Rail = nullptr;
		return false;
	}

	return true;
}

void UComposableCameraPathGuidedTransition::DestroySpawnedActors()
{
	if (IsValid(IntermediateCamera))
	{
		IntermediateCamera->Destroy();
	}
	IntermediateCamera = nullptr;

	if (IsValid(DebugSplineActor))
	{
		DebugSplineActor->Destroy();
	}
	DebugSplineActor = nullptr;
}

void UComposableCameraPathGuidedTransition::BeginDestroy()
{
	// Backup cleanup. Normal completion runs DestroySpawnedActors via the
	// OnTransitionFinishesDelegate registered in OnBeginPlay; this catches the
	// interrupted-mid-blend case (camera destroyed, eval tree pruned) where
	// that delegate never fires.
	DestroySpawnedActors();
	Super::BeginDestroy();
}

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

	// Hard precondition for both branches: a usable rail spline. Spawning
	// actors before this check (the previous behavior) leaks them on bad data
	//. Every failed validation earlier left a half-initialized IntermediateCamera
	// or DebugSplineActor in the level. Resolving up-front keeps the failure
	// path actor-free; OnEvaluate's `if (!Rail) return CurrentTargetPose;`
	// then provides the visible behavior (hard-cut).
	if (!ResolveAndValidateRail())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Single cleanup hook covering both spawned actors. Captures `this` weakly
	//. If the transition UObject is gone by the time the delegate fires
	// (shouldn't happen on the normal path but keeps us safe), BeginDestroy
	// already handled cleanup.
	TWeakObjectPtr<UComposableCameraPathGuidedTransition> WeakSelf(this);
	OnTransitionFinishesDelegate.AddLambda([WeakSelf]()
	{
		if (UComposableCameraPathGuidedTransition* Self = WeakSelf.Get())
		{
			Self->DestroySpawnedActors();
		}
	});

	switch (Type)
	{
	case EComposableCameraPathGuidedTransitionType::Inertialized:
		{
			// IntermediateCamera. Every spawn / NewObject step is null-checked
			// because each one can fail under late-init / world-teardown
			// conditions (class load failure, GC pressure, blocked spawn) and
			// the previous code dereferenced the result immediately. On failure
			// we DestroySpawnedActors() to clean up any half-built state and
			// fall through. The transition becomes a hard cut on the next
			// Evaluate (the missing IntermediateCamera / DrivingTransition
			// guards there return CurrentTargetPose).
			IntermediateCamera = World->SpawnActorDeferred<AComposableCameraCameraBase>(
				AComposableCameraCameraBase::StaticClass(), FTransform{});
			if (!IntermediateCamera)
			{
				UE_LOG(LogComposableCameraSystem, Warning,
					TEXT("PathGuidedTransition (Inertialized): SpawnActorDeferred returned null. Falling back to hard cut."));
				DestroySpawnedActors();
				break;
			}
			IntermediateCamera->bIsTransient = false;
			IntermediateCamera->LifeTime = -1.f;
			IntermediateCamera->RemainingLifeTime = -1.f;
			IntermediateCamera->Initialize(UComposableCameraBlueprintLibrary::GetComposableCameraPlayerCameraManager(this, 0));

			UComposableCameraSplineNode* SplineNode = NewObject<UComposableCameraSplineNode>(IntermediateCamera, UComposableCameraSplineNode::StaticClass());
			if (!SplineNode)
			{
				UE_LOG(LogComposableCameraSystem, Warning,
					TEXT("PathGuidedTransition (Inertialized): NewObject<UComposableCameraSplineNode> returned null. Falling back to hard cut."));
				DestroySpawnedActors();
				break;
			}
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
			if (!EnterTransition)
			{
				UE_LOG(LogComposableCameraSystem, Warning,
					TEXT("PathGuidedTransition (Inertialized): NewObject<UComposableCameraInertializedTransition> returned null. Falling back to hard cut."));
				DestroySpawnedActors();
				break;
			}
			FComposableCameraTransitionInitParams EnterInitParams = InitParams;
			EnterTransition->TransitionEnabled(EnterInitParams);
			EnterTransition->SetTransitionTime(GuideRange.X * TransitionTime);
			EnterTransition->ResetTransitionState();

			break;
		}
	case EComposableCameraPathGuidedTransitionType::Auto:
		{
			DebugSplineActor = World->SpawnActor<AActor>();
			if (!DebugSplineActor)
			{
				UE_LOG(LogComposableCameraSystem, Warning,
					TEXT("PathGuidedTransition (Auto): SpawnActor returned null. Falling back to hard cut."));
				DestroySpawnedActors();
				break;
			}

			USceneComponent* Root = NewObject<USceneComponent>(DebugSplineActor, USceneComponent::StaticClass(), TEXT("RootComponent"));
			if (!Root)
			{
				UE_LOG(LogComposableCameraSystem, Warning,
					TEXT("PathGuidedTransition (Auto): NewObject<USceneComponent> returned null. Falling back to hard cut."));
				DestroySpawnedActors();
				break;
			}
			Root->RegisterComponent();
			DebugSplineActor->SetRootComponent(Root);

			if (!BuildInternalSpline(CurrentTargetPose, DeltaTime))
			{
				UE_LOG(LogComposableCameraSystem, Warning,
					TEXT("PathGuidedTransition (Auto): BuildInternalSpline failed. Falling back to hard cut."));
				DestroySpawnedActors();
				break;
			}
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
			if (!IntermediateCamera)
			{
				return CurrentTargetPose;
			}
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
					// Lazy-create the exit transition the first time we
					// cross GuideRange.Y. Same null-check discipline as
					// OnBeginPlay's earlier spawns -NewObject can return
					// null on teardown / GC pressure / abnormal source-
					// object state, and the immediate `TransitionEnabled`
					// deref would crash. On failure, tear down the
					// transition and degrade to a hard cut: subsequent
					// frames hit the `!IntermediateCamera` early-return
					// at the top of this branch and keep returning
					// CurrentTargetPose without retrying NewObject (a
					// failure mode that retries every frame would be
					// worse than a single hard cut).
					ExitTransition = NewObject<UComposableCameraInertializedTransition>(this, UComposableCameraInertializedTransition::StaticClass());
					if (!ExitTransition)
					{
						UE_LOG(LogComposableCameraSystem, Warning,
							TEXT("PathGuidedTransition (Inertialized): NewObject<UComposableCameraInertializedTransition> returned null in OnEvaluate. Falling back to hard cut."));
						DestroySpawnedActors();
						return CurrentTargetPose;
					}
					FComposableCameraTransitionInitParams ExitInitParams;
					ExitInitParams.CurrentSourcePose = IntermediateCamera->CameraPose;
					ExitInitParams.PreviousSourcePose = IntermediateCamera->LastFrameCameraPose;
					ExitInitParams.DeltaTime = DeltaTime;
					ExitTransition->TransitionEnabled(ExitInitParams);
					ExitTransition->SetTransitionTime(GetTransitionTime() * (1.f - GuideRange.Y));
					ExitTransition->ResetTransitionState();
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

bool UComposableCameraPathGuidedTransition::BuildInternalSpline(const FComposableCameraPose& CurrentTargetPose, float DeltaTime)
{
	// Precondition: ResolveAndValidateRail succeeded->Rail, RailSplineComponent,
	// and >= 1 spline point are all guaranteed. Caller (OnBeginPlay Auto branch)
	// also ensures DebugSplineActor was spawned. Defensive null-check on the
	// DuplicateObject return. The engine can return null on archetype lookup
	// failure or GC interference, and the immediate RegisterComponent deref
	// would crash.
	InternalSpline = DuplicateObject(Rail->GetRailSplineComponent(), DebugSplineActor, TEXT("InternalSplineForPathGuidedTransition"));
	if (!InternalSpline)
	{
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("BuildInternalSpline: DuplicateObject returned null for Rail '%s'."),
			*GetNameSafe(Rail));
		return false;
	}
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

#if WITH_EDITORONLY_DATA
	InternalSpline->bAllowDiscontinuousSpline = true;
#endif
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
	return true;
}

float UComposableCameraPathGuidedTransition::GetBlendWeightAt(float NormalizedTime) const
{
	// Timing curve delegates to the inner driving transition since
	// PathGuided only substitutes the spatial path. No driving transition
	// set (authoring incomplete) ->fall back to linear so the panel still
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

	// Coral accent. Warm but clearly distinct from Ease orange and
	// DynamicDeocclusion red.
	static const FColor AccentColor { 255, 130, 130 };

	DrawStandardTransitionDebug(World, bViewerIsOutsideCamera, AccentColor);

	// Pick whichever spline is actually driving motion this frame.
	// Inertialized ->IntermediateCamera's spline node owns the rail; we
	// simply walk the RailActor's spline here, same effect.
	// Auto->InternalSpline, built at OnBeginPlay in world space.
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
