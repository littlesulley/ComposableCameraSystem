// Copyright Sulley. All rights reserved.

#include "Patches/ComposableCameraPatchEnvelope.h"

namespace UE::ComposableCameras::PatchEnvelope
{
	float ApplyEase(EComposableCameraPatchEase Ease, float t)
	{
		t = FMath::Clamp(t, 0.f, 1.f);
		switch (Ease)
		{
			case EComposableCameraPatchEase::Linear:
				return t;
			case EComposableCameraPatchEase::EaseIn:
				return t * t;
			case EComposableCameraPatchEase::EaseOut:
				return 1.f - (1.f - t) * (1.f - t);
			case EComposableCameraPatchEase::EaseInOut:
				return t < 0.5f
					? 2.f * t * t
					: 1.f - 2.f * (1.f - t) * (1.f - t);
			case EComposableCameraPatchEase::Smooth:
				return t * t * (3.f - 2.f * t); // smoothstep
		}
		return t;
	}

	float ComputeStatelessAlpha(
		FFrameNumber CurrentFrame,
		FFrameNumber SectionStart,
		FFrameNumber SectionEnd,
		float EnterDurationSeconds,
		float ExitDurationSeconds,
		EComposableCameraPatchEase Ease,
		FFrameRate TickRate)
	{
		// Outside the section bounds ->no contribution.
		if (CurrentFrame < SectionStart || CurrentFrame >= SectionEnd)
		{
			return 0.f;
		}

		// Convert seconds ->ticks via the movie scene's tick resolution.
		// `* (double, FFrameRate)` is the canonical "X seconds in tick units"
		// conversion; FloorToFrame matches what the rest of the section /
		// channel path uses for time comparisons.
		const FFrameNumber EnterDurationFrames = (EnterDurationSeconds > 0.f)
			? (EnterDurationSeconds * TickRate).FloorToFrame()
			: FFrameNumber(0);
		const FFrameNumber ExitDurationFrames = (ExitDurationSeconds > 0.f)
			? (ExitDurationSeconds * TickRate).FloorToFrame()
			: FFrameNumber(0);

		// Resolve the enter / exit windows, scaling proportionally when their
		// sum exceeds the section length so EnterEnd == ExitStart (no overlap,
		// no discontinuity). The naive per-side `Min(Duration, SectionSize)`
		// clamp lets both windows occupy the same ticks. Which then triggers
		// the "first matching branch wins" preference below and produces a
		// step jump where Enter hands off to Exit. Proportional scaling
		// preserves the authored Enter:Exit ratio and yields a continuous
		// piecewise envelope even on too-short sections.
		const int32 SectionSizeTicks = (SectionEnd - SectionStart).Value;
		int32       EnterTicks       = EnterDurationFrames.Value;
		int32       ExitTicks        = ExitDurationFrames.Value;
		if (const int32 SumTicks = EnterTicks + ExitTicks; SumTicks > SectionSizeTicks && SumTicks > 0)
		{
			// Scale both sides; assign Exit by remainder so rounding never
			// leaves a one-tick gap or overlap (Enter+Exit == SectionSize exactly).
			EnterTicks = static_cast<int32>(
				(static_cast<int64>(EnterTicks) * SectionSizeTicks) / SumTicks);
			ExitTicks = SectionSizeTicks - EnterTicks;
		}

		const FFrameNumber EnterEnd  = SectionStart + EnterTicks;
		const FFrameNumber ExitStart = SectionEnd   - ExitTicks;

		// Enter ramp: 0 ->1 over [Start, Start + Enter]. Skip if EnterTicks=0.
		if (EnterTicks > 0 && CurrentFrame < EnterEnd)
		{
			const float t = static_cast<float>((CurrentFrame - SectionStart).Value) / static_cast<float>(EnterTicks);
			return ApplyEase(Ease, t);
		}

		// Exit ramp: 1 ->0 over [End - Exit, End). Skip if ExitTicks=0.
		if (ExitTicks > 0 && CurrentFrame >= ExitStart)
		{
			const float t = static_cast<float>((CurrentFrame - ExitStart).Value) / static_cast<float>(ExitTicks);
			return 1.f - ApplyEase(Ease, t);
		}

		// Inside the steady-state middle zone. Fully active.
		return 1.f;
	}
}
