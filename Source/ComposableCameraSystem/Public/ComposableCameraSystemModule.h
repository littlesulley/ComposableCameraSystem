// Copyright Sulley. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "Stats/Stats.h"

class FComposableCameraSystemModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

// COMPOSABLECAMERASYSTEM_API on the declaration is required so other modules
// (notably ComposableCameraSystemUncookedOnly's UK2Node_ActivateComposableCamera)
// can resolve the LogComposableCameraSystem symbol at link time. Without the
// export macro, DECLARE_LOG_CATEGORY_EXTERN declares an unqualified extern that
// only the runtime module's own DLL can resolve, producing LNK2001 in any
// dependent module that calls UE_LOG(LogComposableCameraSystem, ...).
COMPOSABLECAMERASYSTEM_API DECLARE_LOG_CATEGORY_EXTERN(LogComposableCameraSystem, Log, All);

// `stat CCS`. Per-frame cycle-counter group. Complements the
// TRACE_CPUPROFILER_EVENT_SCOPE markers that feed Unreal Insights: this group
// drives the in-viewport stat HUD with realtime numeric counters (sum of
// cycles spent in each scope this frame), which is the right surface for
// "is my change making tick cheaper / more expensive" at-a-glance checks.
// Insights remains the right tool for timeline / flame-graph analysis.
// Counters are declared per-cpp via `DECLARE_CYCLE_STAT(..., STATGROUP_CCS)`
// and sit next to the existing trace scopes.
DECLARE_STATS_GROUP(TEXT("ComposableCamera"), STATGROUP_CCS, STATCAT_Advanced);
