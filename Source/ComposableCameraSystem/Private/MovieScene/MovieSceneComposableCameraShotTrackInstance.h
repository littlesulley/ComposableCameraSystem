// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "EntitySystem/TrackInstance/MovieSceneTrackInstance.h"
#include "UObject/ObjectMacros.h"

#include "MovieSceneComposableCameraShotTrackInstance.generated.h"

/**
 * Per-section TrackInstance for `UMovieSceneComposableCameraShotTrack` sections - Phase E of Shot-Based Keyframing.
 *
 * Sole responsibility: forward each in-range section's resolved Shot data to
 * the bound `AComposableCameraLevelSequenceActor`'s
 * `UComposableCameraLevelSequenceComponent` as a Shot override entry. The LS
 * Component owns the top-row-winner pick and the CompositionFramingNode
 * write-through (see `UComposableCameraLevelSequenceComponent::ApplyActiveSequencerShotOverride`).
 *
 * Lifecycle:
 *
 *   OnAnimate      ->for each in-range section: walk to parent track, find
 *                    the binding GUID via `UMovieSceneTrack::FindObjectBindingGuid`,
 *                    resolve to the LS Actor + LS Component, resolve the
 *                    section's active Shot, push (Section, Shot, RowIndex)
 *                    to the LS Component.
 *   OnInputRemoved ->resolve LS Component, call `RemoveSequencerShotOverride`.
 *   OnDestroyed    ->walk every input, call `RemoveSequencerShotOverride` - defensive teardown for hot-reload / linker shutdown.
 *
 * Modeled on `UMovieSceneComposableCameraPatchTrackInstance` minus the
 * envelope / parameter-block logic -Shot tracks are pure hard-cut data
 * push in V1.
 */
UCLASS()
class UMovieSceneComposableCameraShotTrackInstance : public UMovieSceneTrackInstance
{
	GENERATED_BODY()

private:
	virtual void OnAnimate() override;
	virtual void OnInputRemoved(const FMovieSceneTrackInstanceInput& InInput) override;
	virtual void OnDestroyed() override;
};
