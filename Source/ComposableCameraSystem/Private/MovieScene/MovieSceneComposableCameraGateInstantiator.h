// Copyright Sulley. All rights reserved.

#pragma once

#include "EntitySystem/MovieSceneEntityInstantiatorSystem.h"
#include "EntitySystem/MovieSceneSequenceInstanceHandle.h"
#include "UObject/ObjectMacros.h"
#include "UObject/WeakObjectPtr.h"

#include "MovieSceneComposableCameraGateInstantiator.generated.h"

class UComposableCameraLevelSequenceComponent;

/**
 * ECS instantiator system that drives cut/blend-only tick gating for
 * UComposableCameraLevelSequenceComponent.
 *
 * Shape inspired by Epic's UMovieSceneCameraParameterInstantiator
 * (Engine/Plugins/Cameras/GameplayCameras/Private/MovieScene/), but with a
 * simpler attachment story: instead of tagging entities through a section
 * decorator, we key RelevantComponent on the engine's built-in
 * FBuiltInComponentTypes::SpawnableBinding, so every Spawn Section (legacy
 * FMovieSceneSpawnable or UE 5.5+ custom UMovieSceneSpawnableBindingBase)
 * is reachable without any editor hook or saved-asset migration.
 *
 * Job per frame:
 *   1. Track the set of (Spawnable → LS component) entities — populated on
 *      NeedsLink, torn down on NeedsUnlink. Non-CCS Spawnables are resolved
 *      and dropped in the lazy-resolve pass so they don't waste work.
 *   2. Query the currently-active Camera Cut Track sections via the engine's
 *      UMovieSceneTrackInstanceInstantiator, determine which bindings are the
 *      current cut target or a blend participant this frame (walking each
 *      input section's ease windows to recover same-row-truncated partners
 *      the engine has hidden from GetInputs()).
 *   3. SetEvaluationEnabled(true) on components whose owning actor is in that
 *      active set; SetEvaluationEnabled(false) on everyone else tracked.
 *
 * Runs in the instantiation phase. The Camera Cut Track instance is visible
 * to us because UMovieSceneTrackInstanceInstantiator::OnEndUpdateInputs has
 * already sorted its inputs by the time our OnRun fires, so we read a
 * stable section-range / context-time view for this frame.
 */
UCLASS(MinimalAPI)
class UMovieSceneComposableCameraGateInstantiator : public UMovieSceneEntityInstantiatorSystem
{
	GENERATED_BODY()

public:
	UMovieSceneComposableCameraGateInstantiator(const FObjectInitializer& ObjInit);

	/** One tracked Spawnable — binding + resolved LS component. The component
	 *  pointer is resolved lazily because the Spawnables system may run after
	 *  our first OnRun; a null pointer just means "retry next frame".
	 *  Public so the free-function task bodies in the .cpp can name it. */
	struct FGateEntry
	{
		UE::MovieScene::FRootInstanceHandle RootInstanceHandle;
		FGuid BindingGuid;
		TWeakObjectPtr<UComposableCameraLevelSequenceComponent> Component;
	};

	using FGateKey = TTuple<UE::MovieScene::FRootInstanceHandle, FGuid>;

private:
	virtual void OnLink() override;
	virtual void OnUnlink() override;
	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override final;

	/** Registry keyed by (root instance handle, binding GUID). Populated on
	 *  NeedsLink, pruned on NeedsUnlink. Entries may have a null component
	 *  pointer until the Spawnables system finishes spawning the actor — we
	 *  re-resolve each frame in OnRun until the actor appears, or drop the
	 *  entry if the resolved actor isn't an AComposableCameraLevelSequenceActor. */
	TMap<FGateKey, FGateEntry> TrackedComponents;
};
