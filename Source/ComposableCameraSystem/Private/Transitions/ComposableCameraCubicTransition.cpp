// Copyright 2026 Sulley. All Rights Reserved.

#include "Transitions/ComposableCameraCubicTransition.h"

#if !UE_BUILD_SHIPPING
#include "Debug/ComposableCameraDebugDrawSink.h"
#include "Debug/ComposableCameraViewportDebug.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "SceneManagement.h"   // SDPG_Foreground

namespace
{
	static TAutoConsoleVariable<int32> CVarShowCubicTransitionGizmo(
		TEXT("CCS.Debug.Viewport.Transitions.Cubic"),
		0,
		TEXT("Show CubicTransition gizmo (source/target/progress in lavender accent).\n")
		TEXT("Requires `CCS.Debug.Viewport 1`. Gizmo disappears when the transition finishes."),
		ECVF_Default);
}
#endif

FComposableCameraPose UComposableCameraCubicTransition::OnEvaluate_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose)
{
	float DurationPct = (GetTransitionTime() - GetRemainingTime()) / GetTransitionTime();
	float BlendWeight = FMath::CubicInterp(0.f, 0.f, 1.f, 0.f, DurationPct);
	Percentage = BlendWeight;

	FComposableCameraPose CurrentPose = BlendPosesByLockedRotationPath(
		CurrentSourcePose,
		CurrentTargetPose,
		BlendWeight);

	return CurrentPose;
}

float UComposableCameraCubicTransition::GetBlendWeightAt(float NormalizedTime) const
{
	// Same CubicInterp call OnEvaluate uses.
	return FMath::CubicInterp(0.f, 0.f, 1.f, 0.f, FMath::Clamp(NormalizedTime, 0.f, 1.f));
}

#if !UE_BUILD_SHIPPING
void UComposableCameraCubicTransition::DrawTransitionDebug(FComposableCameraDebugDrawSink& Draw, bool bViewerIsOutsideCamera) const
{
	if (CVarShowCubicTransitionGizmo.GetValueOnGameThread() == 0
		&& !FComposableCameraViewportDebug::ShouldShowAllTransitionGizmos()) { return; }

	// Lavender. Still in the cool family but well clear of LookAt cyan
	// and SplineNode violet.
	const FColor AccentColor = FComposableCameraViewportDebugColors::TransitionCubic();

	DrawStandardTransitionDebug(Draw, bViewerIsOutsideCamera, AccentColor);

	// Path is a straight line. Position lerps linearly in space. Only
	// the cubic timing curve differs from Linear.
	Draw.DrawLine(LastDebugSource.Position, LastDebugTarget.Position, AccentColor,
		/*Thickness=*/0.f, /*DepthPriority=*/SDPG_Foreground);
}
#endif
