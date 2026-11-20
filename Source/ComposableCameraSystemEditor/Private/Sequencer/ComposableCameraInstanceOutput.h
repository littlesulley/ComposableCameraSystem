// Copyright 2026 Sulley. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

class ISequencer;
class UMovieSceneComposableCameraInstanceTrack;

namespace ComposableCameraSystem::CameraInstanceOutput
{
	bool IsCreating();
	/** Read-only ownership check; never creates or reattaches an output. */
	bool HasOwnedOutput(TSharedRef<ISequencer> Sequencer, const UMovieSceneComposableCameraInstanceTrack& Track);
	/** Compatibility normalization on editor load/attachment. Native binding names own the label. */
	void ClearLegacyDisplayName(UMovieSceneComposableCameraInstanceTrack& Track);
	/** Restores missing legacy templates in place; creates missing/foreign outputs without stealing ownership. */
	bool Ensure(TSharedRef<ISequencer> Sequencer, UMovieSceneComposableCameraInstanceTrack& Track, FString& Error);
	/** Called inside the edit transaction. Retains outputs referenced by surviving user Cuts. */
	bool RemoveOrphans(TSharedRef<ISequencer> Sequencer);
}
