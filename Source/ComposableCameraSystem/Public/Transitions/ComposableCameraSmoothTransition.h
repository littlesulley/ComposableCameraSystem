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

public:
	// Whether to use smoother step, a fifth-order polynomial algorithm for transition.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bSmootherStep;
};
