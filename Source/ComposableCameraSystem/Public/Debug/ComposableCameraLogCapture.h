// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Logging/LogVerbosity.h"
#include "Misc/OutputDevice.h"

/**
 * A single captured warning / error log line. Produced by
 * `FComposableCameraLogCapture` every time `UE_LOG` fires on a
 * ComposableCamera log category at Warning verbosity or worse.
 */
struct FComposableCameraLogEntry
{
	/** Log category name that emitted the line. Used so the panel can
	 *  distinguish runtime (`LogComposableCameraSystem`) from editor
	 *  (`LogComposableCameraSystemEditor`) origins at a glance. */
	FName CategoryName;

	/** Verbosity level (Warning / Error / Fatal). Filtered at capture
	 *  time — Log / Display / Verbose / VeryVerbose never enter the ring
	 *  buffer so the panel doesn't drown in trivia. */
	ELogVerbosity::Type Verbosity = ELogVerbosity::NoLogging;

	/** Formatted message body (after `UE_LOG`'s printf-style substitution). */
	FString Message;

	/** Wall-clock time of capture, from `FPlatformTime::Seconds()`. The
	 *  panel displays `(Now - Timestamp)` as "Xs ago" so users can tell
	 *  stale warnings from fresh ones. */
	double Timestamp = 0.0;
	
	/** Number of times the same (Category, Verbosity, Message) triple
	 *  has been emitted since it first entered the ring buffer. Starts
	 *  at 1 on first capture; bumped by the dedupe path in
	 *  `FComposableCameraLogCapture::Serialize` without moving the
	 *  entry or allocating. Panel renders "(xN)" when N > 1 so callers
	 *  see at a glance that a warning is spamming. */
	int32 RepeatCount = 1;
};

/**
 * Output device that watches `GLog` for warnings and errors emitted on
 * any `LogComposableCamera*` log category and keeps the most recent N in
 * a ring buffer for the debug panel's Warnings region to display.
 *
 * Why this is needed: several non-fatal error paths in the runtime
 * (running-camera null, referenced-director destroyed mid-blend, spline
 * transition missing its rail actor, etc.) emit `UE_LOG(..., Error, ...)`
 * but are only visible in Output Log. Users running PIE without Output
 * Log open would miss them entirely. Mirroring them into the panel
 * surfaces the signal in the same place the rest of the debug state
 * lives.
 *
 * Thread safety: `Serialize` is called from whichever thread did the
 * `UE_LOG`. The ring buffer is guarded by a CriticalSection, so the
 * panel's game-thread read and any worker-thread write serialize
 * cleanly. Capacity is small (16) so the critical section is never
 * held long enough to matter.
 *
 * Compiled out in shipping builds — both Install/Uninstall and the
 * accessor are `#if !UE_BUILD_SHIPPING`, so shipping games pay zero
 * cost (no output device registered, no ring buffer memory, no lock).
 */
class COMPOSABLECAMERASYSTEM_API FComposableCameraLogCapture : public FOutputDevice
{
public:
	/** Register with `GLog`. Idempotent — calling twice is a no-op. */
	static void Install();

	/** Unregister from `GLog` and clear the ring buffer. Idempotent. */
	static void Uninstall();

	/** Copy the current ring-buffer contents into `OutEntries`, oldest
	 *  first. Safe to call from any thread; blocks briefly on the
	 *  ring-buffer critical section but never for more than a few μs. */
	static void GetRecentEntries(TArray<FComposableCameraLogEntry>& OutEntries);

	/** Max entries the ring buffer keeps. Older entries overflow off the
	 *  front. Exposed so the panel's height pass can reserve rows up front. */
	static constexpr int32 MaxCapturedEntries = 16;

	// FOutputDevice interface.
	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override;
	virtual bool CanBeUsedOnAnyThread()    const override { return true; }
	virtual bool CanBeUsedOnMultipleThreads() const override { return true; }

private:
#if !UE_BUILD_SHIPPING
	/** Returns the shared singleton instance — keyed off static local so
	 *  the first call installs and lifetime ends with module shutdown.
	 *  Not exposed publicly: everything users want goes through the
	 *  static Install / Uninstall / GetRecentEntries surface above. */
	static FComposableCameraLogCapture& Get();

	TArray<FComposableCameraLogEntry> RingBuffer;
	FCriticalSection                  BufferCS;
	bool                              bInstalled = false;
#endif
};
