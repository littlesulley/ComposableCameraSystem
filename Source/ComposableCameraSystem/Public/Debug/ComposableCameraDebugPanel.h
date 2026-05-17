// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Runtime in-viewport debug panel for the Composable Camera System.
 *
 * Toggled via the `CCS.Debug.Panel 0|1` console variable. When enabled,
 * paints a multi-region HUD overlay showing, from top to bottom:
 *   1. Current Pose    . Position / rotation / FOV / aspect
 *   2. Context Stack & Evaluation Tree
 *   3. Running Camera  . Class, tag, lifetime, nodes, parameters, variables
 *   4. Actions
 *   5. Modifiers       . Count (phase 1)
 *
 * Rendering goes through UDebugDrawService's "Game" channel with a static
 * delegate registered at module startup. While disabled the draw function
 * only performs a CVar read and early-outs, so there is no meaningful cost.
 *
 * Data sources are the same public read-only accessors already used by
 * AComposableCameraPlayerCameraManager::DisplayDebug (showdebug camera).
 * The existing showdebug path is preserved. This panel is additive.
 *
 * Lifecycle is module-owned: FComposableCameraSystemModule::StartupModule
 * calls Initialize(); ShutdownModule calls Shutdown().
 */
class COMPOSABLECAMERASYSTEM_API FComposableCameraDebugPanel
{
public:
	/** Register the UDebugDrawService delegate. Idempotent. */
	static void Initialize();

	/** Unregister the UDebugDrawService delegate. Idempotent. */
	static void Shutdown();
};
