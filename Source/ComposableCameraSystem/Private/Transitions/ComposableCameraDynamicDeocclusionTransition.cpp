// Copyright Sulley. All rights reserved.

#include "Transitions/ComposableCameraDynamicDeocclusionTransition.h"

#include "ComposableCameraSystemModule.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "Curves/CurveFloat.h"
#include "EditorHooks/EditorHooks.h"
#include "Kismet/GameplayStatics.h"
#include "Utils/ComposableCameraBlueprintLibrary.h"

#if !UE_BUILD_SHIPPING
#include "Debug/ComposableCameraViewportDebug.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "SceneManagement.h"   // SDPG_Foreground

namespace
{
	static TAutoConsoleVariable<int32> CVarShowDynamicDeocclusionTransitionGizmo(
		TEXT("CCS.Debug.Viewport.Transitions.DynamicDeocclusion"),
		0,
		TEXT("Show DynamicDeocclusionTransition gizmo:\n")
		TEXT("  - Standard source/target/progress triplet in red accent.\n")
		TEXT("  - Every feeler ray emanating from the current blended pose,\n")
		TEXT("    drawn with its configured radius and length. Essential when\n")
		TEXT("    tuning feeler angles — you can see in 3D whether a feeler\n")
		TEXT("    actually points at the geometry it's supposed to dodge.\n")
		TEXT("Requires `CCS.Debug.Viewport 1`. Gizmo disappears when the transition finishes."),
		ECVF_Default);
}
#endif

void UComposableCameraDynamicDeocclusionTransition::OnBeginPlay_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose)
{
	if (DrivingTransition)
	{
		DrivingTransition->TransitionEnabled(InitParams);
		DrivingTransition->SetTransitionTime(TransitionTime);
		DrivingTransition->ResetTransitionState();
	}

	// Kismet-trace built-in debug draw is disabled in favour of the unified
	// CCS.Debug.Viewport framework — a future DrawTransitionDebug(UWorld*)
	// virtual on UComposableCameraTransitionBase can route through it if
	// transition-level gizmos are wanted back.
	DrawDebugType = EDrawDebugTrace::None;

	// Ignored actors. Reset both arrays first so a re-used transition object
	// (NewObject from a previous activation) doesn't accumulate entries
	// across runs — the original code only `Append`'d, so a transition that
	// was activated twice ended up with double + stale entries.
	ActorsToIgnoreWeak.Reset();
	ResolvedActorsToIgnore.Reset();

	for (TSoftClassPtr<AActor> ActorType : ActorTypesToIgnore)
	{
		if (ActorType.IsValid())
		{
			TArray<AActor*> IgnoredActors;
			UGameplayStatics::GetAllActorsOfClass(this, ActorType.Get(), IgnoredActors);
			ActorsToIgnoreWeak.Reserve(ActorsToIgnoreWeak.Num() + IgnoredActors.Num());
			for (AActor* Ignored : IgnoredActors)
			{
				ActorsToIgnoreWeak.Add(Ignored);
			}
		}
	}
}

FComposableCameraPose UComposableCameraDynamicDeocclusionTransition::OnEvaluate_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose)
{
	if (!DrivingTransition)
	{
		// Returning a default-constructed `FComposableCameraPose{}` here would
		// hand back zero position / identity rotation / default FOV (90°) /
		// default projection — a hard snap to a black-frame-ish pose that
		// looks like missing data, asset corruption, or init-failure error
		// art. Hard-cutting to the target pose is the standard "transition
		// ineffective" fallback and matches what `PathGuidedTransition`
		// already does on its own missing-DrivingTransition guard. Keep the
		// log so authoring incompletions still surface.
		UE_LOG(LogComposableCameraSystem, Warning, TEXT("DrivingTransition is not valid in ComposableCameraDynamicDeocclusionTransition."));
		return CurrentTargetPose;
	}

	FComposableCameraPose BasePose = DrivingTransition->Evaluate(DeltaTime, CurrentSourcePose, CurrentTargetPose);
	FComposableCameraPose CandidatePose = BasePose;
	CandidatePose.Position = PreviousOffset + BasePose.Position;
	FVector AggregateOffset = FVector::ZeroVector;

	// Rebuild the raw ignore-list from the weak snapshot taken at
	// OnBeginPlay — actors that have been destroyed during the transition
	// drop out, the ones still alive flow through. `Reset` keeps the
	// previously-allocated capacity so this is allocation-free in steady
	// state.
	ResolvedActorsToIgnore.Reset(ActorsToIgnoreWeak.Num());
	for (const TWeakObjectPtr<AActor>& Weak : ActorsToIgnoreWeak)
	{
		if (AActor* Live = Weak.Get(); IsValid(Live))
		{
			ResolvedActorsToIgnore.Add(Live);
		}
	}

	// For each feeler.
	for (const auto& Feeler : Feelers)
	{
		FVector StartPosition = Feeler.GetRayStartPosition(CandidatePose);
		FVector EndPosition = Feeler.GetRayEndPosition(CandidatePose);

		FHitResult Hit;
		UKismetSystemLibrary::SphereTraceSingle(
			this,
			StartPosition,
			EndPosition,
			Feeler.Radius,
			TraceChannel,
			true,
			ResolvedActorsToIgnore,
			DrawDebugType,
			Hit,
			true);

		if (Hit.bBlockingHit && Feeler.StrengthCurve)
		{
			FVector Offset = CandidatePose.Rotation.RotateVector(Feeler.Offset.GetSafeNormal());
			AggregateOffset += Offset * Feeler.StrengthCurve->GetFloatValue(Hit.Distance);
		}
	}

	if (AggregateOffset.IsZero())
	{
		ElapsedWaitingTime += DeltaTime;
	}
	else
	{
		ElapsedWaitingTime = 0.f;
	}

	if (ElapsedWaitingTime >= ResumeWaitingTime || (GetTransitionTime() - GetRemainingTime()) / GetTransitionTime() >= DeadPercentage)
	{
		AggregateOffset = FMath::VInterpTo(
			PreviousOffset,
			FVector::ZeroVector,
			DeltaTime,
			DeocclusionSpeed
		);
	}
	else
	{
		AggregateOffset = FMath::VInterpTo(
			PreviousOffset,
			PreviousOffset + AggregateOffset,
			DeltaTime,
			DeocclusionSpeed
		);
	}
	
	CandidatePose.Position = BasePose.Position + AggregateOffset;
	PreviousOffset = AggregateOffset;

	Percentage = DrivingTransition->GetPercentage();

	// Drop raw AActor* pointers before returning. `ResolvedActorsToIgnore`
	// is a non-UPROPERTY UObject-pointer container on this transition (a
	// UCLASS member); raw pointers stored here are GC-blind, so leaving the
	// list populated across frames would let a stale pointer survive an
	// ignored-actor's destruction. Reset() preserves the underlying TArray
	// capacity so the next-frame rebuild stays alloc-free, while leaving
	// the array empty between Evaluate calls.
	ResolvedActorsToIgnore.Reset();

	return CandidatePose;
}

float UComposableCameraDynamicDeocclusionTransition::GetBlendWeightAt(float NormalizedTime) const
{
	if (DrivingTransition)
	{
		return DrivingTransition->GetBlendWeightAt(NormalizedTime);
	}
	return FMath::Clamp(NormalizedTime, 0.f, 1.f);
}

#if !UE_BUILD_SHIPPING
void UComposableCameraDynamicDeocclusionTransition::DrawTransitionDebug(
	UWorld* World, bool bViewerIsOutsideCamera) const
{
	if (!World) { return; }
	if (CVarShowDynamicDeocclusionTransitionGizmo.GetValueOnGameThread() == 0
		&& !FComposableCameraViewportDebug::ShouldShowAllTransitionGizmos()) { return; }

	// Red accent reinforces the "danger / avoid" intent of deocclusion.
	static const FColor AccentColor { 255, 90, 90 };

	DrawStandardTransitionDebug(World, bViewerIsOutsideCamera, AccentColor);

	// Feeler rays at the current blended pose. GetRayStartPosition /
	// GetRayEndPosition only read Position and Rotation, so we synthesize
	// a minimal pose from our narrow debug snapshot rather than caching
	// the whole FComposableCameraPose (see the GC rationale in
	// FTransitionDebugSnapshot's header comment).
	FComposableCameraPose BlendedPoseForFeelers;
	BlendedPoseForFeelers.Position = LastDebugBlended.Position;
	BlendedPoseForFeelers.Rotation = LastDebugBlended.Rotation;

	for (const FComposableCameraRayFeeler& Feeler : Feelers)
	{
		const FVector RayStart = Feeler.GetRayStartPosition(BlendedPoseForFeelers);
		const FVector RayEnd   = Feeler.GetRayEndPosition(BlendedPoseForFeelers);

		DrawDebugLine(World, RayStart, RayEnd, AccentColor,
			/*bPersistent=*/false, /*LifeTime=*/-1.f,
			/*DepthPriority=*/SDPG_Foreground, /*Thickness=*/0.f);

		// If the feeler has a non-zero radius the engine performs a sphere
		// trace along the ray — show the sphere at the tip so the user
		// knows the trace has volume, not just a line. Low alpha (70) so
		// multiple overlapping feeler tips don't pile up into an opaque
		// mass.
		if (Feeler.Radius > 0.f)
		{
			FComposableCameraViewportDebug::DrawSolidDebugSphere(
				World, RayEnd, Feeler.Radius, AccentColor,
				/*Alpha=*/70, /*Segments=*/10, /*DepthPriority=*/SDPG_Foreground);
		}
	}
}
#endif
