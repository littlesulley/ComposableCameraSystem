// Copyright Sulley. All rights reserved.

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
	virtual FComposableCameraPose OnEvaluate(float DeltaTime, const FComposableCameraPose& CurrentTargetPose) override;
};
