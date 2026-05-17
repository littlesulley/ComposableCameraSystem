// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraTransitionBase.h"
#include "ComposableCameraCubicTransition.generated.h"

/**
 * Cubic transition.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraCubicTransition
	: public UComposableCameraTransitionBase
{
	GENERATED_BODY()

public:
	virtual FComposableCameraPose OnEvaluate_Implementation(float DeltaTime, const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose) override;

	// `FMath::CubicInterp(0, 0, 1, 0, t)`. Matches OnEvaluate's curve
	// so the debug sparkline reads as the same cubic shape the camera
	// actually blends with.
	virtual float GetBlendWeightAt(float NormalizedTime) const override;

#if !UE_BUILD_SHIPPING
	// Gated on `CCS.Debug.Viewport.Transitions.Cubic`.
	virtual void DrawTransitionDebug(UWorld* World, bool bViewerIsOutsideCamera) const override;
#endif
};
