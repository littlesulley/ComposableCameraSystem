// Copyright 2026 Sulley. All Rights Reserved.

#include "Transitions/ComposableCameraCompositionPreservingTransition.h"

#include "ComposableCameraSystemModule.h"
#include "GameFramework/Actor.h"
#include "Utils/ComposableCameraActorInputSource.h"

#if !UE_BUILD_SHIPPING
#include "Debug/ComposableCameraDebugDrawSink.h"
#include "Debug/ComposableCameraViewportDebug.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "SceneManagement.h"   // SDPG_Foreground

namespace
{
	static TAutoConsoleVariable<int32> CVarShowCompositionPreservingTransitionGizmo(
		TEXT("CCS.Debug.Viewport.Transitions.CompositionPreserving"),
		0,
		TEXT("Show CompositionPreservingTransition gizmo:\n")
		TEXT("  - Standard source/target/progress triplet in turquoise accent.\n")
		TEXT("  - A line from source endpoint to the tracked subject.\n")
		TEXT("Requires `CCS.Debug.Viewport 1`. Gizmo disappears when the transition finishes."),
		ECVF_Default);
}
#endif

void UComposableCameraCompositionPreservingTransition::OnBeginPlay_Implementation(
	float /*DeltaTime*/,
	const FComposableCameraPose& CurrentSourcePose,
	const FComposableCameraPose& CurrentTargetPose)
{
	if (DrivingTransition)
	{
		float EffectiveTransitionTime = TransitionTime;
		if (EffectiveTransitionTime <= 0.f && DrivingTransition->GetTransitionTime() > 0.f)
		{
			EffectiveTransitionTime = DrivingTransition->GetTransitionTime();
			SetTransitionTime(EffectiveTransitionTime);
			RemainingTime = EffectiveTransitionTime;
		}

		DrivingTransition->TransitionEnabled(InitParams);
		DrivingTransition->SetTransitionTime(EffectiveTransitionTime);
		DrivingTransition->ResetTransitionState();
	}

	bHasCapturedSubjectComposition = CaptureInitialSubjectComposition(CurrentSourcePose);
}

FComposableCameraPose UComposableCameraCompositionPreservingTransition::OnEvaluate_Implementation(
	float DeltaTime,
	const FComposableCameraPose& CurrentSourcePose,
	const FComposableCameraPose& CurrentTargetPose)
{
	if (!DrivingTransition)
	{
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("CompositionPreservingTransition has no DrivingTransition. Falling back to target pose."));
		return CurrentTargetPose;
	}

	const FComposableCameraPose DrivingPose =
		DrivingTransition->Evaluate(DeltaTime, CurrentSourcePose, CurrentTargetPose);
	Percentage = DrivingTransition->IsFinished() ? 1.f : DrivingTransition->GetPercentage();

	AActor* Subject = ResolveSubjectActor();
	if (!bHasCapturedSubjectComposition || !IsValid(Subject))
	{
		return DrivingPose;
	}

	return BuildCompositionPreservingPose(
		CurrentSourcePose,
		CurrentTargetPose,
		Subject->GetActorLocation(),
		DrivingPose.Rotation,
		Percentage);
}

float UComposableCameraCompositionPreservingTransition::GetBlendWeightAt(float NormalizedTime) const
{
	if (DrivingTransition)
	{
		return DrivingTransition->GetBlendWeightAt(NormalizedTime);
	}
	return FMath::Clamp(NormalizedTime, 0.f, 1.f);
}

AActor* UComposableCameraCompositionPreservingTransition::ResolveSubjectActor() const
{
	return ComposableCameraSystem::ResolveActorInput(
		SubjectActorSource,
		SubjectActor.Get(),
		GetOwningPlayerCameraManager(),
		this);
}

bool UComposableCameraCompositionPreservingTransition::CaptureInitialSubjectComposition(
	const FComposableCameraPose& CurrentSourcePose)
{
	AActor* Subject = ResolveSubjectActor();
	if (!IsValid(Subject))
	{
		return false;
	}

	const FVector SubjectOffsetFromSource = Subject->GetActorLocation() - CurrentSourcePose.Position;
	CapturedSubjectOffsetInSourceSpace =
		CurrentSourcePose.Rotation.UnrotateVector(SubjectOffsetFromSource);

	return true;
}

FComposableCameraPose UComposableCameraCompositionPreservingTransition::BuildCompositionPreservingPose(
	const FComposableCameraPose& CurrentSourcePose,
	const FComposableCameraPose& CurrentTargetPose,
	const FVector& CurrentSubjectLocation,
	const FRotator& DrivingRotation,
	float BlendWeight) const
{
	const float ClampedBlendWeight = FMath::Clamp(BlendWeight, 0.f, 1.f);
	FComposableCameraPose OutPose = CurrentSourcePose;
	OutPose.BlendBy(CurrentTargetPose, ClampedBlendWeight);

	const FVector LiveSubjectOffsetInTargetSpace =
		CurrentTargetPose.Rotation.UnrotateVector(CurrentSubjectLocation - CurrentTargetPose.Position);

	const FVector BlendedSubjectOffset = FMath::Lerp(
		CapturedSubjectOffsetInSourceSpace,
		LiveSubjectOffsetInTargetSpace,
		ClampedBlendWeight);
	OutPose.Position =
		CurrentSubjectLocation
		- DrivingRotation.RotateVector(BlendedSubjectOffset);
	OutPose.Rotation = DrivingRotation;
	return OutPose;
}

#if !UE_BUILD_SHIPPING
void UComposableCameraCompositionPreservingTransition::DrawTransitionDebug(
	FComposableCameraDebugDrawSink& Draw, bool bViewerIsOutsideCamera) const
{
	if (CVarShowCompositionPreservingTransitionGizmo.GetValueOnGameThread() == 0
		&& !FComposableCameraViewportDebug::ShouldShowAllTransitionGizmos()) { return; }

	const FColor AccentColor = FComposableCameraViewportDebugColors::TransitionCompositionPreserving();
	DrawStandardTransitionDebug(Draw, bViewerIsOutsideCamera, AccentColor);

	AActor* Subject = ResolveSubjectActor();
	if (IsValid(Subject))
	{
		Draw.DrawLine(LastDebugSource.Position, Subject->GetActorLocation(), AccentColor,
			/*Thickness=*/0.f, /*DepthPriority=*/SDPG_Foreground);
	}
}
#endif
