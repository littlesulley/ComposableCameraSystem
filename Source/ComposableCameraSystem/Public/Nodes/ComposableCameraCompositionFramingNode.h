// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "DataAssets/ComposableCameraShot.h"
#include "ComposableCameraCompositionFramingNode.generated.h"

class UComposableCameraTransitionDataAsset;

/**
 * Camera node that wraps the Composition Solver — produces a complete camera
 * pose (Position + Rotation + FOV + Focus + Aperture) from an authored
 * `FComposableCameraShot` each tick.
 *
 * This is the consumer that connects the Phase B Shot data model to the
 * camera evaluation pipeline. See Docs/ShotBasedKeyframing.md §3-4 for the
 * data model + solver pipeline; TechDoc §3.25 for the solver internals.
 *
 * ── Authoring model (V1) ────────────────────────────────────────────────
 *
 * The Shot is authored fully in this node's Details panel inside the
 * camera type asset. The struct contains a `TArray<FShotTarget>` which
 * violates the pin data block's POD constraint (TechDoc §3.2), so it is
 * NOT pin-exposable. Phase E's LS Shot Section integration will push Shot
 * data via a separate runtime API (not pin wiring). `GetPinDeclarations`
 * accordingly returns no pins — the node has no inputs to wire and no
 * outputs to pipe; it OWNS the camera pose.
 *
 * ── Behavior ────────────────────────────────────────────────────────────
 *
 * Pose-overwriting node. Position + Rotation + FieldOfView + Aperture +
 * FocusDistance are unconditionally written from the solver result when it
 * succeeds. If you want upstream nodes to contribute, place them DOWNSTREAM
 * (after the Composition node), not upstream.
 *
 * `PhysicalCameraBlendWeight` is forced to 1.0 so the Aperture +
 * FocusDistance written by the solver are actually consumed by the
 * renderer's DoF system. If you don't want physical-camera DoF, follow this
 * node with a downstream node (e.g. a custom one) that sets
 * `PhysicalCameraBlendWeight` back to 0.
 *
 * `FocalLength` is set to -1 to put the pose in "FOV is authoritative" mode
 * (matches `LensNode::bOverrideFieldOfViewFromFocalLength = true` semantics).
 * Downstream `LensNode` may override.
 *
 * When `Shot.Placement.PlacementAnchor` or `Shot.Aim.AimAnchor` is
 * unresolvable (spec §5.3 — invalid index, all weights zero, etc.), the
 * node passes the upstream pose through unmodified and the solver logs a
 * warning. Camera doesn't snap; previous frame's pose effectively persists.
 *
 * ── Patch compatibility ─────────────────────────────────────────────────
 *
 * `Incompatible`. The node's whole purpose is to overwrite the pose; layering
 * a Patch on top of it has no defined semantics. Same classification as
 * `RelativeFixedPoseNode` / `MixingCameraNode`.
 *
 * ── Bounds-cache lifecycle ──────────────────────────────────────────────
 *
 * `OnInitialize` refreshes the AutoFromComponentBounds cache for every
 * target with that BoundsShape — covers `StaticSnapshot` policy entirely
 * and seeds Periodic / Live so their first tick uses fresh data.
 *
 * `OnTickNode` then refreshes per-target according to policy:
 *   - `Live`:           refresh every frame.
 *   - `Periodic`:       refresh when `LocalFrameCounter % Interval == 0`.
 *   - `StaticSnapshot`: never refresh after OnInitialize.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem,
	meta = (ToolTip = "Wraps the Composition Solver — produces camera pose + lens parameters from an authored FComposableCameraShot."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraCompositionFramingNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	UComposableCameraCompositionFramingNode() { PaletteCategory = TEXT("Framing"); }

public:
	virtual void OnInitialize_Implementation() override;
	virtual void OnTickNode_Implementation(float DeltaTime,
		const FComposableCameraPose& CurrentCameraPose,
		FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(
		TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;
	virtual EComposableCameraNodePatchCompatibility GetPatchCompatibility_Implementation() const override;

#if !UE_BUILD_SHIPPING
	virtual void DrawNodeDebug(UWorld* World, bool bViewerIsOutsideCamera) const override;
#endif

public:
	/**
	 * Authored Shot — drives the Composition Solver each tick. Edited in
	 * the node's Details panel. NOT pin-exposable: `FComposableCameraShot`
	 * contains a `TArray<FShotTarget>` which violates the pin data block's
	 * POD constraint (TechDoc §3.2). Runtime pushes (e.g. from LS Shot
	 * Sections in Phase E) mutate this struct via the
	 * `SetActiveShotsFromSequencer` API.
	 *
	 * `BlueprintReadOnly` per spec §1.4 — Shot data is designer-authored
	 * content, not gameplay-controlled state.
	 *
	 * In Phase F's two-Shot blend the field is treated as the *primary*
	 * (outgoing / lower-row) Shot. The secondary Shot is held in
	 * `SecondaryShot` and active iff `bHasSecondaryShot` is true.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = InputParameters)
	FComposableCameraShot Shot;

	/**
	 * Phase F: push the active Shot pair from the LSComponent each frame
	 * an override is applied.
	 *
	 *   InPrimaryShot   → written into `Shot` (the V1 primary path).
	 *   InSecondaryShot → optional incoming Shot (the higher-row of an
	 *                     overlap pair). When non-null AND `InTransition`
	 *                     is non-null, the framing node runs both solvers
	 *                     and blends their poses through the transition
	 *                     using `InAlpha` ∈ [0, 1].
	 *   InTransition    → resolved transition asset; null = hard cut.
	 *                     The blend pass is skipped — `Shot` (primary)
	 *                     is the sole solver input, matching V1 top-row-
	 *                     winner behavior.
	 *   InAlpha         → secondary's contribution weight ∈ [0, 1]. Clamped
	 *                     defensively. Ignored when `InSecondaryShot` is null
	 *                     or `InTransition` is null.
	 *
	 * Persistence: the node retains the last-written state across frames.
	 * When the LSComponent's override map empties, no further calls happen
	 * and the camera holds its last framing — the gap-fill semantic Phase E
	 * established and Phase F preserves.
	 */
	void SetActiveShotsFromSequencer(
		const FComposableCameraShot& InPrimaryShot,
		const FComposableCameraShot* InSecondaryShot,
		UComposableCameraTransitionDataAsset* InTransition,
		float InAlpha);

	/**
	 * Push the effective render aspect ratio for solver use. The node by
	 * default queries `GetEffectiveViewportAspectRatio` from `OwningPlayerCameraManager`,
	 * which is null in the LS Component path → falls back to either GameViewport
	 * (PIE) or editor active viewport. That works for unconstrained CineCams,
	 * but with `bConstrainAspectRatio == true` the renderer letterboxes to the
	 * filmback-derived aspect regardless of viewport, and the solver needs to
	 * match. The LS Component computes the effective aspect via
	 * `GetEffectiveAspectRatioForCineCamera(OutputCineCameraComponent)` and
	 * pushes it here; OnTickNode prefers the override when > 0.
	 *
	 * Set every tick by `UComposableCameraLevelSequenceComponent::TickComponent`
	 * before invoking the solver. PCM-driven path doesn't call this — falls
	 * back to the `OwningPlayerCameraManager` query, which is correct for
	 * gameplay (PCM has access to PlayerController viewport).
	 */
	void SetExternalAspectRatioOverride(float Aspect)
	{
		ExternalAspectRatioOverride = (Aspect > 0.f) ? Aspect : 0.f;
	}

private:
	/**
	 * Per-instance frame counter for the `Periodic` bounds-cache policy.
	 * Reset in OnInitialize, incremented at the end of every OnTickNode.
	 * `EBoundsCachePolicy::Live` ignores this; `StaticSnapshot` doesn't
	 * touch the cache after OnInitialize.
	 */
	int32 LocalFrameCounter = 0;

	// ─── Phase F two-Shot blend state ────────────────────────────────────
	//
	// Set by `SetActiveShotsFromSequencer`; consumed by `OnTickNode` (F.4).
	// Owning entity for these fields is the LSComponent's per-section
	// override map — the framing node holds them only as transient per-frame
	// snapshots. No `UPROPERTY` annotation on `bHasSecondaryShot` /
	// `ActiveBlendAlpha` (POD); `SecondaryShot` is `Transient` for nested-
	// member coverage; `ActiveBlendTransition` is GC-tracked because the
	// asset reference must outlive arbitrary GC sweeps between frames.

	/** True iff a secondary (incoming) Shot is currently active for blend.
	 *  When false the node behaves as V1: Shot is the sole solver input. */
	bool bHasSecondaryShot = false;

	/** Phase F secondary Shot — the higher-row (incoming) section's
	 *  effective Shot. Only consumed when `bHasSecondaryShot` is true and
	 *  `ActiveBlendTransition` is non-null. */
	UPROPERTY(Transient)
	FComposableCameraShot SecondaryShot;

	/** Resolved EnterTransition asset for the active two-Shot blend. Set
	 *  by `SetActiveShotsFromSequencer`. Null indicates hard cut — F.4
	 *  treats this as "secondary Shot is unused" and runs only the primary
	 *  solver path. */
	UPROPERTY(Transient)
	TObjectPtr<UComposableCameraTransitionDataAsset> ActiveBlendTransition;

	/** Secondary's contribution weight ∈ [0, 1] for the active blend.
	 *  0 = primary fully dominant; 1 = secondary fully dominant. */
	float ActiveBlendAlpha = 0.0f;

	/** Push-from-LSComponent override for the solver's `ViewportAspectRatio`.
	 *  Honors CineCam's `bConstrainAspectRatio` (filmback-derived) vs
	 *  unconstrained (live viewport size, including editor scrub via the
	 *  `FGetActiveEditorViewport` hook). 0 = no override, fall back to
	 *  `GetEffectiveViewportAspectRatio(OwningPlayerCameraManager)`. */
	float ExternalAspectRatioOverride = 0.f;
};
