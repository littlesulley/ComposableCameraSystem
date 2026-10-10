// Copyright 2026 Sulley. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "DataAssets/ComposableCameraShot.h"

class ISequencer;
class UMovieSceneComposableCameraShotSection;
class UMovieScene;
struct FMovieSceneObjectBindingID;

namespace ComposableCameraSystem::ShotAuthoring
{
	/** Validate without creating bindings; Spawnables retain their sequence-relative identity. */
	bool ResolveSubjectBinding(ISequencer& Sequencer, AActor* Actor, bool bCreatePossessable,
		FMovieSceneObjectBindingID& Binding, FString& Reason);
	/** Read-only preflight. Existing Camera Cuts are never resized/rebound by creation. */
	bool CanCreateRange(const UMovieScene& MovieScene, const FGuid& CameraBinding,
		const TRange<FFrameNumber>& Range, FString& Reason);
	/** Extend only this camera's spawn coverage; existing spawn keys/ranges remain intact. */
	bool EnsureCameraSpawnRange(UMovieScene& MovieScene, const FGuid& CameraBinding, const TRange<FFrameNumber>& Range);
	UMovieSceneComposableCameraShotSection* CreateInSequence(TSharedRef<ISequencer> Sequencer,
		const FComposableCameraShot& Shot, const FText& Label, float DurationSeconds,
		bool bDialogue, bool bSpawnable, UMovieSceneComposableCameraShotSection* ReuseSource,
		FString& Reason);
	UMovieSceneComposableCameraShotSection* DuplicateInSequence(TSharedRef<ISequencer> Sequencer,
		UMovieSceneComposableCameraShotSection& Source, FString& Reason);
}
