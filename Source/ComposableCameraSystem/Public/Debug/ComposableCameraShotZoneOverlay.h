// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Cinemachine-style framing-zone overlay for the LS / PIE / Game viewport.
 *
 * Draws translucent dead/soft zone rectangles + the resolved anchor disc
 * for every active `UComposableCameraCompositionFramingNode` whose
 * `Aim.AimZones` (or `Placement.PlacementZones` under `AnchorAtScreen`)
 * has `bEnabled == true`. Mirrors the Shot Editor's preview overlay
 * (§D.4.1 of EditorDesignDoc) so designers see identical framing zones
 * in PIE / packaged play / Sequencer scrub as they did while authoring.
 *
 * Gating:
 *   - Master CVar: `CCS.Debug.Viewport.ShotZones 0|1` — when 0, nothing
 *     draws and the registered delegates skip in O(1).
 *   - Independent of the 3D-gizmo `CCS.Debug.Viewport` master and any
 *     per-node CVars. The two viewport-debug groups address different
 *     diagnostic needs (3D world-space gizmos vs. 2D screen-space
 *     framing-zone overlay) and shouldn't entangle.
 *
 * Rendering pathway: `UDebugDrawService::Register("Game" / "Editor", ...)`
 * — same dual-channel + per-(frame, FCanvas*) dedup pattern
 * `FComposableCameraDebugPanel` uses, so the overlay survives the F8
 * eject viewport swap and doesn't double-draw when one viewport
 * transiently has both ShowFlags set. Iterating
 * `UComposableCameraCompositionFramingNode::GetActiveInstances()` is
 * the source of truth for "which Shots are currently driving a
 * camera"; both the gameplay PCM path and the LS Component path
 * register identically because both end up running the same node.
 *
 * All cost is guarded `#if !UE_BUILD_SHIPPING` — overlays are an
 * authoring / debugging affordance, not a runtime feature.
 *
 * Lifecycle: module-owned. `FComposableCameraSystemModule::StartupModule`
 * calls `Initialize`; `ShutdownModule` calls `Shutdown`.
 */
class COMPOSABLECAMERASYSTEM_API FComposableCameraShotZoneOverlay
{
public:
	/** Register the dual-channel debug-draw delegates. Idempotent. */
	static void Initialize();

	/** Unregister the delegates. Idempotent. */
	static void Shutdown();
};
