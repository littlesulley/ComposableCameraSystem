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
		// Simple linear interpolation for predictable test output.
		FComposableCameraPose Result;
		Result.Position = FMath::Lerp(CurrentSourcePose.Position, CurrentTargetPose.Position, BlendFactor);
		Result.Rotation = FMath::Lerp(CurrentSourcePose.Rotation, CurrentTargetPose.Rotation, BlendFactor);
		Result.FieldOfView = FMath::Lerp(CurrentSourcePose.FieldOfView, CurrentTargetPose.FieldOfView, BlendFactor);
		return Result;
	}
};
