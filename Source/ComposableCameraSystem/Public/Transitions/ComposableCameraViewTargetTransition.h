// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "ComposableCameraTransitionBase.h"
#include "ComposableCameraViewTargetTransition.generated.h"

/**
 * A transition that emulates the engine's built-in view-target blend curves.
 *
 * Created programmatically by the PCM's SetViewTarget override when external code
 * (engine CameraCut handler, gameplay Possess, SetViewTargetWithBlend, etc.) calls
 * SetViewTarget with non-zero FViewTargetTransitionParams. The transition delegates
 * blend-curve evaluation to FViewTargetTransitionParams::GetBlendAlpha(), so every
 * EViewTargetBlendFunction the engine supports is automatically available.
 *
 * This class is NOT meant to be placed in a transition data asset by designers.
 * It exists solely as the bridge between the engine's SetViewTarget blend params
 * and CCS's pose-only transition system.
 */
UCLASS(NotBlueprintable, Hidden, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraViewTargetTransition
	: public UComposableCameraTransitionBase
{
	GENERATED_BODY()

public:
	/** Initialize from engine transition params. Sets TransitionTime from BlendTime. */
	void InitFromViewTargetParams(const FViewTargetTransitionParams& InParams);

protected:
	virtual FComposableCameraPose OnEvaluate_Implementation(
		float DeltaTime,
		const FComposableCameraPose& CurrentSourcePose,
		const FComposableCameraPose& CurrentTargetPose) override;

private:
	/** The engine transition params. Stores blend function, exponent, and lock flags. */
	FViewTargetTransitionParams ViewTargetParams;
};
