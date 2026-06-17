// Copyright 2026 Sulley. All Rights Reserved.

#include "Transitions/ComposableCameraLinearTransition.h"

#if !UE_BUILD_SHIPPING
#include "Debug/ComposableCameraDebugDrawSink.h"
#include "Debug/ComposableCameraViewportDebug.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "SceneManagement.h"   // SDPG_Foreground

namespace
{
	// Per-transition opt-in. Master `CCS.Debug.Viewport` gates whether any
	// transition gizmo draws at all; this CVar then decides whether THIS
	// transition's progress visualization contributes. Consistent pattern
	// with every per-node gizmo CVar.
	static TAutoConsoleVariable<int32> CVarShowLinearTransitionGizmo(
		TEXT("CCS.Debug.Viewport.Transitions.Linear"),
		0,
		TEXT("Show LinearTransition gizmo while the blend is active:\n")
		TEXT("  - Green/blue source/target spheres, white 'lerp baseline' line.\n")
		TEXT("  - Light grey progress sphere at the blended pose.\n")
		TEXT("  - In F8/SIE: half-scale green/blue frustums at source and target.\n")
		TEXT("Requires `CCS.Debug.Viewport 1`. Gizmo disappears the instant the\n")
		TEXT("transition finishes (the inner node collapses)."),
		ECVF_Default);
}
#endif

FComposableCameraPose UComposableCameraLinearTransition::OnEvaluate_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose)
{
	float BlendWeight = (GetTransitionTime() - GetRemainingTime()) / GetTransitionTime();
	Percentage = BlendWeight;

	FComposableCameraPose CurrentPose = BlendPosesByLockedRotationPath(
		CurrentSourcePose,
		CurrentTargetPose,
		BlendWeight);

	return CurrentPose;
}

#if !UE_BUILD_SHIPPING
void UComposableCameraLinearTransition::DrawTransitionDebug(FComposableCameraDebugDrawSink& Draw, bool bViewerIsOutsideCamera) const
{
	if (CVarShowLinearTransitionGizmo.GetValueOnGameThread() == 0
		&& !FComposableCameraViewportDebug::ShouldShowAllTransitionGizmos()) { return; }

	// Light grey accent -"neutral" progress color to match the linear
	// blend's unremarkable nature. Distinct from every node gizmo color.
	const FColor AccentColor = FComposableCameraViewportDebugColors::TransitionLinear();

	DrawStandardTransitionDebug(Draw, bViewerIsOutsideCamera, AccentColor);

	// Path is a straight line. Position blends linearly in space (only
	// the timing curve would differ for Smooth/Ease/Cubic siblings).
	Draw.DrawLine(LastDebugSource.Position, LastDebugTarget.Position, AccentColor,
		/*Thickness=*/0.f, /*DepthPriority=*/SDPG_Foreground);
}
#endif
