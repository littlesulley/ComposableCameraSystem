// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraTransitionBase.h"
#include "ComposableCameraEaseTransition.generated.h"

/**
 * EaseInOut transition.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraEaseTransition : public UComposableCameraTransitionBase
{
	GENERATED_BODY()

public:
	virtual FComposableCameraPose OnEvaluate_Implementation(float DeltaTime, const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose) override;

	// InterpEaseInOut with the authored `Exp`. Exposed so the debug
	// panel can preview exactly how steep the ease-in / ease-out shoulders
	// are for the current Exp value. Invaluable when tuning Exp by eye.
	virtual float GetBlendWeightAt(float NormalizedTime) const override;

#if !UE_BUILD_SHIPPING
	// Gated on `CCS.Debug.Viewport.Transitions.Ease`.
	virtual void DrawTransitionDebug(UWorld* World, bool bViewerIsOutsideCamera) const override;
#endif

public:
	// Exponential for EaseInOut transition.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition")
	float Exp { 1.f };
};
