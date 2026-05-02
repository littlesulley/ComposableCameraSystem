// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"

class AComposableCameraPlayerCameraManager;
class UCineCameraComponent;

/**
 * Viewport-size helpers for camera nodes that need to know the window / view
 * dimensions without hard-wiring a PlayerCameraManager dereference.
 *
 * Background: the original ScreenSpacePivot / ScreenSpaceConstraints nodes
 * computed aspect ratio as `PCM->GetOwningPlayerController()->GetViewportSize()`.
 * That path works for the PCM-driven runtime but falls over in the Level
 * Sequence component path, where there is no PCM. The PCM, however, isn't a
 * hard requirement for the underlying question — "what are the viewport
 * dimensions right now?" — because the engine already tracks that through
 * GEngine->GameViewport in game worlds and through GEditor->GetActiveViewport()
 * in editor worlds. This utility consolidates the resolution chain so node
 * code can just ask and doesn't need to carry the decision tree itself.
 *
 * Resolution order (first source that returns a valid size wins):
 *   1. PCM → PlayerController → viewport (legacy / multiplayer-aware; honors
 *      split-screen per-player viewports).
 *   2. GEngine->GameViewport (game worlds: PIE, standalone, packaged).
 *   3. GEditor->GetActiveViewport() in WITH_EDITOR builds (editor preview of
 *      LS Spawnables, piloted actors in the level editor).
 *   4. A 1920×1080 sentinel last-resort fallback so math never divides by
 *      zero. NaNs are worse than a slightly-wrong aspect ratio.
 */
namespace UE::ComposableCameras
{
	/**
	 * Get the effective viewport size in pixels. Returns true when the value
	 * came from a real source (PCM / GameViewport / editor viewport); false
	 * when OutSize is the fallback 1920×1080. Callers that need a different
	 * fallback behavior can branch on the return value.
	 */
	COMPOSABLECAMERASYSTEM_API bool TryGetEffectiveViewportSize(
		const AComposableCameraPlayerCameraManager* OptionalPCM,
		FIntPoint& OutSize);

	/**
	 * Convenience wrapper — returns aspect ratio (width / height) from the
	 * resolved viewport size. Always returns a finite positive number; falls
	 * back to 16:9 if no real source is available.
	 */
	COMPOSABLECAMERASYSTEM_API float GetEffectiveViewportAspectRatio(
		const AComposableCameraPlayerCameraManager* OptionalPCM);

	/**
	 * Resolve the effective render aspect for a specific `UCineCameraComponent`
	 * — what the renderer ACTUALLY uses, not just the viewport raw aspect.
	 *
	 *   - `CineCam->bConstrainAspectRatio == true`  → return `CineCam->AspectRatio`
	 *     (filmback-derived). Renderer letterboxes to this aspect regardless
	 *     of the viewport's actual shape, so the solver should match.
	 *   - `CineCam->bConstrainAspectRatio == false` → return the actual
	 *     viewport aspect (PCM / GameViewport / editor viewport via
	 *     `TryGetEffectiveViewportSize`). Renderer adapts to viewport, so
	 *     the solver tracks real-time when designers resize the level
	 *     viewport.
	 *
	 * Used by the Composition Solver via `FShotSolveContext::ViewportAspectRatio`
	 * so anchor screen positions match between the Shot Editor preview and
	 * LS playback regardless of CineCam constraint state. Falls back to
	 * `GetEffectiveViewportAspectRatio` when `CineCam` is null.
	 */
	COMPOSABLECAMERASYSTEM_API float GetEffectiveAspectRatioForCineCamera(
		const UCineCameraComponent* CineCam,
		const AComposableCameraPlayerCameraManager* OptionalPCM = nullptr);
}
