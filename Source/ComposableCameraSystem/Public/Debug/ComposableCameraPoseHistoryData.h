// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * One frame's worth of pose snapshot captured by the PCM for the debug
 * panel's "Pose History" sparklines + scrub tooltip.
 *
 * Deliberately narrower than `FComposableCameraPose`: we only keep the
 * fields the sparkline rows and tooltip display. Skipping
 * `FPostProcessSettings` matters because it embeds `TObjectPtr<UTexture>`
 * references. And the history ring buffer is NOT a `UPROPERTY`, so any
 * UObject refs it held would escape GC tracking. Same GC-safety pattern
 * as `UComposableCameraTransitionBase::FTransitionDebugSnapshot`.
 *
 * ~48 bytes per entry x 120-entry capacity = ~6 KB of ring memory per
 * PCM. Negligible.
 */
struct FComposableCameraPoseHistoryEntry
{
	/** World position at capture. */
	FVector Position = FVector::ZeroVector;

	/** Rotation at capture (Pitch / Yaw / Roll). */
	FRotator Rotation = FRotator::ZeroRotator;

	/** Resolved FOV in degrees (via `FComposableCameraPose::GetEffectiveFieldOfView`). */
	float FOVDegrees = 90.f;

	/** Game-world time in seconds at capture (`UWorld::GetTimeSeconds`).
	 *  Pauses with the game so the timeline doesn't drift while paused - that's the semantic users expect when scrubbing history. */
	float GameTime = 0.f;

	/** Active-context name at capture. Used both for the context-switch
	 *  marker strip (vertical line across sparklines whenever this changes
	 *  between adjacent entries) and for the hover tooltip. */
	FName ContextName = NAME_None;
};
