// Copyright Sulley. All rights reserved.

#include "Transitions/ComposableCameraDynamicDeocclusionTransition.h"

#include "ComposableCameraSystemModule.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
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

	// Ignored actors.
	for (TSoftClassPtr<AActor> ActorType: ActorTypesToIgnore)
	{
		if (ActorType.IsValid())
		{
			TArray<AActor*> IgnoredActors;
			UGameplayStatics::GetAllActorsOfClass(this, ActorType.Get(), IgnoredActors);
			ActorsToIgnore.Append(IgnoredActors);
		}
	}
}

FComposableCameraPose UComposableCameraDynamicDeocclusionTransition::OnEvaluate_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose)
{
	if (!DrivingTransition)
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT("DrivingTransition is not valid in ComposableCameraDynamicDeocclusionTransition."));
		return FComposableCameraPose{};
	}

	FComposableCameraPose BasePose = DrivingTransition->Evaluate(DeltaTime, CurrentSourcePose, CurrentTargetPose);
	FComposableCameraPose CandidatePose = BasePose;
	CandidatePose.Position = PreviousOffset + BasePose.Position;
	FVector AggregateOffset = FVector::ZeroVector;

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
			ActorsToIgnore,
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
