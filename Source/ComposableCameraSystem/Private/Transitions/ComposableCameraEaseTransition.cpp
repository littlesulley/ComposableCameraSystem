// Copyright 2026 Sulley. All Rights Reserved.

#include "Transitions/ComposableCameraEaseTransition.h"

#if !UE_BUILD_SHIPPING
#include "Debug/ComposableCameraDebugDrawSink.h"
#include "Debug/ComposableCameraViewportDebug.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "SceneManagement.h"   // SDPG_Foreground

namespace
{
	static TAutoConsoleVariable<int32> CVarShowEaseTransitionGizmo(
		TEXT("CCS.Debug.Viewport.Transitions.Ease"),
		0,
		TEXT("Show EaseTransition gizmo (source/target/progress in orange accent).\n")
		TEXT("Requires `CCS.Debug.Viewport 1`. Gizmo disappears when the transition finishes."),
		ECVF_Default);
}
#endif

FComposableCameraPose UComposableCameraEaseTransition::OnEvaluate_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose)
{
	float DurationPct = (GetTransitionTime() - GetRemainingTime()) / GetTransitionTime();
	float BlendWeight = FMath::InterpEaseInOut(0.f, 1.f, DurationPct, Exp);
	Percentage = BlendWeight;

	FComposableCameraPose CurrentPose = BlendPosesByLockedRotationPath(
		CurrentSourcePose,
		CurrentTargetPose,
		BlendWeight);

	return CurrentPose;
}

float UComposableCameraEaseTransition::GetBlendWeightAt(float NormalizedTime) const
{
	// Same InterpEaseInOut call OnEvaluate uses. Keeps the debug preview
	// in sync with any Exp tweak the user makes.
	return FMath::InterpEaseInOut(0.f, 1.f, FMath::Clamp(NormalizedTime, 0.f, 1.f), Exp);
}

#if !UE_BUILD_SHIPPING
void UComposableCameraEaseTransition::DrawTransitionDebug(FComposableCameraDebugDrawSink& Draw, bool bViewerIsOutsideCamera) const
{
	if (CVarShowEaseTransitionGizmo.GetValueOnGameThread() == 0
		&& !FComposableCameraViewportDebug::ShouldShowAllTransitionGizmos()
		&& !Draw.ShouldForceDrawAllTransitionGizmos()) { return; }

	// Burnt orange. Reads as "easing", also distinct from the
	// RelativeFixedPose node's orange (which sits on a different subsystem).
	const FColor AccentColor = FComposableCameraViewportDebugColors::TransitionEase();

	DrawStandardTransitionDebug(Draw, bViewerIsOutsideCamera, AccentColor);

	// Path is a straight line. Position lerps linearly in space. Only the
	// ease-in-out timing curve (driven by Exp) differs from Linear.
	Draw.DrawLine(LastDebugSource.Position, LastDebugTarget.Position, AccentColor,
		/*Thickness=*/0.f, /*DepthPriority=*/SDPG_Foreground);
}
#endif
