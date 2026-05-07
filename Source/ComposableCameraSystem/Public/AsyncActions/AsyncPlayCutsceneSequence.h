// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "MovieSceneSequencePlaybackSettings.h"
#include "AsyncPlayCutsceneSequence.generated.h"

class ULevelSequence;
class ULevelSequencePlayer;
class ALevelSequenceActor;
class UComposableCameraTransitionDataAsset;
class AComposableCameraPlayerCameraManager;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCutsceneSequenceFinished);

/**
 * Action that plays a level sequence as a CCS cutscene.
 *
 * This is the high-level entry point for Level Sequence integration.
 * It handles:
 *   1. Pushing a cutscene context (inter-context transition from gameplay).
 *   2. Starting ULevelSequencePlayer playback with user-provided settings.
 *   3. Popping the cutscene context when the LS ends (or is manually stopped).
 *
 * Camera cuts within the LS are handled by the engine's CameraCut track, which
 * calls SetViewTarget on the PCM at each section boundary. The PCM's overridden
 * SetViewTarget creates transient proxy cameras for each LS camera actor and
 * activates them in the cutscene context's director with CCS transitions
 * converted from FViewTargetTransitionParams (implicit camera activation).
 *
 * Blueprint usage:
 *   The "Play Cutscene Sequence" K2 node (UK2Node_PlayCutsceneSequence)
 *   provides a "Cutscene Action" output pin and an "On Finished" exec pin.
 *   Cache the Cutscene Action to call StopCutsceneSequence() later.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UAsyncPlayCutsceneSequence
	: public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	/**
	 * Create the action and register it with the game instance. Does NOT call
	 * Activate() — the K2 node's ExpandNode handles that after delegate binding.
	 *
	 * This is a plain C++ static, not a UFUNCTION. The Blueprint entry point is
	 * UComposableCameraBlueprintLibrary::PlayCutsceneSequence, which the K2 node
	 * calls via ExpandNode.
	 */
	static UAsyncPlayCutsceneSequence* Create(
		UObject* WorldContextObject,
		ULevelSequence* InLevelSequence,
		FName ContextName,
		UComposableCameraTransitionDataAsset* InEnterTransition,
		FMovieSceneSequencePlaybackSettings PlaybackSettings);

	/**
	 * Stop the cutscene, pop the cutscene context, and clean up all resources.
	 * Triggers an inter-context transition back to gameplay using ExitTransition.
	 *
	 * @param ExitTransition Optional transition for the context pop back to gameplay.
	 *        If nullptr, falls back to the resume camera's default enter transition.
	 */
	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|LevelSequence")
	void StopCutsceneSequence(UComposableCameraTransitionDataAsset* ExitTransition = nullptr);

	/** Fires when the level sequence finishes playing (not fired on infinite loop or manual stop). */
	UPROPERTY(BlueprintAssignable, Category = "Async Action")
	FOnCutsceneSequenceFinished OnFinished;

	// UBlueprintAsyncActionBase interface.
	virtual void Activate() override;

private:
	/** Called when the LS player reports playback finished. */
	UFUNCTION()
	void OnSequenceFinished();

	/** Internal cleanup: pop context, destroy player. */
	void CleanUp(UComposableCameraTransitionDataAsset* PopTransition = nullptr);

private:
	UPROPERTY(Transient)
	TObjectPtr<ULevelSequence> LevelSequence;

	UPROPERTY(Transient)
	TObjectPtr<ULevelSequencePlayer> SequencePlayer;

	UPROPERTY(Transient)
	TObjectPtr<ALevelSequenceActor> SequenceActor;

	UPROPERTY(Transient)
	TObjectPtr<UComposableCameraTransitionDataAsset> EnterTransition;

	UPROPERTY(Transient)
	TWeakObjectPtr<AComposableCameraPlayerCameraManager> CachedPCM;

	/** Cached from the WorldContextObject in the factory — GetWorld() on the base class is unreliable. */
	TWeakObjectPtr<UWorld> CachedWorld;

	FName CutsceneContextName;
	FMovieSceneSequencePlaybackSettings CachedPlaybackSettings;
	bool bIsActive { false };
};
