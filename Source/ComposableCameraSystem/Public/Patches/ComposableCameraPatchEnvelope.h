// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/FrameNumber.h"
#include "Misc/FrameRate.h"
#include "Patches/ComposableCameraPatchTypes.h"

namespace UE::ComposableCameras::PatchEnvelope
{
	/**
	 * Apply the 5-curve enum ease shape to a normalized time t in [0, 1].
	 * Returns f(t) in [0, 1].
	 *
	 * Pulled out of UComposableCameraPatchManager.cpp's anonymous namespace so
	 * the runtime stateful envelope (`AdvancePatchEnvelope` in PatchManager.cpp)
	 * and the editor-preview stateless envelope (`ComputeStatelessAlpha` below)
	 * agree on the curve shape to a single canonical implementation. Adding a
	 * new ease type means editing one switch instead of two.
	 */
	COMPOSABLECAMERASYSTEM_API float ApplyEase(EComposableCameraPatchEase Ease, float t);

	/**
	 * Stateless envelope alpha at a given frame, computed purely from time +
	 * section bounds + ease parameters. No Phase / ElapsedInPhase / ExitStartAlpha
	 * fields, no per-call mutation.
	 *
	 * Used by the Sequencer **editor preview** path (LS Component's TickComponent
	 * applies the result onto its InternalCamera's pose before projecting to
	 * the CineCamera). The runtime PIE path keeps the stateful machine on
	 * UComposableCameraPatchInstance because it needs to handle real-time
	 * Manual / Condition / OnCameraChange channels -Sequencer editor scrub
	 * doesn't have those. Computing alpha as a pure function of time also
	 * makes scrub-backwards correct: dragging the playhead to the section's
	 * exit window shows the fade-out, no matter how the user got there.
	 *
	 * Curve shape:
	 *   playhead < SectionStart                       ->0
	 *   playhead in [Start, Start + EnterDuration]    ->ease(t)        // ramp up
	 *   playhead in [Start + Enter, End - Exit]       ->1
	 *   playhead in [End - ExitDuration, End]         ->1 - ease(t)    // ramp down
	 *   playhead >= SectionEnd                        ->0
	 *
	 * EnterDuration / ExitDuration <=0 short-circuit (no ramp on that side).
	 *
	 * @param CurrentFrame          Playhead in tick units (the section / movie
	 *                              scene's tick resolution).
	 * @param SectionStart          Section's inclusive lower bound.
	 * @param SectionEnd            Section's exclusive upper bound.
	 * @param EnterDurationSeconds  Resolved enter duration (asset default OR
	 *                              overridden by Params; section easing is
	 *                              folded in upstream by the caller).
	 * @param ExitDurationSeconds   Same shape as EnterDurationSeconds.
	 * @param Ease                  Patch asset's authored ease type.
	 * @param TickRate              Owning movie scene's tick resolution; used
	 *                              to convert enter/exit seconds ->tick counts
	 *                              for the in-tick-space comparison.
	 */
	COMPOSABLECAMERASYSTEM_API float ComputeStatelessAlpha(
		FFrameNumber CurrentFrame,
		FFrameNumber SectionStart,
		FFrameNumber SectionEnd,
		float EnterDurationSeconds,
		float ExitDurationSeconds,
		EComposableCameraPatchEase Ease,
		FFrameRate TickRate);
}
