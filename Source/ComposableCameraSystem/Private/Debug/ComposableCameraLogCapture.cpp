// Copyright 2026 Sulley. All Rights Reserved.

#include "Debug/ComposableCameraLogCapture.h"

#include "HAL/PlatformTime.h"
#include "Logging/LogMacros.h"
#include "Misc/CoreMisc.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/ScopeLock.h"

// The Serialize() implementation is intentionally NOT guarded by
// !UE_BUILD_SHIPPING: even in shipping, an output device that's been
// registered somehow (should never happen with the Install guard) should
// fail safely. The body early-outs if no installation happened.

void FComposableCameraLogCapture::Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category)
{
#if !UE_BUILD_SHIPPING
	// Filter 1: verbosity. ELogVerbosity values are ordered so that lower
	// numbers are MORE severe (Fatal=0, Error=1, Warning=2, ...). Keep
	// anything at Warning or worse.
	const ELogVerbosity::Type EffectiveVerbosity = static_cast<ELogVerbosity::Type>(Verbosity & ELogVerbosity::VerbosityMask);
	if (EffectiveVerbosity > ELogVerbosity::Warning)
	{
		return;
	}

	// Filter 2: category. FName equality is a uint32 compare on the name
	// pool index. Fast enough to run on every single engine log line
	// (`Serialize` is called for ALL `UE_LOG` invocations, not just
	// CCS ones). Prior version used `ToString().StartsWith(...)` which
	// allocated an FString per call. Unacceptable for a hot path that
	// fires thousands of times per second.
	//
	// Trade-off: this explicit-list approach requires updating the
	// filter when a new `LogComposableCamera*` category is added. The
	// two current categories cover runtime + editor; any additions
	// should extend this conditional.
	static const FName NAME_LogComposableCameraSystem      (TEXT("LogComposableCameraSystem"));
	static const FName NAME_LogComposableCameraSystemEditor(TEXT("LogComposableCameraSystemEditor"));
	if (Category != NAME_LogComposableCameraSystem
	 && Category != NAME_LogComposableCameraSystemEditor)
	{
		return;
	}

	FComposableCameraLogEntry Entry;
	Entry.CategoryName = Category;
	Entry.Verbosity    = EffectiveVerbosity;
	Entry.Message      = V ? FString(V) : FString();
	Entry.Timestamp    = FPlatformTime::Seconds();

	FScopeLock Lock(&BufferCS);

	// Dedupe: identical (Category, Verbosity, Message) triples collapse
	// into the existing entry. Refresh its timestamp + bump repeat
	// counter, keep its position. Linear scan is O(N) with N <= 16;
	// we only reach here AFTER verbosity + category filters, so the
	// path is cold in steady state.
	for (FComposableCameraLogEntry& Existing : RingBuffer)
	{
		if (Existing.Verbosity    == Entry.Verbosity
		 && Existing.CategoryName == Entry.CategoryName
		 && Existing.Message      == Entry.Message)
		{
			Existing.Timestamp = Entry.Timestamp;
			++Existing.RepeatCount;
			return;
		}
	}

	if (RingBuffer.Num() >= MaxCapturedEntries)
	{
		RingBuffer.RemoveAt(0, 1, EAllowShrinking::No);
	}
	RingBuffer.Add(MoveTemp(Entry));
#endif
}

#if !UE_BUILD_SHIPPING

FComposableCameraLogCapture& FComposableCameraLogCapture::Get()
{
	// Meyers singleton. Thread-safe under C++11 initialization rules.
	// The buffer lives with the instance; module shutdown calls Uninstall
	// which clears the ring buffer but does NOT destroy the singleton
	// (destruction happens at program exit, when GLog is about to tear
	// down anyway).
	static FComposableCameraLogCapture Instance;
	return Instance;
}

void FComposableCameraLogCapture::Install()
{
	FComposableCameraLogCapture& Instance = Get();
	if (Instance.bInstalled)
	{
		return;
	}
	if (GLog)
	{
		GLog->AddOutputDevice(&Instance);
		Instance.bInstalled = true;
	}
}

void FComposableCameraLogCapture::Uninstall()
{
	FComposableCameraLogCapture& Instance = Get();
	if (!Instance.bInstalled)
	{
		return;
	}
	if (GLog)
	{
		GLog->RemoveOutputDevice(&Instance);
	}
	Instance.bInstalled = false;

	FScopeLock Lock(&Instance.BufferCS);
	Instance.RingBuffer.Reset();
}

void FComposableCameraLogCapture::GetRecentEntries(TArray<FComposableCameraLogEntry>& OutEntries)
{
	FComposableCameraLogCapture& Instance = Get();
	FScopeLock Lock(&Instance.BufferCS);
	OutEntries = Instance.RingBuffer;
}

#else

void FComposableCameraLogCapture::Install()
{
}

void FComposableCameraLogCapture::Uninstall()
{
}

void FComposableCameraLogCapture::GetRecentEntries(TArray<FComposableCameraLogEntry>& OutEntries)
{
	OutEntries.Reset();
}

#endif // !UE_BUILD_SHIPPING
