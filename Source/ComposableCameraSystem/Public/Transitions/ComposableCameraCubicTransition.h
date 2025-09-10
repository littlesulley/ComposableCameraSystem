// Copyright Sulley. All rights reserved.

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
	virtual FComposableCameraPose OnEvaluate(float DeltaTime, const FComposableCameraPose& CurrentTargetPose) override;

};
