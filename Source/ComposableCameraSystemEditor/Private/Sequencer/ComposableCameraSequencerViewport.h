// Copyright 2026 Sulley. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class ISequencer;
struct FSequencerInitParams;

namespace ComposableCameraSystem::SequencerViewport
{
// Runs before Sequencer can capture a persisted, rolled editor view as its restore pose.
void PrepareForOpen(const FSequencerInitParams& Params);

// Runs before BroadcastEditorClose lets Level viewports save their current camera.
// The supplied list is in creation order and must exclude already-closed Sequencers.
void RestoreBeforeShutdown(TConstArrayView<TWeakPtr<ISequencer>> Sequencers);
}
