// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EntitySystem/TrackInstance/MovieSceneTrackInstance.h"
#include "UObject/ObjectMacros.h"

#include "MovieSceneComposableCameraShotTrackInstance.generated.h"

/**
 * Per-section TrackInstance for `UMovieSceneComposableCameraShotTrack` sections.
 *
 * Sole responsibility: forward each in-range section's resolved Shot data to
 * the bound `AComposableCameraLevelSequenceActor`'s
 * `UComposableCameraLevelSequenceComponent` as a Shot override entry. The LS
 * Component owns the primary / secondary pick and the CompositionFramingNode
 * push (see `UComposableCameraLevelSequenceComponent::ApplyActiveSequencerShotOverride`).
 *
 * Lifecycle:
 *
 *   OnAnimate      ->for each in-range section: walk to parent track, find
 *                    the binding GUID via `UMovieSceneTrack::FindObjectBindingGuid`,
 *                    resolve to the LS Actor + LS Component, resolve the
 *                    section's active Shot, push (Section, Shot, RowIndex,
 *                    EnterTransition, BlendAlpha)
 *                    to the LS Component.
 *   OnInputRemoved ->resolve LS Component, call `RemoveSequencerShotOverride`.
 *   OnDestroyed    ->walk every input, call `RemoveSequencerShotOverride` - defensive teardown for hot-reload / linker shutdown.
 *
 * Modeled on `UMovieSceneComposableCameraPatchTrackInstance` minus the
 * envelope / parameter-block logic. Shot tracks push structured Shot data
 * plus overlap-blend metadata.
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
