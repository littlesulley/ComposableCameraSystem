// Copyright 2026 Sulley. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Misc/FrameRate.h"
#include "Misc/FrameTime.h"
#include "MovieSceneFwd.h"

/** Sequence-time clock shared by draft and runtime instances. Never advances on a viewport tick. */
struct FComposableCameraInstanceTime
{
	struct FSample
	{
		float DeltaSeconds = 0.f;
		bool bReset = false;
		bool bChanged = false;
	};
	FSample Sample(FFrameTime Time, FFrameRate Rate, bool bDiscontinuity, bool bInvalidated)
	{
		FSample Result;
		Result.bReset = bInvalidated || !bHasTime || Time < Previous || (Time != Previous && bDiscontinuity);
		Result.bChanged = Result.bReset || Time != Previous;
		if (!Result.bReset) Result.DeltaSeconds = static_cast<float>((Time - Previous).AsDecimal() / Rate.AsDecimal());
		Previous = Time;
		bHasTime = true;
		return Result;
	}
	void Reset() { bHasTime = false; }
	static bool IsDiscontinuousStatus(EMovieScenePlayerStatus::Type Status)
	{
		return Status == EMovieScenePlayerStatus::Scrubbing || Status == EMovieScenePlayerStatus::Jumping
			|| Status == EMovieScenePlayerStatus::Stepping;
	}
private:
	FFrameTime Previous;
	bool bHasTime = false;
};
