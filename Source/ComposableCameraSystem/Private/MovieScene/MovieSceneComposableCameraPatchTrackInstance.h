// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EntitySystem/TrackInstance/MovieSceneTrackInstance.h"
#include "UObject/ObjectMacros.h"

#include "MovieSceneComposableCameraPatchTrackInstance.generated.h"

/**
 * Per-section TrackInstance for UMovieSceneComposableCameraPatchTrack sections.
 *
 * Sole responsibility: forward each section's per-frame state to the bound
 * AComposableCameraLevelSequenceActor's UComposableCameraLevelSequenceComponent.
 * The LS Component owns evaluator caching, envelope application, and pose
 * projection. See `SetSequencerPatchOverlay` / `RemoveSequencerPatchOverlay`.
 *
 *   OnInputAdded->no-op (lazy spawn happens on first SetSequencerPatchOverlay).
 *   OnAnimate      ->for each in-range section: resolve TargetActorBinding -> *                    LS Component, sample channel-driven parameter block at
 *                    the input's current frame, compute envelope alpha
 *                    statelessly via PatchEnvelope::ComputeStatelessAlpha,
 *                    push (params, alpha) to LS Component.
 *   OnInputRemoved ->resolve LS Component, call RemoveSequencerPatchOverlay
 *                    so the cached evaluator + state are torn down cleanly.
 *   OnDestroyed    ->walk inputs, RemoveSequencerPatchOverlay each. Defensive
 *                    teardown for Sequencer hot-reload / linker shutdown.
 *
 * No PCM / Director / PatchManager involvement here. Patches added through
 * the BP library `AddCameraPatch(PlayerIndex, ContextName, ...)` go through
 * the gameplay PCM/Director path, which is a separate orthogonal surface.
 *
 * Modeled on UMovieSceneCVarTrackInstance. Same dispatch shape, same
 * UMovieSceneTrackInstanceSystem driver.
 */
UCLASS()
class UMovieSceneComposableCameraPatchTrackInstance : public UMovieSceneTrackInstance
{
	GENERATED_BODY()

private:
	virtual void OnAnimate() override;
	virtual void OnInputRemoved(const FMovieSceneTrackInstanceInput& InInput) override;
	virtual void OnDestroyed() override;
};
