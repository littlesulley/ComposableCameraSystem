// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Transitions/ComposableCameraTransitionBase.h"
#include "ComposableCameraTestObjects.generated.h"

/**
 * A controllable transition for testing. Allows tests to manually set finished state
 * and control the blend output.
 */
UCLASS(Hidden, MinimalAPI)
class UComposableCameraTestTransition : public UComposableCameraTransitionBase
{
	GENERATED_BODY()

public:
	/** Manually mark the transition as finished. */
	void SetFinished(bool bInFinished)
	{
		bFinished = bInFinished;
	}

	/** The blend factor returned by this transition (0 = full source, 1 = full target). */
	float BlendFactor { 0.5f };

protected:
	virtual FComposableCameraPose OnEvaluate_Implementation(
		float DeltaTime,
		const FComposableCameraPose& CurrentSourcePose,
		const FComposableCameraPose& CurrentTargetPose) override
	{
		// Simple linear interpolation for predictable test output. Delegating to BlendBy
		// ensures we cover all pose fields (FOV, physical, projection) and respect the
		// "resolve FOV before blending" invariant.
		FComposableCameraPose Result = BlendPosesByLockedRotationPath(
			CurrentSourcePose,
			CurrentTargetPose,
			BlendFactor);
		return Result;
	}
};
