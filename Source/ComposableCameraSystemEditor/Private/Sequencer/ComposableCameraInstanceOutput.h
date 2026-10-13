// Copyright 2026 Sulley. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

class ISequencer;
class UMovieSceneComposableCameraInstanceTrack;

namespace ComposableCameraSystem::CameraInstanceOutput
{
	bool IsCreating();
	/** Compatibility normalization on editor load/attachment. Native binding names own the label. */
	void ClearLegacyDisplayName(UMovieSceneComposableCameraInstanceTrack& Track);
	/** Creates only missing/foreign outputs. Never takes ownership of another instance's camera. */
	bool Ensure(TSharedRef<ISequencer> Sequencer, UMovieSceneComposableCameraInstanceTrack& Track, FString& Error);
	/** Called inside the edit transaction. Retains outputs referenced by surviving user Cuts. */
	bool RemoveOrphans(TSharedRef<ISequencer> Sequencer);
}
