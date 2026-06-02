// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataAssets/ComposableCameraShotTarget.h"
#include "Math/Interval.h"
#include "ComposableCameraShot.generated.h"

/**
 * Selects how a Shot anchor. A single world-space point. Is resolved
 * from the Shot's targets. Used by both the placement anchor (where the
 * camera is placed relative to) and the aim anchor (where the camera is
 * looking at). See `FComposableCameraAnchorSpec` below for the data-side.
 */
UENUM(BlueprintType)
enum class EShotAnchorMode : uint8
{
	/** Anchor = Targets[TargetIndex].Target's resolved world pivot. */
	SingleTarget,

	/**
	 * Anchor = weighted centroid of multiple targets' world pivots.
	 * WeightedTargets carries (TargetIndex, Weight) pairs; only entries
	 * with valid TargetIndex AND Weight > 0 contribute.
	 */
	WeightedWorldCentroid,

	/** Anchor = an explicit world-space point (WorldPosition), independent
	 *  of any target. */
	FixedWorldPosition
};

/**
 * Selects how the camera's POSITION is determined.
 *
 *   - **AnchorOrbit**: pure spherical placement around the placement anchor.
 *     Camera = PlacementAnchor + Distance * BasisQuat * UnitDir(Yaw, Pitch).
 *     `ScreenPosition` is **unused**. Anchor projects to screen center
 *     under tentative look-at-anchor rotation. Recommended default;
 *     designers wanting an off-center anchor on screen should use
 *     `Aim.ScreenPosition` (rotation-realized) instead.
 *
 *   - **AnchorAtScreen**: AnchorOrbit's spherical placement
 *     THEN a lateral camera shift along basis-derived right / up axes to
 *     make the anchor project to `Placement.ScreenPosition` under
 *     tentative rotation. Useful for OTS-style framings where designer
 *     wants explicit control over the placement anchor's screen X / Y
 *     while Aim looks at a different anchor. **Caveat**: once the
 *     lateral shift is applied, the camera is no longer literally "at
 *     Yaw/Pitch around anchor". The effective spherical position
 *     drifts. The two parametrizations (Yaw/Pitch + ScreenPosition)
 *     over-specify the camera position; the result is the geometric
 *     composition of both, NOT a strict spherical interpretation of
 *     Yaw/Pitch.
 *
 *   - **FixedWorldPosition**: camera placed at an explicit world-space
 *     point. No orbit, no anchor required for position. Useful for
 *     "locked" cinematic shots (cranes, jib heads, surveillance cams).
 *
 * Drives the Placement layer of the Composition Solver.
 */
UENUM(BlueprintType)
enum class EShotPlacementMode : uint8
{
	AnchorOrbit,
	AnchorAtScreen,
	FixedWorldPosition
};

/**
 * Selects how the camera's ROTATION is determined (after Position is set
 * by the Placement layer).
 *
 *   - **LookAtAnchor**: camera rotates so the aim anchor lands at
 *     `Aim.ScreenPosition`. Closed-form via
 *     `SolveCameraRotationForScreenTarget`. The aim anchor may differ
 *     from the placement anchor. When it does, this is naturally an
 *     OTS / two-shot framing (camera placed near subject A, looking at
 *     subject B).
 *
 *   - **NoOp**: Aim layer does nothing. Output rotation = identity with
 *     `Shot.Roll` composed. `Aim.AimAnchor` and `Aim.ScreenPosition` are
 *     ignored. Useful when downstream nodes (or a FixedWorldPosition
 *     placement) should fully drive rotation; the editor renders the
 *     Aim handle greyed out as a non-effective indicator. Note: in NoOp
 *     mode `SolvedFromBoundsFit` FOV and `FollowAnchor` Focus modes
 *     still consume the identity rotation. Projection / depth
 *     computations relative to that frame may not match designer intent;
 *     prefer `Manual` Lens + Focus modes when pairing with NoOp Aim.
 *
 * Drives the Aim layer of the Composition Solver.
 */
UENUM(BlueprintType)
enum class EShotAimMode : uint8
{
	LookAtAnchor,
	NoOp
};

/**
 * Selects how FOV is computed. Drives the Lens layer.
 */
UENUM(BlueprintType)
enum class EShotFOVMode : uint8
{
	/** Use FShotLens::ManualFOV directly. */
	Manual,

	/**
	 * Solve FOV from per-target bounds using the weight-scaled perceptual
	 * union box algorithm.
	 */
	SolvedFromBoundsFit
};

/**
 * Selects what world point drives the focus distance. Independent of
 * Position / Rotation / FOV.
 */
UENUM(BlueprintType)
enum class EShotFocusMode : uint8
{
	/** Use FShotFocus::ManualDistance directly. */
	Manual,

	/** Focus distance = camera-to-PlacementAnchor depth (along forward). */
	FollowPlacementAnchor,

	/** Focus distance = camera-to-AimAnchor depth (along forward). */
	FollowAimAnchor,

	/** Focus distance = camera-to-FocusAnchor depth (along forward),
	 *  where FocusAnchor is its own `FComposableCameraAnchorSpec` - letting the focus point follow a third world point independent
	 *  of Placement / Aim. */
	FollowCustomAnchor
};

/**
 * Reference-frame selector for AnchorOrbit's `LocalCameraDirection`.
 * Lives at `FShotPlacement::BasisFrame`.
 */
UENUM(BlueprintType)
enum class EShotPlacementBasisFrame : uint8
{
	/** Use world axes for the LocalCameraDirection basis. Always valid. */
	World,

	/**
	 * Use the actor at `FShotPlacement::BasisActorIndex` as the basis - its world quat (or its first SkelMeshComponent's quat when the
	 * target's `bUseSkeletalMeshForwardAsBasis` flag is set, see
	 * `FComposableCameraTargetInfo::ResolveBasisQuat`). Falls back to
	 * World basis with a warning when the index is out of range or the
	 * actor is null.
	 */
	InheritFromActor
};

/**
 * One entry in `FComposableCameraAnchorSpec::WeightedTargets`: a target
 * index + non-negative weight. Used when AnchorMode == WeightedWorldCentroid.
 */
USTRUCT(BlueprintType)
struct COMPOSABLECAMERASYSTEM_API FComposableCameraAnchorTargetWeight
{
	GENERATED_BODY()

	/** Index into the owning Shot's Targets array. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anchor")
	int32 TargetIndex = 0;

	/** Weight in [0, 1]. Entries with Weight == 0 are silently dropped.
	 *  Only ratios matter in the centroid math, [0, 1] keeps Details intent
	 *  readable (consistent with the other Weight fields on FShotTarget). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anchor",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Weight = 1.f;
};

/**
 * Resolves a single world-space anchor point from various sources. Reused
 * by `FShotPlacement::PlacementAnchor`, `FShotAim::AimAnchor`, and
 * `FShotFocus::FocusAnchor`. Three different roles, one shape.
 *
 * Three modes:
 *   - SingleTarget:           anchor = one target's pivot
 *   - WeightedWorldCentroid:  anchor = weighted centroid of N targets
 *   - FixedWorldPosition:     anchor = an explicit world point
 *
 * Properties are BlueprintReadOnly because Shot data is designer-authored.
 */
USTRUCT(BlueprintType)
struct COMPOSABLECAMERASYSTEM_API FComposableCameraAnchorSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anchor")
	EShotAnchorMode Mode = EShotAnchorMode::SingleTarget;

	/** Index into the owning Shot's Targets array. Used iff Mode ==
	 *  SingleTarget. Validated >= 0 && < Targets.Num() at solve time. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anchor",
		meta = (EditCondition = "Mode == EShotAnchorMode::SingleTarget"))
	int32 TargetIndex = 0;

	/** Per-target weights for WeightedWorldCentroid mode. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anchor",
		meta = (EditCondition = "Mode == EShotAnchorMode::WeightedWorldCentroid"))
	TArray<FComposableCameraAnchorTargetWeight> WeightedTargets;

	/** Explicit world-space point, used iff Mode == FixedWorldPosition. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anchor",
		meta = (EditCondition = "Mode == EShotAnchorMode::FixedWorldPosition"))
	FVector WorldPosition = FVector::ZeroVector;

	/**
	 * Resolves to a single world point given the Shot's full target list.
	 * Returns false (OutPos unchanged) when:
	 *   SingleTarget       - TargetIndex out of range OR Actor null
	 *   WeightedCentroid  . No entry has Weight > 0 AND a valid Actor
	 *   FixedWorldPosition. Never (always returns true)
	 */
	bool ResolveWorldPosition(
		TConstArrayView<FComposableCameraShotTarget> Targets,
		FVector& OutPos) const;
};

/**
 * One side's padding for a `FShotScreenZones` rectangle. Each padding
 * value is the **distance from the authored `ScreenPosition` center to
 * that edge**, expressed in normalized screen fractions. Half-extents
 * are independent per side so framing zones can be asymmetric. E.g. a
 * tracking shot can carry a wide right-side soft zone (lead room) and a
 * tight left-side dead zone. Designer drags one edge to mutate exactly
 * one of these four floats.
 *
 * Range `[0, 0.5]` covers half the viewport per side; the maximum
 * realistic asymmetric zone is the full screen (0.5 + 0.5 across an axis).
 */
USTRUCT(BlueprintType)
struct COMPOSABLECAMERASYSTEM_API FShotScreenZonePadding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Padding",
		meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float Left = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Padding",
		meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float Right = 0.1f;

	/** "Top" = +Y in the solver's normalized screen convention (= upward
	 *  on screen). Maps to the SMALLER pixel-Y in viewport coords. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Padding",
		meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float Top = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Padding",
		meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float Bottom = 0.1f;
};

/**
 * Cinemachine-style screen-space framing zones for an anchor's screen
 * position constraint. The zone is a pair of nested rectangles
 * **centered on `ScreenPosition`** (NOT on the anchor's projected
 * position). The anchor "floats" inside the zone, its projection drifts
 * relative to ScreenPosition while the camera holds, and the solver
 * pulls it back when it strays.
 *
 * When `bEnabled == false` the solver runs the hard-constraint path
 * (anchor lands exactly at `ScreenPosition` every frame). When
 * `bEnabled == true` the solver:
 *
 *   1. projects the anchor through `LastOutputPose` to read its current
 *      screen position;
 *   2. computes the per-axis residual outside the dead-zone padding:
 *      anchor inside the dead rect [SP - DeadLeft, SP + DeadRight] x
 *      [SP - DeadBottom, SP + DeadTop] means zero residual, so camera holds;
 *   3. one-pole (`FMath::FInterpTo`) damps the residual per axis using
 *      `HorizontalSpeed` / `VerticalSpeed`;
 *   4. clamps the post-damping offset to the soft-zone padding rectangle
 *      (hard limit. Anchor never leaves soft zone, no damping on the
 *      clamp);
 *   5. feeds the resulting effective screen target into the closed-form /
 *      decoupled placement solver.
 *
 * One struct, two attachment sites -`FShotAim::AimZones` (always
 * applies when AimMode == LookAtAnchor) and `FShotPlacement::PlacementZones`
 * (only consumed in `AnchorAtScreen` placement; ignored by `AnchorOrbit`
 * because that mode does not author a `Placement.ScreenPosition`).
 *
 * **Asymmetric paddings** allow zones that aren't centered on
 * ScreenPosition. Useful for lead-room framing where the dead zone
 * trails the subject on one axis. Designer drags one edge -> only the
 * matching padding mutates (Cinemachine-style single-side resize).
 *
 * Damping speeds are `FMath::FInterpTo`-style. Higher = snappier;
 * `0` = no damping (snap to zone boundary instantly). Match the
 * convention used by `UComposableCameraIIRInterpolator::Speed`.
 *
 * Per-side soft padding must be `>= ` matching dead padding (Soft is
 * the outer rect; Dead is the inner). The drag handler enforces this
 * by pushing the partner side; the solver also defensively clamps.
 */
USTRUCT(BlueprintType)
struct COMPOSABLECAMERASYSTEM_API FShotScreenZones
{
	GENERATED_BODY()

	FShotScreenZones()
	{
		// Soft-zone defaults are 3x dead-zone defaults (0.3 vs 0.1 per
		// side), matching Cinemachine's RotationComposer baseline.
		SoftZone.Left   = 0.3f;
		SoftZone.Right  = 0.3f;
		SoftZone.Top    = 0.3f;
		SoftZone.Bottom = 0.3f;
	}

	/** Master switch. `false` = hard-constraint path (every frame the
	 *  anchor is solved toward `ScreenPosition`: closed-form for
	 *  `LookAtAnchor`, decoupled placement for `AnchorAtScreen + LookAtAnchor`).
	 *  `true` = pose-state-aware Cinemachine-style damped framing
	 *  described above. Default `false` for backward compatibility. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zones")
	bool bEnabled = false;

	/** Inner rectangle (per-side padding from ScreenPosition). Anchor
	 *  inside this rect = camera does not adjust. Default 0.1 each side
	 *  = 20% x 20% rect when zones are symmetric. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zones",
		meta = (EditCondition = "bEnabled"))
	FShotScreenZonePadding DeadZone;

	/** Outer rectangle (per-side padding from ScreenPosition). Anchor is
	 *  hard-clamped to never leave this rect. Each side must be `>= `
	 *  matching dead-zone padding (drag handler enforces; solver also
	 *  defensively clamps). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zones",
		meta = (EditCondition = "bEnabled"))
	FShotScreenZonePadding SoftZone;

	/** Horizontal damping speed (`FMath::FInterpTo` Speed semantics) - higher = snappier, `0` = instant snap to zone boundary. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zones",
		meta = (EditCondition = "bEnabled", ClampMin = "0.0"))
	float HorizontalSpeed = 5.f;

	/** Vertical damping speed (`FMath::FInterpTo` Speed semantics) - higher = snappier, `0` = instant snap to zone boundary. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zones",
		meta = (EditCondition = "bEnabled", ClampMin = "0.0"))
	float VerticalSpeed = 5.f;
};

/**
 * Placement layer. Decides camera POSITION. Layered architecture: the
 * solver runs Placement first to get a Position, then Aim (which only
 * decides Rotation), then Lens (FOV), then Focus.
 *
 * Anchor concepts unified: Placement's anchor is the world point the
 * camera is placed RELATIVE TO; Aim's anchor (separate field on
 * `FShotAim`) is the world point the camera LOOKS AT. They can be the
 * same (standard third-person) or different (OTS. Placed near A,
 * looking at B).
 */
USTRUCT(BlueprintType)
struct COMPOSABLECAMERASYSTEM_API FShotPlacement
{
	GENERATED_BODY()

	/** Authoring range for `Distance`, in cm. Mirrored by the field's
	 *  `ClampMin` / `ClampMax` UPROPERTY meta. But since UPROPERTY meta
	 *  only enforces clamping at the Details-panel input layer, all
	 *  *code* writers (gestures, reverse-solve, BP setters, runtime) must
	 *  go through `FMath::Clamp(..., MinDistance, MaxDistance)` to keep
	 *  the canonical range in sync. See the `Distance` field comment for
	 *  the rationale behind the bounds. */
	static constexpr float MinDistance = 1.f;       // 1cm. Solver pre-flight floor
	static constexpr float MaxDistance = 10000.f;   // 100m. Sanity cap

	/** Where in the world the camera is placed RELATIVE to. Resolved from
	 *  the Shot's Targets list (or a fixed world point). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	FComposableCameraAnchorSpec PlacementAnchor;

	/** Selects how Position is computed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	EShotPlacementMode Mode = EShotPlacementMode::AnchorOrbit;

	// --- AnchorOrbit-only fields (pure spherical) ------------------------

	/** Reference-frame selector for `LocalCameraDirection`. World means
	 *  global axes; InheritFromActor means the basis actor's world quat.
	 *  Only consumed in `AnchorOrbit` mode (pure spherical); the
	 *  `AnchorAtScreen` mode borrows its forward axis from
	 *  Aim and has no need for a basis. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement|AnchorOrbit",
		meta = (EditCondition = "Mode == EShotPlacementMode::AnchorOrbit"))
	EShotPlacementBasisFrame BasisFrame = EShotPlacementBasisFrame::InheritFromActor;

	/** Index into Targets. The actor whose world quat is the basis when
	 *  `BasisFrame == InheritFromActor`. Falls back to World basis when
	 *  out of range or the actor is null. AnchorOrbit-only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement|AnchorOrbit",
		meta = (EditCondition = "Mode == EShotPlacementMode::AnchorOrbit && BasisFrame == EShotPlacementBasisFrame::InheritFromActor"))
	int32 BasisActorIndex = 0;

	/** Camera position direction in BasisFrame's basis, expressed as
	 *  (Yaw, Pitch) in degrees. AnchorOrbit-only -`AnchorAtScreen`
	 *  derives camera position from the placement screen constraint and
	 *  Aim rotation seed, no spherical direction parameter. Roll about the look
	 *  axis is authored separately on the Shot's top-level `Roll` field. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement|AnchorOrbit",
		meta = (EditCondition = "Mode == EShotPlacementMode::AnchorOrbit"))
	FVector2D LocalCameraDirection = FVector2D(180.f, 0.f);

	// --- Shared by AnchorOrbit and AnchorAtScreen -----------

	/** Camera-to-PlacementAnchor distance in world units (cm). Semantics
	 *  depend on Mode:
	 *    - AnchorOrbit: Euclidean. Measured along the unit direction
	 *      vector implied by `LocalCameraDirection`. In the typical case
	 *      where camera looks at the placement anchor, this equals the
	 *      cam-frame depth.
	 *    - AnchorAtScreen: cam-frame depth (X coordinate of
	 *      PlacementAnchor under the assumed camera rotation used by the
	 *      placement pass). Designer thinks "I want PlacementAnchor
	 *      this far in front of camera" regardless of where the lateral
	 *      offset lands it.
	 *
	 *  Range `[1, 10000]` cm = `[1cm, 100m]`. Floor matches the solver's
	 *  pre-flight check (1cm prevents division by ~0 in screen-space
	 *  placement math). Ceiling is a sanity cap against typo / scroll-spam
	 *  pushing Distance to ~1e9. The solver's float math degrades well
	 *  before that, so a hard 100m clamp is cheaper to enforce here than
	 *  to chase down NaN poses downstream. 100m covers the vast majority
	 *  of in-engine framing (character / interior / vehicle scale);
	 *  projects that genuinely need >100m can lift this manually - promote to a project setting if the need recurs.
	 *
	 *  `SliderExponent = "3.0"` weights the Details-panel drag toward
	 *  the low end of the range so dragging at <1000 cm (the typical
	 *  framing scale) doesn't blast past the value the designer is
	 *  trying to hit. Picked over a fixed `Delta = "1.0"` because at
	 *  high-end values (multi-km vista shots) a fixed-cm-per-pixel
	 *  rate would be agonizingly slow. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement|AnchorOrbit",
		meta = (EditCondition = "Mode == EShotPlacementMode::AnchorOrbit || Mode == EShotPlacementMode::AnchorAtScreen", ClampMin = "1.0", ClampMax = "10000.0", Units = "cm", SliderExponent = "3.0"))
	float Distance = 200.f;

	/**
	 * IIR damping speed for `Distance` (`FMath::FInterpTo` Speed semantics).
	 * `0` = no damping -> camera snaps to the authored Distance every frame
	 * (default behavior). Positive = damped. When the designer drags
	 * the Distance slider or a Sequencer track keys it, the camera glides
	 * toward the new value over time instead of teleporting. Higher
	 * values = snappier; lower values = heavier camera. Independent of the
	 * screen-space framing zones (which damp X / Y); this damps Z.
	 *
	 * Skipped in `FixedWorldPosition` mode (Distance is unused there).
	 * Requires `PriorPose != nullptr` like the rest of the stateful solver.
	 * First-frame seed snaps to authored value, damping kicks in
	 * from frame 2 onward.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement|AnchorOrbit",
		meta = (EditCondition = "Mode == EShotPlacementMode::AnchorOrbit || Mode == EShotPlacementMode::AnchorAtScreen", ClampMin = "0.0"))
	float DistanceSpeed = 0.f;

	/**
	 * `AnchorAtScreen`-only. Where the resolved PlacementAnchor
	 * should land on screen, normalized to [-0.5, 0.5] -(0, 0) is screen
	 * center. The placement pass positions the camera so PlacementAnchor
	 * lands at this screen point under an assumed rotation. The Aim pass then
	 * solves rotation for AimAnchor. On activation / reseed, the solver may
	 * run a bounded joint seed to reduce first-frame drift.
	 *
	 * Requires `Aim.Mode == LookAtAnchor` AND `AimAnchor != PlacementAnchor`.
	 * The screen-seeded solve degenerates without both. Solver logs a warning
	 * and skips pose update otherwise.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement|AnchorOrbit",
		meta = (EditCondition = "Mode == EShotPlacementMode::AnchorAtScreen", ClampMin = "-0.5", ClampMax = "0.5"))
	FVector2D ScreenPosition = FVector2D::ZeroVector;

	/**
	 * Cinemachine-style screen-space framing zones for `Placement.ScreenPosition`.
	 * Only consumed in `AnchorAtScreen` placement mode (where placement actually
	 * produces a screen-position constraint on PlacementAnchor); ignored in
	 * `AnchorOrbit` (no `Placement.ScreenPosition`) and `FixedWorldPosition`
	 * (placement is world-locked, not screen-driven).
	 *
	 * When enabled the placement solve runs against the zone-derived effective
	 * screen target instead of the raw authored `ScreenPosition`, with damping
	 * applied in screen space. Anchor inside the dead zone produces zero
	 * error ->the camera holds its previous `LastOutputPose`. See
	 * `FShotScreenZones` for the algorithm description.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement|AnchorOrbit",
		meta = (EditCondition = "Mode == EShotPlacementMode::AnchorAtScreen"))
	FShotScreenZones PlacementZones;

	// --- FixedWorldPosition-only fields ----------------------------------

	/** Used iff `Mode == FixedWorldPosition`. Camera lives at this world
	 *  point; PlacementAnchor / Distance / Direction / ScreenPosition are
	 *  ignored in this mode. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement|FixedWorldPosition",
		meta = (EditCondition = "Mode == EShotPlacementMode::FixedWorldPosition"))
	FVector FixedWorldPosition = FVector::ZeroVector;
};

/**
 * Aim layer. Decides camera ROTATION (Position is already determined by
 * the Placement layer).
 */
USTRUCT(BlueprintType)
struct COMPOSABLECAMERASYSTEM_API FShotAim
{
	GENERATED_BODY()

	/** Where in the world the camera LOOKS AT. Independent of Placement's
	 *  anchor. When they differ, you get OTS / two-shot framing for free.
	 *  Resolved from the Shot's Targets list (or a fixed world point). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim")
	FComposableCameraAnchorSpec AimAnchor;

	/** Selects how Rotation is computed. Currently a single mode
	 *  (LookAtAnchor); kept as an enum for symmetry with PlacementMode and
	 *  future expansion (e.g. ManualEuler). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim")
	EShotAimMode Mode = EShotAimMode::LookAtAnchor;

	/**
	 * Where the resolved aim anchor should land on screen, normalized to
	 * [-0.5, 0.5]. With `AimZones.bEnabled == false` this
	 * is a hard rotation constraint. The closed-form solver satisfies
	 * it exactly via `SolveCameraRotationForScreenTarget` every frame.
	 * With `AimZones.bEnabled == true` this becomes the *target* position
	 * the anchor is damped toward (and the center of the zone rectangles).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim",
		meta = (ClampMin = "-0.5", ClampMax = "0.5"))
	FVector2D ScreenPosition = FVector2D::ZeroVector;

	/**
	 * Cinemachine-style screen-space framing zones for `Aim.ScreenPosition`.
	 * Only meaningful for `AimMode == LookAtAnchor` (NoOp ignores
	 * ScreenPosition entirely). When enabled the closed-form rotation
	 * solver runs against the zone-derived effective screen target
	 * instead of the raw authored `ScreenPosition`. See
	 * `FShotScreenZones` for the algorithm.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim",
		meta = (EditCondition = "Mode == EShotAimMode::LookAtAnchor"))
	FShotScreenZones AimZones;
};

/**
 * Lens layer. Decides FOV + Aperture.
 */
USTRUCT(BlueprintType)
struct COMPOSABLECAMERASYSTEM_API FShotLens
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lens")
	EShotFOVMode FOVMode = EShotFOVMode::Manual;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lens",
		meta = (EditCondition = "FOVMode == EShotFOVMode::Manual", ClampMin = "1.0", ClampMax = "170.0", Units = "deg"))
	float ManualFOV = 79.f;

	/** Used iff FOVMode == SolvedFromBoundsFit. The perceptual-union-box's
	 *  longest axis should occupy this fraction of the viewport. 0.5 = half
	 *  the viewport's longest axis. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lens",
		meta = (EditCondition = "FOVMode == EShotFOVMode::SolvedFromBoundsFit", ClampMin = "0.05", ClampMax = "1.0"))
	float DesiredViewportFillRatio = 0.5f;

	/** Hard clamp on the solved FOV. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lens",
		meta = (EditCondition = "FOVMode == EShotFOVMode::SolvedFromBoundsFit"))
	FFloatInterval FOVClamp { 12.f, 100.f };

	/** Lens aperture (f-stops). No auto mode. Purely artistic. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lens",
		meta = (ClampMin = "0.7", ClampMax = "64.0"))
	float Aperture = 2.8f;

	/**
	 * IIR damping speed for the solved FOV (`FMath::FInterpTo` Speed
	 * semantics). `0` = no damping -> camera snaps to the authored /
	 * solved FOV every frame (default). Positive = damped. When
	 * the designer drags `ManualFOV` or `SolvedFromBoundsFit`'s solved
	 * FOV jumps (target-set composition change), the lens glides toward
	 * the new value over time. Independent of Placement.DistanceSpeed
	 * (which damps depth). Requires `PriorPose != nullptr` like the
	 * other stateful damping; first-frame seed snaps to authored.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lens",
		meta = (ClampMin = "0.0"))
	float FOVSpeed = 0.f;
};

/**
 * Focus layer. Decides focus distance. Independent of Position /
 * Rotation / FOV.
 */
USTRUCT(BlueprintType)
struct COMPOSABLECAMERASYSTEM_API FShotFocus
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Focus")
	EShotFocusMode Mode = EShotFocusMode::FollowAimAnchor;

	/** Used iff Mode == Manual. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Focus",
		meta = (EditCondition = "Mode == EShotFocusMode::Manual", ClampMin = "1.0"))
	float ManualDistance = 200.f;

	/** Used iff Mode == FollowCustomAnchor. Lets the focus point follow a
	 *  third world point distinct from Placement / Aim. Resolved from the
	 *  Shot's Targets list (or a fixed world point) via
	 *  `FComposableCameraAnchorSpec`. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Focus",
		meta = (EditCondition = "Mode == EShotFocusMode::FollowCustomAnchor"))
	FComposableCameraAnchorSpec FocusAnchor;
};

/**
 * Top-level Shot data. The layered composition snapshot for one shot.
 * Three orthogonal solver layers (Placement / Aim / Lens) each take a
 * sub-struct + a per-layer anchor spec, plus an independent Focus layer
 * and a single Roll value composed onto the final rotation.
 *
 * Targets are pure world-space objects (Actor + Bone + Offset + Bounds);
 * they carry NO screen-space data. Screen-space composition lives on
 * Placement (`ScreenPosition`) and Aim (`ScreenPosition`). Each tied to
 * its own anchor.
 *
 * See DesignDoc for the full data model and TechDoc for solver details.
 */
USTRUCT(BlueprintType)
struct COMPOSABLECAMERASYSTEM_API FComposableCameraShot
{
	GENERATED_BODY()

	/** All actors tracked by this Shot, in authoring order. Index stability
	 *  matters: Placement / Aim / Focus anchor specs reference indices
	 *  into this array. Reordering must update those indices in lockstep.
	 *
	 *  Category is `"Shot"` (NOT a sub-category like `"Shot|Targets"`) so
	 *  the array renders at the top of the Details panel, above the
	 *  Placement / Aim / Lens / Focus sub-structs. Designer authoring
	 *  flow is "pick the actors first, then frame them". Targets at
	 *  the top reflects that. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shot")
	TArray<FComposableCameraShotTarget> Targets;

	/** Placement layer. Decides Position. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shot")
	FShotPlacement Placement;

	/** Aim layer. Decides Rotation (Position is already set by Placement). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shot")
	FShotAim Aim;

	/** Camera roll about its forward (look) axis, in degrees. Composed onto
	 *  the output rotation as the final operation; the solver pre-rotates
	 *  Aim.ScreenPosition by -Roll before solving so the screen constraint
	 *  holds at any Roll. 0 = level.
	 *
	 *  Range `[-180, 180]` deg. Kept narrow on purpose:
	 *    1. The Alt+RMB-drag Roll gesture (Section 23.13) accumulates via
	 *       `FMath::UnwindDegrees`, which already maps every accumulated
	 *       value into `[-180, 180]`. Numeric input through the Details
	 *       panel slider is clamped to the same range, so the field's
	 *       edit surfaces and authoring gesture agree on the canonical
	 *       representation.
	 *    2. Values outside `[-180, 180]` are mathematically equivalent
	 *       (mod 360). Extending to e.g. `[-540, 540]` would let the
	 *       designer author redundant values (540 deg == 180 deg visually) and
	 *       open up confusing transition behavior (a linear blend from
	 *       Shot A Roll=170 to Shot B Roll=540 takes the long way around).
	 *    3. FRotator's wrap math handles in-engine values outside the
	 *       range correctly. The clamp is purely a UX / authoring-
	 *       canonical-form constraint, not a runtime correctness one.
	 *
	 *  If a future use case (e.g. multi-revolution roll for a transition
	 *  effect) demands wider range, prefer a dedicated transition node
	 *  over widening this clamp. The canonical shot Roll should stay
	 *  unique-per-pose. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shot",
		meta = (ClampMin = "-180.0", ClampMax = "180.0", Units = "deg"))
	float Roll = 0.f;

	/**
	 * IIR damping speed for `Roll` (`FMath::FInterpTo` Speed semantics).
	 * `0` = no damping -> camera snaps to the authored Roll every frame
	 * (default). Positive = damped. When the designer Alt+RMB-drags
	 * Roll or a Sequencer track keys it, the camera eases into the new
	 * angle. The IIR is **wrap-aware**: a transition from +175 deg to
	 * -175 deg (visually +10 deg) takes the short way around, not the long
	 * way. Requires `PriorPose != nullptr` like the other
	 * stateful damping; first-frame seed snaps to authored.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shot",
		meta = (ClampMin = "0.0"))
	float RollSpeed = 0.f;

	/** Lens layer. Decides FOV + Aperture. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shot")
	FShotLens Lens;

	/** Focus layer. Decides focus distance. Independent of pose / FOV. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shot")
	FShotFocus Focus;
};
