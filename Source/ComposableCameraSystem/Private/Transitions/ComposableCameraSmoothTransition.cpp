// Copyright 2026 Sulley. All Rights Reserved.

#include "Transitions/ComposableCameraSmoothTransition.h"
#include "Math/ComposableCameraMath.h"

#if !UE_BUILD_SHIPPING
#include "Debug/ComposableCameraDebugDrawSink.h"
#include "Debug/ComposableCameraViewportDebug.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "SceneManagement.h"   // SDPG_Foreground

namespace
{
	static TAutoConsoleVariable<int32> CVarShowSmoothTransitionGizmo(
		TEXT("CCS.Debug.Viewport.Transitions.Smooth"),
		0,
		TEXT("Show SmoothTransition gizmo (source/target/progress in gold accent).\n")
		TEXT("Requires `CCS.Debug.Viewport 1`. Gizmo disappears when the transition finishes."),
		ECVF_Default);
}
#endif

FComposableCameraPose UComposableCameraSmoothTransition::OnEvaluate_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose)
{
	float DurationPct = (GetTransitionTime() - GetRemainingTime()) / GetTransitionTime();
	float BlendWeight = bSmootherStep ? ComposableCameraSystem::SmootherStep(DurationPct) : ComposableCameraSystem::SmoothStep(DurationPct);
	Percentage = BlendWeight;

	FComposableCameraPose CurrentPose = BlendPosesByLockedRotationPath(
		CurrentSourcePose,
		CurrentTargetPose,
		BlendWeight);

	return CurrentPose;
}

float UComposableCameraSmoothTransition::GetBlendWeightAt(float NormalizedTime) const
{
	// Mirrors the OnEvaluate branch exactly: SmootherStep vs SmoothStep,
	// whichever the authored `bSmootherStep` flag picks. Pure math, no
	// state reads, safe to call many times per frame from the snapshot
	// sampler.
	const float T = FMath::Clamp(NormalizedTime, 0.f, 1.f);
	return bSmootherStep
		? ComposableCameraSystem::SmootherStep(T)
		: ComposableCameraSystem::SmoothStep(T);
}

#if !UE_BUILD_SHIPPING
void UComposableCameraSmoothTransition::DrawTransitionDebug(FComposableCameraDebugDrawSink& Draw, bool bViewerIsOutsideCamera) const
{
	if (CVarShowSmoothTransitionGizmo.GetValueOnGameThread() == 0
		&& !FComposableCameraViewportDebug::ShouldShowAllTransitionGizmos()
		&& !Draw.ShouldForceDrawAllTransitionGizmos()) { return; }

	// Gold accent. Warm, rich, distinct from Linear's neutral grey.
	const FColor AccentColor = FComposableCameraViewportDebugColors::TransitionSmooth();

	DrawStandardTransitionDebug(Draw, bViewerIsOutsideCamera, AccentColor);

	// Path is a straight line. Position lerps linearly in space; only the
	// smooth-step / smoother-step timing curve differs from Linear.
	Draw.DrawLine(LastDebugSource.Position, LastDebugTarget.Position, AccentColor,
		/*Thickness=*/0.f, /*DepthPriority=*/SDPG_Foreground);
}
#endif
