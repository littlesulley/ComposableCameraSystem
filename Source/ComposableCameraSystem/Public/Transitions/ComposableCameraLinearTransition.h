// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraTransitionBase.h"
#include "ComposableCameraLinearTransition.generated.h"

/**
 * Linear transition.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraLinearTransition
	: public UComposableCameraTransitionBase
{
	GENERATED_BODY()

public:
	virtual FComposableCameraPose OnEvaluate_Implementation(float DeltaTime, const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose) override;

#if !UE_BUILD_SHIPPING
	// Gated on `CCS.Debug.Viewport.Transitions.Linear`. Paints only the
	// standard source / target / progress triplet. A pure linear blend has
	// no additional state worth visualizing.
	virtual void DrawTransitionDebug(UWorld* World, bool bViewerIsOutsideCamera) const override;
#endif
};
