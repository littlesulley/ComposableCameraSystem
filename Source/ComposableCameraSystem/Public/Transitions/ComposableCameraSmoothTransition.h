// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraTransitionBase.h"
#include "ComposableCameraSmoothTransition.generated.h"

/**
 * Smooth and smoother transition.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraSmoothTransition
	: public UComposableCameraTransitionBase
{
	GENERATED_BODY()

public:
	virtual FComposableCameraPose OnEvaluate_Implementation(float DeltaTime, const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose) override;

	// SmoothStep (3t - 2t) or SmootherStep (6t?- 15t?+ 10t),
	// selected by `bSmootherStep`. Matches the curve OnEvaluate applies
	// to BlendWeight every frame. The debug panel renders this exact
	// shape as a sparkline so the user can compare smooth vs smoother
	// visually before tweaking the flag.
	virtual float GetBlendWeightAt(float NormalizedTime) const override;

#if !UE_BUILD_SHIPPING
	// Gated on `CCS.Debug.Viewport.Transitions.Smooth`.
	virtual void DrawTransitionDebug(UWorld* World, bool bViewerIsOutsideCamera) const override;
#endif

public:
	// Whether to use smoother step, a fifth-order polynomial algorithm for transition.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition")
	bool bSmootherStep;
};
