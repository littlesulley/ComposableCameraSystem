// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "DataAssets/ComposableCameraShot.h"
#include "ComposableCameraCompositionFramingNode.generated.h"

class UComposableCameraTransitionDataAsset;

/**
 * Camera node that wraps the Composition Solver. Produces a complete camera
 * pose (Position + Rotation + FOV + Focus + Aperture) from an authored
 * `FComposableCameraShot` each tick.
 *
 * This is the consumer that connects the Phase B Shot data model to the
 * camera evaluation pipeline. See Docs/ShotBasedKeyframing.md Section 3-4 for the
 * data model + solver pipeline; TechDoc Section 3.25 for the solver internals.
 *
 * -- Authoring model (V1) ------------------------------------------------
 *
 * The Shot is authored fully in this node's Details panel inside the
 * camera type asset. The struct contains a `TArray<FShotTarget>` which
 * violates the pin data block's POD constraint (TechDoc Section 3.2), so it is
 * NOT pin-exposable. Phase E's LS Shot Section integration will push Shot
 * data via a separate runtime API (not pin wiring). `GetPinDeclarations`
 * accordingly returns no pins. The node has no inputs to wire and no
 * outputs to pipe; it OWNS the camera pose.
 *
 * -- Behavior ------------------------------------------------------------
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
 * unresolvable (spec Section 5.3 - invalid index, all weights zero, etc.), the
 * node passes the upstream pose through unmodified and the solver logs a
 * warning. Camera doesn't snap; previous frame's pose effectively persists.
 *
 * -- Patch compatibility -------------------------------------------------
 *
 * `Incompatible`. The node's whole purpose is to overwrite the pose; layering
 * a Patch on top of it has no defined semantics. Same classification as
 * `RelativeFixedPoseNode` / `MixingCameraNode`.
 *
 * -- Bounds-cache lifecycle ----------------------------------------------
 *
 * `OnInitialize` refreshes the AutoFromComponentBounds cache for every
 * target with that BoundsShape. Covers `StaticSnapshot` policy entirely
 * and seeds Periodic / Live so their first tick uses fresh data.
 *
 * `OnTickNode` then refreshes per-target according to policy:
 *   - `Live`:           refresh every frame.
 *   - `Periodic`:       refresh when `LocalFrameCounter % Interval == 0`.
 *   - `StaticSnapshot`: never refresh after OnInitialize.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem,
	meta = (ToolTip = "Wraps the Composition Solver. Produces camera pose + lens parameters from an authored FComposableCameraShot."))
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

	/**
	 * Per-process registry of all currently-initialized framing nodes - read by `FComposableCameraShotZoneOverlay` (the LS / PIE viewport
	 * `CCS.Debug.Viewport.ShotZones` overlay) so it can paint anchor +
	 * zone gizmos for every active Shot regardless of whether the host
	 * camera is on the PCM context stack or owned by an LS Component.
	 *
	 * Ownership: each instance adds itself in `OnInitialize` and removes
	 * itself in `BeginDestroy`. `TWeakObjectPtr` keys ensure GC'd nodes
	 * silently drop out of iteration without needing manual cleanup.
	 *
	 * Cost: a single static `TSet` insert/remove per camera lifecycle
	 * boundary; iteration cost is paid only when the overlay CVar is on.
	 * Compiled out in shipping. */
	static const TSet<TWeakObjectPtr<UComposableCameraCompositionFramingNode>>& GetActiveInstances();
#endif

#if !UE_BUILD_SHIPPING
	virtual void BeginDestroy() override;
#endif

public:
	/**
	 * Authored Shot. Drives the Composition Solver each tick. Edited in
	 * the node's Details panel. NOT pin-exposable: `FComposableCameraShot`
	 * contains a `TArray<FShotTarget>` which violates the pin data block's
	 * POD constraint (TechDoc Section 3.2). Runtime pushes (e.g. from LS Shot
	 * Sections in Phase E) mutate this struct via the
	 * `SetActiveShotsFromSequencer` API.
	 *
	 * `BlueprintReadOnly` per spec Section 1.4 -Shot data is designer-authored
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
	 *   InPrimaryShot   -> written into `Shot` (the V1 primary path).
	 *   InSecondaryShot -> optional incoming Shot (the higher-row of an
	 *                     overlap pair). When non-null AND `InTransition`
	 *                     is non-null, the framing node runs both solvers
	 *                     and blends their poses through the transition
	 *                     using `InAlpha` in [0, 1].
	 *   InTransition    -> resolved transition asset; null = hard cut.
	 *                     The blend pass is skipped - `Shot` (primary)
	 *                     is the sole solver input, matching V1 top-row-
	 *                     winner behavior.
	 *   InAlpha         -> secondary's contribution weight in [0, 1]. Clamped
	 *                     defensively. Ignored when `InSecondaryShot` is null
	 *                     or `InTransition` is null.
	 *   bPrimaryChanged -> true iff the LSComponent detected that the
	 *                     active *primary* Section has changed since the
	 *                     previous tick (Section A -> Section B with no
	 *                     overlap, Section bind to a different ShotAsset,
	 *                     etc.).
	 *   bPrimaryWasPreviousSecondary
	 *                  -> true iff this primary was the previous frame's
	 *                     secondary. Used for authored overlap exits
	 *                     (A+B->B): the secondary prior cache is promoted
	 *                     to primary so the first post-blend frame continues
	 *                     the same zone / damping state instead of hard-
	 *                     seeding and popping. When false, a changed primary
	 *                     is treated as a hard cut and reseeds.
	 *   bSecondaryChanged
	 *                  -> true iff the active secondary Section identity
	 *                     changed. Clears secondary prior state so A+B
	 *                     followed by A+C does not feed B's cache into C.
	 *
	 * Persistence: the node retains the last-written state across frames.
	 * When the LSComponent's override map empties, no further calls happen
	 * and the camera holds its last framing. The gap-fill semantic Phase E
	 * established and Phase F preserves.
	 */
	void SetActiveShotsFromSequencer(
		const FComposableCameraShot& InPrimaryShot,
		const FComposableCameraShot* InSecondaryShot,
		UComposableCameraTransitionDataAsset* InTransition,
		float InAlpha,
		bool bPrimaryChanged = false,
		bool bPrimaryWasPreviousSecondary = false,
		bool bSecondaryChanged = false);

	/**
	 * Push the effective render aspect ratio for solver use. The node by
	 * default queries `GetEffectiveViewportAspectRatio` from `OwningPlayerCameraManager`,
	 * which is null in the LS Component path ->falls back to either GameViewport
	 * (PIE) or editor active viewport. That works for unconstrained CineCams,
	 * but with `bConstrainAspectRatio == true` the renderer letterboxes to the
	 * filmback-derived aspect regardless of viewport, and the solver needs to
	 * match. The LS Component computes the effective aspect via
	 * `GetEffectiveAspectRatioForCineCamera(OutputCineCameraComponent)` and
	 * pushes it here; OnTickNode prefers the override when > 0.
	 *
	 * Set every tick by `UComposableCameraLevelSequenceComponent::TickComponent`
	 * before invoking the solver. PCM-driven path doesn't call this. Falls
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

	// --- Detailed-warning rate-limiter for SolveShot failures -----------
	//
	// SolveShot prints a generic warning every frame the primary anchor is
	// unresolvable. The richer diagnostic in `OnTickNode_Implementation`
	// (Targets dump, world-type, owner LSActor) is gated on these so a
	// steady-state failure logs once instead of every frame. Re-armed on
	// recovery (next valid solve) and on Targets-count change so a designer
	// edit that mutates the Targets array re-issues the diagnostic.
	bool bLastTickWasUnresolved = false;
	int32 LastUnresolvedTargetCount = INDEX_NONE;

	// --- Cinemachine-style framing-zone prior-pose state ----------------
	//
	// When `Aim.AimZones.bEnabled` (or the equivalent placement zones) is
	// set, the solver reads anchor positions through the *previous* frame's
	// pose and damps the screen-space residual instead of re-solving the
	// hard constraint each frame. These caches hold "previous frame's solved
	// pose" for the primary and (Phase F) secondary shots independently -	// damping is per-shot so blending two damped shots stacks IIR responses,
	// which is the user's chosen Phase F behavior (over option A's blend-
	// time hard-constraint fallback).
	//
	// Lifecycle:
	//   - `OnInitialize`            ->both `bHas*` cleared. First valid solve
	//                                 each shot performs seeds the cache.
	//   - First successful primary solve  -> seeds `LastPrimaryOutputPose`.
	//   - Same for secondary on first valid blend tick.
	//   - `SetActiveShotsFromSequencer` promotes the secondary prior into
	//     primary when an authored overlap exits as A+B->B. True hard
	//     cuts still clear the primary cache so the next tick re-seeds via
	//     a hard solve.
	//   - Secondary Section identity changes clear the secondary cache so
	//     a new overlap participant starts from its own authored pose.
	//   - Scrub / reverse playback (`DeltaTime <= 0`) is NOT detected as a
	//     state-reset trigger by design. Designers accept the single-shot
	//     determinism penalty in exchange for not having to author scrub-
	//     specific behavior.
	//
	// Pose snapshot scope: only what the zone preprocessor needs from a
	// prior pose -Position + Rotation. FOV / Focus / Aperture are reread
	// from the current frame's solver context.

	/** True iff `LastPrimaryOutputPose` carries a valid prior solve.
	 *  False = next tick performs a V1 hard solve and seeds the cache. */
	bool bHasLastPrimaryOutputPose = false;

	/** Position + rotation produced by the most recent successful primary
	 *  solve. Read by the next tick to project Aim/Placement anchors when
	 *  the corresponding zones are enabled. Only Position + Rotation are
	 *  cached because the Solver re-derives FOV / Focus from the current
	 *  frame's context regardless. */
	FVector  LastPrimaryOutputPosition = FVector::ZeroVector;
	FRotator LastPrimaryOutputRotation = FRotator::ZeroRotator;

	/** Last frame's effective `Placement.Distance` (post-damping +
	 *  post-clamp) for the primary shot. `< 0` means no prior. Solver
	 *  skips Distance damping on the next tick and uses the authored
	 *  value. Cached together with the pose because both share the
	 *  same activation / Section-boundary lifecycle. */
	float    LastPrimaryDistance = -1.f;

	/** Last frame's effective FOV / Roll for the primary shot. Sentinel
	 *  semantics match `FShotPriorPose::LastFOV` / `LastRoll`:
	 *  `LastPrimaryFOV < 0` means no prior; `LastPrimaryRoll == FLT_MAX` means no prior. Solver skips the corresponding damping on the next
	 *  tick when the prior is absent. */
	float    LastPrimaryFOV  = -1.f;
	float    LastPrimaryRoll = TNumericLimits<float>::Max();

	/** Same as primary, but for the Phase F secondary (incoming) shot.
	 *  Cleared whenever `SetActiveShotsFromSequencer` transitions out of
	 *  the secondary-active state. */
	bool     bHasLastSecondaryOutputPose = false;
	FVector  LastSecondaryOutputPosition = FVector::ZeroVector;
	FRotator LastSecondaryOutputRotation = FRotator::ZeroRotator;
	float    LastSecondaryDistance       = -1.f;
	float    LastSecondaryFOV            = -1.f;
	float    LastSecondaryRoll           = TNumericLimits<float>::Max();

	// --- Phase F two-Shot blend state ------------------------------------
	//
	// Set by `SetActiveShotsFromSequencer`; consumed by `OnTickNode` (F.4).
	// Owning entity for these fields is the LSComponent's per-section
	// override map. The framing node holds them only as transient per-frame
	// snapshots. No `UPROPERTY` annotation on `bHasSecondaryShot` /
	// `ActiveBlendAlpha` (POD); `SecondaryShot` is `Transient` for nested-
	// member coverage; `ActiveBlendTransition` is GC-tracked because the
	// asset reference must outlive arbitrary GC sweeps between frames.

	/** True iff a secondary (incoming) Shot is currently active for blend.
	 *  When false the node behaves as V1: Shot is the sole solver input. */
	bool bHasSecondaryShot = false;

	/** Phase F secondary Shot. The higher-row (incoming) section's
	 *  effective Shot. Only consumed when `bHasSecondaryShot` is true and
	 *  `ActiveBlendTransition` is non-null. */
	UPROPERTY(Transient)
	FComposableCameraShot SecondaryShot;

	/** Resolved EnterTransition asset for the active two-Shot blend. Set
	 *  by `SetActiveShotsFromSequencer`. Null indicates hard cut -F.4
	 *  treats this as "secondary Shot is unused" and runs only the primary
	 *  solver path. */
	UPROPERTY(Transient)
	TObjectPtr<UComposableCameraTransitionDataAsset> ActiveBlendTransition;

	/** Secondary's contribution weight in [0, 1] for the active blend.
	 *  0 = primary fully dominant; 1 = secondary fully dominant. */
	float ActiveBlendAlpha = 0.0f;

	/** Push-from-LSComponent override for the solver's `ViewportAspectRatio`.
	 *  Honors CineCam's `bConstrainAspectRatio` (filmback-derived) vs
	 *  unconstrained (live viewport size, including editor scrub via the
	 *  `FGetActiveEditorViewport` hook). 0 = no override, fall back to
	 *  `GetEffectiveViewportAspectRatio(OwningPlayerCameraManager)`. */
	float ExternalAspectRatioOverride = 0.f;
};
