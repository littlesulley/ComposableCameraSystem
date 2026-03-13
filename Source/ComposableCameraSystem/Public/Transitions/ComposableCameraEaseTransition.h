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
	virtual FComposableCameraPose OnEvaluate_Implementation(float DeltaTime, const FComposableCameraPose& CurrentTargetPose) override;
	virtual FComposableCameraPose OnEvaluateBySource_Implementation(float DeltaTime, const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose) override;

public:
	// Exponential for EaseInOut transition.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Exp { 1.f };
};
