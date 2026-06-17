// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"

struct FComposableCameraViewportDebugLegendEntry
{
	const TCHAR* Label = nullptr;
	FColor Color = FColor::White;
	const TCHAR* CVarName = nullptr;
	bool bIsTransition = false;
};

struct FComposableCameraViewportDebugColors
{
	static FLinearColor ToLinearColor(const FColor& Color)
	{
		return FLinearColor(
			static_cast<float>(Color.R) / 255.f,
			static_cast<float>(Color.G) / 255.f,
			static_cast<float>(Color.B) / 255.f,
			static_cast<float>(Color.A) / 255.f);
	}

	static FColor SourcePose() { return FColor(80, 220, 120); }
	static FColor TargetPose() { return FColor(80, 170, 255); }

	static FColor TransitionLinear() { return FColor(200, 200, 200); }
	static FColor TransitionSmooth() { return FColor(255, 220, 100); }
	static FColor TransitionEase() { return FColor(255, 160, 80); }
	static FColor TransitionCubic() { return FColor(180, 130, 255); }
	static FColor TransitionInertialized() { return FColor(255, 100, 200); }
	static FColor TransitionCylindrical() { return FColor(100, 230, 200); }
	static FColor TransitionSpline() { return FColor(140, 200, 255); }
	static FColor TransitionPathGuided() { return FColor(255, 130, 130); }
	static FColor TransitionDynamicDeocclusion() { return FColor(255, 90, 90); }
	static FColor TransitionCompositionPreserving() { return FColor(80, 220, 210); }

	static FColor PivotOffset() { return FColor::Yellow; }
	static FColor LockOnAimPoint() { return FColor(80, 160, 255); }
	static FColor PivotDamping() { return FColor(255, 0, 255); }
	static FColor LookAt() { return FColor(30, 200, 255); }
	static FColor CollisionPushClear() { return FColor(80, 255, 120); }
	static FColor CollisionPushBlocked() { return FColor(255, 80, 80); }
	static FColor CollisionPushHit() { return FColor(255, 0, 0); }
	static FColor CollisionPushSelf() { return FColor(80, 200, 255); }
	static FColor OcclusionFadeSweep() { return FColor(255, 80, 80); }
	static FColor OcclusionFadeProximity() { return FColor(80, 200, 255); }
	static FColor VolumeConstraintClear() { return FColor(90, 255, 120); }
	static FColor VolumeConstraintClamping() { return FColor(255, 90, 90); }
	static FColor FocusPull() { return FColor(255, 200, 60); }
	static FColor HitchcockZoom() { return FColor(180, 80, 220); }
	static FColor SplineNode() { return FColor(170, 120, 255); }
	static FColor SpiralNode() { return FColor(255, 150, 60); }
	static FColor SpiralPivot() { return FColor::Yellow; }
	static FColor ReceivePivotActor() { return FColor::White; }
	static FColor RelativeFixedPose() { return FColor(255, 140, 0); }
	static FColor ScreenSpacePivot() { return FColor(80, 200, 180); }
	static FColor ScreenSpaceConstraints() { return FColor(255, 180, 220); }
	static FColor PivotLookAhead() { return FColor(255, 128, 0); }
	static FColor CompositionFramingPlacement() { return FColor(255, 130, 30); }
	static FColor CompositionFramingAim() { return FColor(80, 200, 255); }
	static FColor CompositionFramingTarget() { return FColor::White; }
};

/**
 * Runtime 3D viewport debug draw for the Composable Camera System.
 *
 * Two-tier gating:
 *  - `CCS.Debug.Viewport 0|1` is the master switch. When 0, nothing draws.
 *    When 1, the camera's FRUSTUM is drawn (with the F8 gate below) and
 *    each node is given a chance to paint its own per-node gizmo.
 *  - Per-node gizmos are controlled by PER-NODE CVars of the form
 *    `CCS.Debug.Viewport.<NodeName>` (e.g. `.PivotOffset`, `.LookAt`,
 *    `.CollisionPush`, `.Spline`, `.PivotDamping`). Each defaults to 0 - users opt in per node. These gizmos are visible in BOTH possessed
 *    play and F8 eject, because they rarely occlude the viewpoint.
 *
 * Frustum auto-hide. The frustum is the one exception. Drawing it at the
 * near plane while the player is viewing through the camera just occludes
 * the scene. The frustum therefore only fires while
 * `GEditor->bIsSimulatingInEditor` is true (F8 eject / Simulate mode).
 * Non-editor builds always show it when the master CVar is on.
 * `CCS.Debug.Viewport.AlwaysShow 1` forces frustum rendering even while
 * possessing. Useful for multi-viewport setups.
 *
 * Rendering pathway: the draw runs from an `FTSTicker::GetCoreTicker()`
 * delegate, not from `UDebugDrawService`. The ticker fires every frame
 * regardless of which viewport is active, and `DrawDebugCamera` routes
 * through the world's LineBatcher, which is rendered by every viewport
 * that draws that world. So the draw is visible both in the game
 * viewport (standalone / possessed play) and in the editor viewport
 * (during F8 eject). An earlier attempt used
 * `UDebugDrawService::Register("Game", ...)` but that hook does NOT fire
 * from the editor viewport during F8 eject, which was the exact time we
 * most wanted draws to appear.
 *
 * Adding a new per-node gizmo is a localised ~15-line job:
 *  1. Override `UComposableCameraCameraNodeBase::DrawNodeDebug(FComposableCameraDebugDrawSink&, bool)`
 *     in the concrete node, guarded `#if !UE_BUILD_SHIPPING`. The second
 *     parameter is `bViewerIsOutsideCamera`. Use it to gate any gizmo that
 *     sits AT the camera's own position (see `CollisionPushNode`'s self-
 *     collision sphere); most nodes ignore it.
 *  2. Declare a static `TAutoConsoleVariable<int32>` in the node's .cpp
 *     under `CCS.Debug.Viewport.<NodeName>`, default 0.
 *  3. Early-out on that CVar at the top of `DrawNodeDebug`.
 *  4. Emit primitives through `FComposableCameraDebugDrawSink` with the
 *     node's resolved runtime state. Use
 *     `FComposableCameraViewportDebugColors` for any shared legend color.
 *
 * This is distinct from `FComposableCameraDebugPanel` (2D HUD overlay,
 * `CCS.Debug.Panel` CVar). They are independent and can be enabled in
 * any combination.
 *
 * All cost is guarded `#if !UE_BUILD_SHIPPING`. The ticker's body compiles
 * to nothing in shipping builds.
 *
 * Lifecycle is module-owned: FComposableCameraSystemModule::StartupModule
 * calls Initialize(); ShutdownModule calls Shutdown().
 */
class COMPOSABLECAMERASYSTEM_API FComposableCameraViewportDebug
{
public:
	/** Register the FTSTicker delegate. Idempotent. */
	static void Initialize();

	/** Unregister the FTSTicker delegate. Idempotent. */
	static void Shutdown();

	/**
	 * True when `CCS.Debug.Viewport.Nodes.All` is non-zero. Every
	 * per-node gizmo (both 3D `DrawNodeDebug` and 2D `DrawNodeDebug2D`
	 * paths) should show regardless of its own per-node CVar. The two
	 * paths share this switch intentionally: each node's 2D / 3D pieces
	 * already share one per-node CVar, so batching them into one "All"
	 * toggle keeps the mental model consistent.
	 *
	 * Callsite idiom:
	 *
	 *     if (CVarShowMyNodeGizmo.GetValueOnGameThread() == 0 &&
	 *         !FComposableCameraViewportDebug::ShouldShowAllNodeGizmos())
	 *     {
	 *         return;
	 *     }
	 *
	 * OR semantics: if either the per-node CVar OR the All CVar is on,
	 * the gizmo draws. No "except" subtraction; users wanting granularity
	 * should leave All off and enable per-node CVars individually.
	 */
	static bool ShouldShowAllNodeGizmos();

	/**
	 * True when `CCS.Debug.Viewport.Transitions.All` is non-zero. Every
	 * per-transition gizmo draws regardless of its own CVar. Same OR
	 * semantics as ShouldShowAllNodeGizmos.
	 */
	static bool ShouldShowAllTransitionGizmos();

	/** Shared legend metadata. Used by the panel and automation tests so
	 *  label swatches come from the same color constants as 3D gizmos. */
	static TConstArrayView<FComposableCameraViewportDebugLegendEntry> GetLegendEntries();

#if !UE_BUILD_SHIPPING
	/** Lifetime used for per-frame sphere labels. Zero prevents stale HUD text. */
	static float GetSphereLabelDurationSeconds();

	/**
	 * Draw a translucent-wireframe debug sphere. The canonical sphere
	 * gizmo used by every CCS node / transition debug override.
	 *
	 * NOTE on the name: "Solid" is a historical artifact. An earlier
	 * iteration rendered a filled UV-mesh via `DrawDebugMesh`, but the
	 * engine's hardcoded `DebugMeshMaterial` depth-tests regardless of
	 * `DepthPriority` (a character mesh in front would clip the sphere).
	 * `SDPG_Foreground` only bypasses depth for LINE primitives, not
	 * for the mesh path, so the helper now draws only the wireframe
	 * layer. But with low segment count (8-2), Thickness=0, and an
	 * alpha-blended color to avoid the "busy wireframe" look that
	 * motivated the mesh experiment in the first place. Kept the
	 * `Solid` name for API stability.
	 *
	 * The `Alpha` parameter is applied OVER the passed `Color.A` (i.e.
	 * overrides it) so callsites can keep using `FColor::Yellow` etc.
	 * without manually baking alpha. The default 100/255 <=39 %
	 * reads as "present but not blocking the view", which is what every
	 * CCS gizmo wants. Pass higher for emphasis (progress markers)
	 * and lower for large "volume" spheres (CollisionPush self-sphere)
	 * so they stay non-occluding.
	 *
	 * @param World          World to draw into. No-op if null.
	 * @param Center         Sphere center in world space.
	 * @param Radius         Sphere radius in world units.
	 * @param Color          RGB from this; A is overridden by the Alpha param.
	 * @param Alpha          Final alpha in [0, 255]. Default 100 (<=39 %).
	 * @param Segments       Ring segment count per hemisphere. Clamped
	 *                       to [4, 32]. 12 = smooth silhouette; 8 reads
	 *                       a touch sparser and is used as the default
	 *                       for overlay-style callsites.
	 * @param DepthPriority  SDPG group. 0 = depth-tested (sphere hides
	 *                       behind opaque meshes); 1 (SDPG_Foreground)
	 *                       = draws above scene geometry via the line
	 *                       batcher's foreground pass. Every CCS
	 *                       callsite passes `SDPG_Foreground` so the
	 *                       marker is always visible even when embedded
	 *                       in a character / geometry.
	 * @param Label          Optional world-space text drawn above the
	 *                       sphere. Keep it short: node name, or node +
	 *                       marker role when one node draws several spheres.
	 *                       Labels are redrawn with a one-frame lifetime
	 *                       so moving markers do not leave stale text behind.
	 *
	 * Compiled out in shipping builds.
	 */
	static void DrawSolidDebugSphere(
		class UWorld* World,
		const FVector& Center,
		float Radius,
		const FColor& Color,
		uint8 Alpha = 100,
		int32 Segments = 12,
		uint8 DepthPriority = 0,
		const TCHAR* Label = nullptr);
#endif
};
