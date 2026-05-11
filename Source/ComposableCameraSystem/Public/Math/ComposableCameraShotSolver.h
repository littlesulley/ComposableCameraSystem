// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraSystemModule.h"        // LogComposableCameraSystem
#include "DataAssets/ComposableCameraShot.h"
#include "DataAssets/ComposableCameraShotTarget.h"
#include "GameFramework/Actor.h"
#include "Math/ComposableCameraMath.h"           // SolveCameraRotationForScreenTarget, ProjectWorldPointToScreen
#include "Math/Interval.h"
#include "Utils/ComposableCameraProjectSettings.h" // Picard tuning (max iters, tolerance, relaxation)

/**
 * Composition Solver for `FComposableCameraShot` — the heart of the
 * Shot-Based Keyframing runtime. Three-layer pipeline (Placement → Aim →
 * Lens) plus an independent Focus pass and a final Roll composition.
 *
 *   1. Placement → camera Position
 *      AnchorOrbit       — spherical placement around PlacementAnchor,
 *                          plus lateral shift to realize Placement.ScreenPosition.
 *      FixedWorldPosition — camera at an explicit world point.
 *
 *   2. Aim → camera Rotation
 *      LookAtAnchor      — closed-form rotation that lands AimAnchor at
 *                          Aim.ScreenPosition (pre-rotated by -Roll so the
 *                          constraint holds after the final Roll composition).
 *
 *   3. Lens → FOV + Aperture
 *      Manual            — direct passthrough.
 *      SolvedFromBoundsFit — Weight-scaled Perceptual Union Box on Targets'
 *                            bounds (BlackEye-derived; spec §4.5).
 *
 *   4. Focus → focus distance (independent)
 *      Manual / FollowPlacementAnchor / FollowAimAnchor / FollowCustomAnchor.
 *
 *   5. Roll composed onto output rotation as the final operation.
 *
 * Pose-time only: consumes target world transforms at the moment of
 * evaluation, no prediction.
 *
 * All step functions are public so they can be unit-tested independently.
 * The top-level orchestrator is `SolveShot()`.
 *
 * Design notes:
 *
 *   - Header-inline. Optimizer benefits from seeing through the call
 *     boundaries; cold-enough that we don't care about code-size
 *     duplication. Same convention as `Math/ComposableCameraMath.h`.
 *
 *   - No Blueprint-callable surface (per spec §1.4 "no runtime BP API for
 *     mutating Shot data"). Solver consumers are C++ only —
 *     `UComposableCameraCompositionFramingNode` and the unit tests.
 *
 *   - Chicken-and-egg with FOV: the rotation-solve projection uses the
 *     previous frame's FOV (passed in via `FShotSolveContext`). When
 *     `FOVMode == Manual`, the manual value is used instead. When
 *     SolvedFromBoundsFit is active, the solver converges in 1-2 frames
 *     after a Shot transition.
 *
 *   - AnchorAtScreen + LookAtAnchor has a strict first-frame seed path:
 *     when no prior pose is supplied, a bounded Picard solve satisfies both
 *     Placement.ScreenPosition and Aim.ScreenPosition before zone damping
 *     starts. Once a prior pose exists, the hot path uses the cheaper
 *     decoupled Position-then-Aim solve and lets zones / damping provide
 *     the Cinemachine-style steady-state behavior.
 */
namespace ComposableCameraSystem::ShotSolver
{
	/**
	 * Per-frame inputs the solver needs from the runtime — viewport state +
	 * the previous frame's FOV (used as the projection FOV when the Shot
	 * is in SolvedFromBoundsFit mode).
	 */
	struct FShotSolveContext
	{
		/** Viewport aspect ratio (width / height). */
		float ViewportAspectRatio = 16.f / 9.f;

		/** Previous frame's FOV in degrees. Used as projection FOV in the
		 *  bounds-fit pass; Manual mode uses ManualFOV instead. */
		float PreviousFrameFOV = 79.f;
	};

	/**
	 * Solver output. `bValid == false` when an essential anchor cannot be
	 * resolved (placement / aim) — caller should preserve upstream pose for
	 * the frame.
	 */
	struct FShotSolveResult
	{
		bool bValid = false;
		FVector CameraPosition = FVector::ZeroVector;
		FRotator CameraRotation = FRotator::ZeroRotator;
		float FieldOfView = 79.f;
		float FocusDistance = 200.f;
		float Aperture = 2.8f;

		/** Effective `Placement.Distance` actually used by the solve this
		 *  frame — = `FInterpTo(prior.LastDistance, Shot.Placement.Distance,
		 *  dt, Shot.Placement.DistanceSpeed)` clamped `>= 1cm`, or simply
		 *  `max(Shot.Placement.Distance, 1)` when no prior pose was supplied
		 *  / DistanceSpeed is 0 / DeltaTime is 0.
		 *
		 *  Caller should feed this back into `FShotPriorPose::LastDistance`
		 *  on the next tick to keep the IIR seeded. `< 0` ⇒ Distance was
		 *  irrelevant for this solve (e.g. `FixedWorldPosition` mode). */
		float EffectiveDistance = -1.f;
	};

	// ─────────────────────────────────────────────────────────────────────
	// Helpers — basis quat resolution
	// ─────────────────────────────────────────────────────────────────────

	/**
	 * Resolves the basis quat for the Placement layer's `LocalCameraDirection`.
	 *   - World basis             → FQuat::Identity (always valid).
	 *   - InheritFromActor basis  → Targets[BasisActorIndex]'s basis quat,
	 *                               via FComposableCameraTargetInfo::ResolveBasisQuat
	 *                               (mesh-component quat for ACharacter-style
	 *                               targets when the per-target flag is set,
	 *                               actor quat otherwise; PIE-remap aware).
	 *                               Falls back to identity (with warning) when
	 *                               the index is out of range or the actor is
	 *                               unresolvable. Both warning paths dedupe so
	 *                               the log line fires at most once per
	 *                               (Shot pointer, distinct unresolvable Actor
	 *                               soft-path) — designers are told once when
	 *                               their basis assignment isn't taking effect,
	 *                               then the hot path stays quiet.
	 */
	inline FQuat ResolvePlacementBasis(const FComposableCameraShot& Shot)
	{
		const FShotPlacement& P = Shot.Placement;
		if (P.BasisFrame == EShotPlacementBasisFrame::World)
		{
			return FQuat::Identity;
		}

		// InheritFromActor — needs a valid target index.
		const int32 Idx = P.BasisActorIndex;
		if (Idx < 0 || Idx >= Shot.Targets.Num())
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("ShotSolver: Placement.BasisActorIndex=%d out of range [0,%d); falling back to World basis."),
				Idx, Shot.Targets.Num());
			return FQuat::Identity;
		}

		FQuat OutBasis = FQuat::Identity;
		if (Shot.Targets[Idx].Target.ResolveBasisQuat(OutBasis))
		{
			return OutBasis;
		}
		// Actor null / level unloaded → World fallback. One-shot warning
		// per distinct Target.Actor soft-object-path so designers notice
		// "BasisFrame=InheritFromActor but my target's Actor field is
		// empty" — a silent World fallback would leave them puzzling over
		// why LocalCameraDirection is world-aligned. Keying by
		// FSoftObjectPath rather than FObjectKey because the actor pointer
		// is null at this point — only the soft path is stable. Function-
		// local static is per-process (deduped via comdat across TUs);
		// worst case a warning fires once per module, which is still vastly
		// better than per-frame.
		{
			static TSet<FSoftObjectPath> WarnedPaths;
			const FSoftObjectPath Path = Shot.Targets[Idx].Target.Actor.ToSoftObjectPath();
			if (!WarnedPaths.Contains(Path))
			{
				WarnedPaths.Add(Path);
				UE_LOG(LogComposableCameraSystem, Warning,
					TEXT("ShotSolver: Placement.BasisFrame=InheritFromActor but Targets[%d].Actor (%s) "
					     "is unresolvable; falling back to World basis. Assign a valid Actor to the target "
					     "or switch BasisFrame to World."),
					Idx, Path.IsValid() ? *Path.ToString() : TEXT("<unset>"));
			}
		}
		return FQuat::Identity;
	}

	// ─────────────────────────────────────────────────────────────────────
	// Layer 1 — Placement (camera Position)
	// ─────────────────────────────────────────────────────────────────────

	/**
	 * Camera position for AnchorOrbit mode:
	 *
	 *   CamPos = AnchorPos + Distance · BasisQuat · UnitDir(Yaw, Pitch)
	 *          + lateral shift to realize ScreenPosition
	 *
	 * The lateral shift is along the camera's right / up axes, computed
	 * from the *tentative* look-at-anchor forward (which equals
	 * `-BasisQuat · UnitDir(Yaw, Pitch)`). This is independent of the Aim
	 * layer's eventual rotation: Position is fixed by Placement alone.
	 *
	 * Distance is Euclidean — measured along the unit direction vector.
	 * Equals camera-frame depth in the typical case (AimAnchor ==
	 * PlacementAnchor → camera looks at PlacementAnchor → depth =
	 * Euclidean distance).
	 *
	 * @param TanHalfHOR  tan(FOV_h / 2) for the lateral shift's screen-to-world
	 *                    conversion. Pass the projection FOV (manual or
	 *                    previous-frame).
	 * @param AspectRatio viewport aspect (width / height).
	 */
	inline FVector SolveAnchorOrbitPosition(
		const FVector& AnchorPos,
		const FQuat& BasisQuat,
		float Distance,
		const FVector2D& LocalCameraDirection,
		const FVector2D& ScreenPosition,
		float TanHalfHOR,
		float AspectRatio)
	{
		const float YawRad   = FMath::DegreesToRadians(LocalCameraDirection.X);
		const float PitchRad = FMath::DegreesToRadians(LocalCameraDirection.Y);
		const float CosP = FMath::Cos(PitchRad);
		const FVector DirLocal(
			CosP * FMath::Cos(YawRad),
			CosP * FMath::Sin(YawRad),
			FMath::Sin(PitchRad));

		const FVector DirWorld = BasisQuat.RotateVector(DirLocal);
		FVector CamPos = AnchorPos + Distance * DirWorld;

		// Lateral shift to realize ScreenPosition. Tentative forward = look
		// at PlacementAnchor (i.e., -DirWorld). We need WorldUp-perp Right
		// and Up axes derived from this forward, in UE's LEFT-HANDED
		// coordinate system.
		if (!ScreenPosition.IsNearlyZero())
		{
			const FVector Forward_t = -DirWorld;   // tentative forward
			const FVector WorldUp = FVector::UpVector;
			// UE LHS convention: right = up × forward (NOT forward × up).
			// Sanity check at identity rotation (forward = +X, up = +Z):
			//   up × forward = (0,0,1) × (1,0,0) = (0, 1, 0) = +Y = +Right ✓
			//   (forward × up = (0, -1, 0) = -Right — that's the opposite axis,
			//    which causes lateral shifts to go the WRONG direction. V1
			//    bug fixed in V2 polish.)
			// When Forward ≈ ±WorldUp (looking straight up/down), the
			// cross product degenerates — fall back to skipping the
			// lateral shift to avoid NaN.
			FVector CamRight = FVector::CrossProduct(WorldUp, Forward_t);
			if (CamRight.IsNearlyZero())
			{
				return CamPos;   // no lateral shift in degenerate gimbal-lock case
			}
			CamRight = CamRight.GetSafeNormal();
			// up = forward × right in LHS.
			const FVector CamUp = FVector::CrossProduct(Forward_t, CamRight).GetSafeNormal();

			const float TanHalfVOR = TanHalfHOR / AspectRatio;
			// Shift so anchor lands at (sx, sy) on screen — anchor offset in
			// cam frame = (Distance, -ΔY, -ΔZ) under tentative rotation;
			// screen X = -ΔY / (Distance · 2·TanH), Y = -ΔZ / (Distance · 2·TanV).
			// → ΔY = -sx · Distance · 2·TanH, ΔZ = -sy · Distance · 2·TanV.
			// (ΔY, ΔZ) are in cam frame; project onto world via CamRight, CamUp.
			const float DRight = -ScreenPosition.X * Distance * 2.f * TanHalfHOR;
			const float DUp    = -ScreenPosition.Y * Distance * 2.f * TanHalfVOR;
			CamPos += DRight * CamRight + DUp * CamUp;
		}

		return CamPos;
	}

	// ─────────────────────────────────────────────────────────────────────
	// Input-validation helpers (shared by AnchorAtScreen + Aim)
	// ─────────────────────────────────────────────────────────────────────

	/**
	 * Soft-clamp limit for authored screen positions in the joint-solve
	 * paths. The screen-coord convention is `[-0.5, +0.5]²` (= edge of
	 * frustum at the projection FOV). Authoring values *at* the edge make
	 * the iterative solver's `SolveCameraRotationForScreenTarget` saturate
	 * its `|T| ≤ 1` clamp, which is correct math but loses convergence
	 * margin and tends to produce contorted poses. We clamp authored
	 * inputs to a slightly inset envelope (~ 98% of frustum width / height)
	 * before they enter the iteration so the solver always has headroom.
	 *
	 * Designers authoring values outside this envelope see a one-shot
	 * warning per solve; the clamp is silent at the iteration's interior
	 * (pre-rotation by -Roll can push pre-rotated values outside the
	 * envelope, which is fine — that's an internal value, not user input).
	 */
	inline constexpr float ShotSolverScreenClampLimit = 0.49f;

	/**
	 * Clamp an authored screen position to `[-0.49, +0.49]²`. Returns true
	 * iff a clamp actually fired (caller logs a warning so the designer
	 * sees that their input was modified).
	 */
	inline bool ClampAuthoredScreenPosition(FVector2D& InOutScreenPos)
	{
		const FVector2D Original = InOutScreenPos;
		InOutScreenPos.X = FMath::Clamp(InOutScreenPos.X,
			-ShotSolverScreenClampLimit, ShotSolverScreenClampLimit);
		InOutScreenPos.Y = FMath::Clamp(InOutScreenPos.Y,
			-ShotSolverScreenClampLimit, ShotSolverScreenClampLimit);
		return !InOutScreenPos.Equals(Original, UE_KINDA_SMALL_NUMBER);
	}

	// ─────────────────────────────────────────────────────────────────────
	// Roll-aware screen-position helper (shared by AnchorAtScreen + Aim)
	// ─────────────────────────────────────────────────────────────────────

	/**
	 * Pre-rotates an authored screen position by -Roll so that, after the
	 * final pose has Roll composed onto its rotation, the world point
	 * still projects to the *authored* (un-pre-rotated) screen coords.
	 *
	 * Math (derivation in spec §4.8): under camera Roll R about forward,
	 * a fixed world point's projected coords transform anisotropically as
	 *
	 *     Sx_R = Sx_0 · cosR  -  (Sy_0 / AR) · sinR
	 *     Sy_R = AR · Sx_0 · sinR  +  Sy_0 · cosR
	 *
	 * To preserve post-Roll proj == authored ScreenPos, we solve the inverse:
	 *
	 *     Sx_0 = Sx · cosR  +  (Sy / AR) · sinR
	 *     Sy_0 = -AR · Sx · sinR  +  Sy · cosR
	 *
	 * Defined here (above the first caller, `SolveLookAtAnchorRotation`)
	 * so the order of inline definitions in this header matches the order
	 * of use — C++ requires inline functions to be defined before use
	 * within the same translation unit.
	 */
	inline FVector2D PreRotateScreenForRoll(
		const FVector2D& Authored, float CosRoll, float SinRoll, float AspectRatio)
	{
		return FVector2D(
			 Authored.X * CosRoll + (Authored.Y / AspectRatio) * SinRoll,
			-Authored.X * AspectRatio * SinRoll + Authored.Y * CosRoll);
	}

	// ─────────────────────────────────────────────────────────────────────
	// Cinemachine-style screen-space framing zones
	// ─────────────────────────────────────────────────────────────────────

	/**
	 * Optional prior camera pose handed to `SolveShot` so the zone path
	 * has a base to project anchors through. Lightweight (Pos + Rot,
	 * FOV reuses `Context.PreviousFrameFOV`) — the Solver header stays
	 * free of `FComposableCameraPose` (which lives one module-folder
	 * away in `Cameras/ComposableCameraCameraBase.h`).
	 */
	struct FShotPriorPose
	{
		FVector  Position = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;

		/** Last frame's effective `Distance` (after V2.2 damping + 1cm
		 *  clamp). `< 0` ⇒ no prior, solver skips Distance damping and
		 *  uses the authored value. Set by the caller from
		 *  `FShotSolveResult::EffectiveDistance` after a successful solve. */
		float    LastDistance = -1.f;

		/** Last frame's effective FOV (degrees, post-damping + post-clamp).
		 *  `< 0` ⇒ no prior, solver skips FOV damping and uses the
		 *  freshly-solved value. Caller sets this from
		 *  `FShotSolveResult::FieldOfView` after a successful solve. */
		float    LastFOV = -1.f;

		/** Last frame's effective Roll (degrees, post-damping). Sentinel
		 *  is `FLT_MAX` — Roll legitimately spans the entire `[-180, 180]`
		 *  range incl. 0, so a numeric-zero default would be ambiguous.
		 *  Caller sets this from `FShotSolveResult::CameraRotation.Roll`
		 *  after a successful solve. */
		float    LastRoll = TNumericLimits<float>::Max();
	};

	/**
	 * Compute the effective screen-space target an anchor should be
	 * solved toward this frame, given:
	 *   - `CurrentScreen`     — the anchor's current projected screen
	 *                           coordinate (read off the prior pose).
	 *   - `AuthoredScreenPos` — the designer's authored target screen
	 *                           position (zone-rect center).
	 *   - `Zones`             — the dead/soft padding + damping config.
	 *   - `DeltaTime`         — frame delta seconds (for `FInterpTo`).
	 *
	 * Algorithm (Cinemachine-style, asymmetric per-side):
	 *
	 *   err           = CurrentScreen - AuthoredScreenPos
	 *
	 *   // Per-axis dead-zone subtraction. The dead zone is the rect
	 *   // [-DeadLeft, +DeadRight] × [-DeadBottom, +DeadTop] around SP.
	 *   // err > +Right  → eff = err - Right  (anchor right of dead, pull left)
	 *   // err < -Left   → eff = err + Left   (anchor left  of dead, pull right)
	 *   // else          → eff = 0            (anchor inside dead, hold)
	 *   eff_after     = FInterpTo(eff, 0, dt, Speed)                  // damping
	 *   step          = eff - eff_after
	 *   new_err       = err - step
	 *
	 *   // Soft-zone hard limit: clamp into the soft padding rect, no
	 *   // damping on the clamp (Cinemachine's "HardLimits" semantics).
	 *   new_err.x in [-SoftLeft,   +SoftRight]
	 *   new_err.y in [-SoftBottom, +SoftTop]
	 *
	 *   target = AuthoredScreenPos + new_err
	 *
	 * Anchor inside the dead rect → eff = 0 → step = 0 → new_err = err →
	 * target = CurrentScreen → solver reproduces the prior pose. Damping
	 * Speed = 0 collapses eff_after to 0, i.e. step = eff, so the anchor
	 * snaps to the nearest dead-zone edge in one frame.
	 *
	 * Soft padding is defensively `>= ` dead padding per side; the drag
	 * handler enforces this on author, but the solver also clamps so an
	 * inverted authoring (Soft < Dead on a side) still yields sensible
	 * output rather than a degenerate clamp band.
	 */
	inline FVector2D ApplyScreenZones(
		const FVector2D& CurrentScreen,
		const FVector2D& AuthoredScreenPos,
		const FShotScreenZones& Zones,
		float DeltaTime)
	{
		const FVector2D Err = CurrentScreen - AuthoredScreenPos;

		// Per-side dead-zone thresholds (positive on each side; sign is
		// applied in the residual lambda).
		const float DeadL = Zones.DeadZone.Left;
		const float DeadR = Zones.DeadZone.Right;
		const float DeadT = Zones.DeadZone.Top;
		const float DeadB = Zones.DeadZone.Bottom;

		// Soft sides clamped >= dead sides (defensive — drag handler
		// already enforces; protects against authored inversions).
		const float SoftL = FMath::Max(Zones.SoftZone.Left,   DeadL);
		const float SoftR = FMath::Max(Zones.SoftZone.Right,  DeadR);
		const float SoftT = FMath::Max(Zones.SoftZone.Top,    DeadT);
		const float SoftB = FMath::Max(Zones.SoftZone.Bottom, DeadB);

		// Per-axis dead-zone residual: returns 0 inside the band,
		// otherwise the signed distance from the relevant edge.
		auto DeadResidual = [](float E, float MinusSide, float PlusSide) -> float
		{
			if (E >  PlusSide ) { return E - PlusSide;  }
			if (E < -MinusSide) { return E + MinusSide; }
			return 0.f;
		};
		const float EffX = DeadResidual(static_cast<float>(Err.X), DeadL, DeadR);
		const float EffY = DeadResidual(static_cast<float>(Err.Y), DeadB, DeadT);

		// Per-axis IIR damping toward 0 (= dead-zone edge).
		// FInterpTo's internal clamp handles Speed <= 0 / DeltaTime <= 0
		// by returning Target — instant snap. Same convention as
		// `UComposableCameraIIRInterpolator`.
		const float EffAfterX = FMath::FInterpTo(EffX, 0.f, DeltaTime, Zones.HorizontalSpeed);
		const float EffAfterY = FMath::FInterpTo(EffY, 0.f, DeltaTime, Zones.VerticalSpeed);

		const float StepX = EffX - EffAfterX;
		const float StepY = EffY - EffAfterY;

		// Soft-zone hard clamp on the post-step residual. Note the
		// asymmetric ranges: bottom = −Y in our convention, so
		// `new_err.Y` ∈ [−SoftB, +SoftT].
		const float NewErrX = FMath::Clamp(static_cast<float>(Err.X) - StepX, -SoftL, SoftR);
		const float NewErrY = FMath::Clamp(static_cast<float>(Err.Y) - StepY, -SoftB, SoftT);

		return AuthoredScreenPos + FVector2D(NewErrX, NewErrY);
	}

	/**
	 * Resolve the effective screen-space target for a single anchor
	 * given a prior camera pose. Convenience wrapper that handles the
	 * "anchor unresolvable" failure path (returns the authored screen
	 * position so the V1 hard-constraint solver still has something
	 * sensible to chew on — caller will likely fail on anchor resolve
	 * downstream anyway).
	 *
	 * Returns the authored ScreenPosition unchanged when:
	 *   - Zones are disabled, OR
	 *   - The anchor cannot resolve to a world point (zone math needs
	 *     the projected `CurrentScreen`, which needs a world point), OR
	 *   - The prior pose's rotation is degenerate (Forward dot AnchorDir
	 *     near zero in `ProjectWorldPointToScreen`).
	 *
	 * The projection uses the same `TanHalfHOR` / `AspectRatio` the rest
	 * of the SolveShot pipeline uses for the current frame — so the
	 * effective screen target is consistent with the projection the V1
	 * solver will subsequently invert.
	 */
	inline FVector2D ResolveEffectiveScreenTarget(
		const FComposableCameraAnchorSpec& Anchor,
		TConstArrayView<FComposableCameraShotTarget> Targets,
		const FVector2D& AuthoredScreenPos,
		const FShotScreenZones& Zones,
		const FShotPriorPose& PriorPose,
		float TanHalfHOR,
		float AspectRatio,
		float DeltaTime)
	{
		if (!Zones.bEnabled)
		{
			return AuthoredScreenPos;
		}

		FVector AnchorWorld;
		if (!Anchor.ResolveWorldPosition(Targets, AnchorWorld))
		{
			return AuthoredScreenPos;
		}

		FVector2D CurrentScreen;
		if (!ProjectWorldPointToScreen(AnchorWorld,
				PriorPose.Position, PriorPose.Rotation,
				TanHalfHOR, AspectRatio, CurrentScreen))
		{
			// Anchor behind camera under prior pose, or projection
			// degenerate. Fall back to authored target — V1 hard solve
			// will pull the anchor back into frame, breaking out of the
			// degenerate state on the next tick (where the prior pose
			// will project the anchor sensibly). One-frame artifact
			// is preferable to silently NaN'ing the zone math.
			return AuthoredScreenPos;
		}

		return ApplyScreenZones(CurrentScreen, AuthoredScreenPos, Zones, DeltaTime);
	}

	// ─────────────────────────────────────────────────────────────────────
	// AnchorAtScreen — closed-form Placement (decoupled from Aim)
	// ─────────────────────────────────────────────────────────────────────
	//
	// The earlier joint Picard solver (5-on-5 closed-form for AnchorAtScreen +
	// LookAtAnchor) has been removed in favor of a Cinemachine-style decoupled
	// pipeline:
	//
	//   1. Position pass — `SolveAnchorAtScreenPos` finds CamPos that places
	//      `PlacementAnchor` at `Placement.ScreenPosition` AT DEPTH `Distance`,
	//      assuming a given camera rotation. The rotation is sourced from
	//      either the Aim NoOp identity (Roll-only) or the prior frame's
	//      rotation (LookAtAnchor's "best guess"; first-frame seed = forward
	//      derived from PlacementAnchor → AimAnchor direction).
	//   2. Rotation pass — `SolveLookAtAnchorRotation` computes the closed-
	//      form rotation that lands AimAnchor at `Aim.ScreenPosition`, taking
	//      the just-computed CamPos as input.
	//
	// The trade-off vs. the joint solver: PlacementAnchor's projected screen
	// position drifts slightly from `Placement.ScreenPosition` once the
	// Aim-pass rotation differs from the assumed rotation, because the
	// position-pass CamPos was computed under the *assumed* rotation. With
	// the prior frame's rotation seeding the assumption, the drift is
	// per-frame O(rotation delta) — typically a few pixels when targets
	// move smoothly, washed out completely the moment framing zones are
	// enabled (the zone solver explicitly tolerates this drift). Designers
	// who needed the V1 strict joint constraint should compose their
	// shots with `AnchorOrbit + LookAtAnchor` instead — that mode is
	// fully closed-form and has no decoupling drift at all (because the
	// Position pass doesn't read `ScreenPosition`).
	//
	// Cinemachine reference: PositionComposer + RotationComposer run
	// independently and accept the same kind of soft "best-effort"
	// satisfaction that this decoupled solver matches.

	/**
	 * Closed-form Position pass for `EShotPlacementMode::AnchorAtScreen`.
	 *
	 * Computes a camera position that places `PlacementAnchor` at depth
	 * `Distance` and screen `ScreenPos` IN THE CAM FRAME OF `AssumedRot`.
	 * Algebraic, no iteration:
	 *
	 *     cam_anchor = (D, sx · 2·TanH · D, sy · 2·TanV · D)   // cam frame
	 *     CamPos     = PlacementAnchor - AssumedRot · cam_anchor
	 *
	 * `AssumedRot` is sourced from:
	 *   - Aim NoOp:                 identity + Shot.Roll (Roll-only).
	 *   - Aim LookAtAnchor + prior:  prior frame’s camera rotation.
	 *                                Decoupling-drift is then per-frame
	 *                                O(rotation delta).
	 *   - Aim LookAtAnchor + first-frame seed:
	 *                                rotation built from the
	 *                                (PlacementAnchor → AimAnchor) world
	 *                                direction + Shot.Roll. Matches the
	 *                                Aim pass’s eventual look-at-AimAnchor
	 *                                rotation closely enough that one
	 *                                frame of decoupling drift washes out
	 *                                inside the IIR damping window.
	 *
	 * `ScreenPos` is consumed as-is (no `-Roll` pre-rotation): when
	 * `AssumedRot` already includes Shot.Roll, the cam-frame right/up
	 * axes are themselves rolled, so `(sx · 2·TanH · D, sy · 2·TanV · D)`
	 * lands the anchor at the authored ScreenPosition under the rolled
	 * view. Contrast with `SolveLookAtAnchorRotation` which DOES
	 * pre-rotate — it solves for the rotation, so it cannot pre-roll it.
	 *
	 * Returns false (OutCamPos unchanged) iff `Distance < 1cm`. Authored
	 * `ScreenPos` outside `[-0.49, +0.49]²` is silently clamped to keep
	 * the cam-frame target inside the frustum-safe envelope.
	 */
	inline bool SolveAnchorAtScreenPos(
		const FVector& PlacementAnchorPos,
		FVector2D ScreenPos,
		float Distance,
		float TanHalfHOR,
		float AspectRatio,
		const FRotator& AssumedRot,
		FVector& OutCamPos)
	{
		if (Distance < 1.f)
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("ShotSolver: AnchorAtScreen Distance=%.4f below 1cm floor; "
				     "skipping pose update."),
				Distance);
			return false;
		}

		if (ClampAuthoredScreenPosition(ScreenPos))
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("ShotSolver: AnchorAtScreen ScreenPosition clamped to %.2f²; "
				     "authored value was outside the frustum-safe envelope."),
				ShotSolverScreenClampLimit);
		}

		const float TanHalfVOR = TanHalfHOR / AspectRatio;
		const FVector CamAnchor(
			Distance,
			ScreenPos.X * 2.f * TanHalfHOR * Distance,
			ScreenPos.Y * 2.f * TanHalfVOR * Distance);
		OutCamPos = PlacementAnchorPos - AssumedRot.RotateVector(CamAnchor);
		return true;
	}

	inline FRotator SolveLookAtAnchorRotation(
		const FVector& CamPos,
		const FVector& AimAnchorPos,
		const FVector2D& AimScreenPosition,
		float RollRad,
		float TanHalfHOR,
		float AspectRatio);

	inline bool SolveAnchorAtScreenLookAtJointSeed(
		const FVector& PlacementAnchorPos,
		const FVector& AimAnchorPos,
		FVector2D PlacementScreenPos,
		const FVector2D& AimScreenPos,
		float Distance,
		float EffectiveRollDeg,
		float TanHalfHOR,
		float AspectRatio,
		const FRotator& InitialAssumedRot,
		FVector& OutCamPos,
		FRotator& OutCamRot)
	{
		if ((PlacementAnchorPos - AimAnchorPos).IsNearlyZero())
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("ShotSolver: AnchorAtScreen with PlacementAnchor == AimAnchor "
				     "is degenerate on the first-frame seed; skipping pose update. "
				     "Switch to AnchorOrbit for single-anchor framing."));
			return false;
		}

		if (ClampAuthoredScreenPosition(PlacementScreenPos))
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("ShotSolver: AnchorAtScreen ScreenPosition clamped to %.2f²; "
				     "authored value was outside the frustum-safe envelope."),
				ShotSolverScreenClampLimit);
		}

		constexpr int32 MaxIterations = 24;
		constexpr float Relaxation = 0.7f;
		constexpr float ScreenTolerance = 5.e-4f;

		FRotator AssumedRot = InitialAssumedRot;
		const float RollRad = FMath::DegreesToRadians(EffectiveRollDeg);
		bool bHaveCandidate = false;

		for (int32 Iter = 0; Iter < MaxIterations; ++Iter)
		{
			FVector CandidatePos;
			if (!SolveAnchorAtScreenPos(
					PlacementAnchorPos, PlacementScreenPos,
					Distance, TanHalfHOR, AspectRatio,
					AssumedRot, CandidatePos))
			{
				return false;
			}

			FRotator CandidateRot = SolveLookAtAnchorRotation(
				CandidatePos, AimAnchorPos, AimScreenPos,
				RollRad, TanHalfHOR, AspectRatio);
			CandidateRot.Roll = EffectiveRollDeg;

			OutCamPos = CandidatePos;
			OutCamRot = CandidateRot;
			bHaveCandidate = true;

			FVector2D ProjectedPlacement;
			FVector2D ProjectedAim;
			const bool bPlacementProjects = ProjectWorldPointToScreen(
				PlacementAnchorPos, CandidatePos, CandidateRot,
				TanHalfHOR, AspectRatio, ProjectedPlacement);
			const bool bAimProjects = ProjectWorldPointToScreen(
				AimAnchorPos, CandidatePos, CandidateRot,
				TanHalfHOR, AspectRatio, ProjectedAim);
			if (bPlacementProjects && bAimProjects)
			{
				const float PlacementErrSq =
					(ProjectedPlacement - PlacementScreenPos).SizeSquared();
				const float AimErrSq =
					(ProjectedAim - AimScreenPos).SizeSquared();
				if (PlacementErrSq + AimErrSq <= ScreenTolerance * ScreenTolerance)
				{
					return true;
				}
			}

			AssumedRot = FQuat::Slerp(
				AssumedRot.Quaternion(),
				CandidateRot.Quaternion(),
				Relaxation).GetNormalized().Rotator();
		}

		// Keep the best candidate instead of holding the upstream pose. This
		// path runs only on activation/reseed, and a near-joint seed is less
		// disruptive than a one-frame hold when geometry is merely stiff.
		return bHaveCandidate;
	}

	/**
	 * Computes camera Position based on the Shot's Placement layer.
	 * Returns false (CamPos unchanged) when an essential anchor can't
	 * resolve — caller handles the no-pose fallback.
	 *
	 * `EffectiveDistance` overrides `Shot.Placement.Distance` for the
	 * `AnchorOrbit` mode — `SolveShot` injects a damped distance here
	 * (V2.2 IIR via `Shot.Placement.DistanceSpeed`); decoupled callers
	 * pass `Shot.Placement.Distance` directly to keep V1 hard behavior.
	 * `FixedWorldPosition` ignores it; the solver expects callers to
	 * still pass a sensible value for symmetry.
	 */
	inline bool SolvePlacement(
		const FComposableCameraShot& Shot,
		float EffectiveDistance,
		float TanHalfHOR,
		float AspectRatio,
		FVector& OutCamPos)
	{
		const FShotPlacement& P = Shot.Placement;
		switch (P.Mode)
		{
		case EShotPlacementMode::AnchorOrbit:
			{
				FVector AnchorPos;
				if (!P.PlacementAnchor.ResolveWorldPosition(Shot.Targets, AnchorPos))
				{
					UE_LOG(LogComposableCameraSystem, Warning,
						TEXT("ShotSolver: Placement.PlacementAnchor unresolvable; skipping pose update."));
					return false;
				}
				const FQuat BasisQuat = ResolvePlacementBasis(Shot);
				const float SafeDistance = FMath::Max(EffectiveDistance, 1.f);
				// Pure spherical placement — no ScreenPosition lateral
				// shift. Anchor projects to screen center under tentative
				// rotation; Aim then re-rotates to put AimAnchor at
				// Aim.ScreenPosition.
				OutCamPos = SolveAnchorOrbitPosition(
					AnchorPos, BasisQuat, SafeDistance,
					P.LocalCameraDirection, FVector2D::ZeroVector,
					TanHalfHOR, AspectRatio);
				return true;
			}

		case EShotPlacementMode::AnchorAtScreen:
			// AnchorAtScreen handled in `SolveShot` orchestrator directly
			// (it threads in an AssumedRot for the closed-form Position
			// pass `SolveAnchorAtScreenPos`). `SolvePlacement` is the
			// decoupled-pipeline entry only; reaching this case here is a
			// dispatch bug.
			ensureMsgf(false,
				TEXT("SolvePlacement should not be called for "
				     "AnchorAtScreen — handled inline in SolveShot."));
			return false;

		case EShotPlacementMode::FixedWorldPosition:
			{
				OutCamPos = P.FixedWorldPosition;
				return true;
			}
		}
		return false;
	}

	// ─────────────────────────────────────────────────────────────────────
	// Layer 2 — Aim (camera Rotation)
	// ─────────────────────────────────────────────────────────────────────
	//
	// `PreRotateScreenForRoll` lives above the AnchorAtScreen section —
	// see the helper block before `SolveAnchorAtScreenPos` for the math
	// derivation. Only the LookAtAnchor rotation pass needs it now (the
	// V2.2 closed-form Position pass folds Roll into AssumedRot directly).

	/**
	 * Solves camera Rotation for LookAtAnchor mode: closed-form rotation
	 * that lands AimAnchor at Aim.ScreenPosition. Uses
	 * `SolveCameraRotationForScreenTarget` from `ComposableCameraMath.h`.
	 * Roll is pre-compensated so the screen constraint holds after the
	 * caller composes Roll onto the result.
	 *
	 * Returns FRotator with Pitch / Yaw set, Roll left at zero — caller
	 * sets Roll = Shot.Roll afterwards.
	 */
	inline FRotator SolveLookAtAnchorRotation(
		const FVector& CamPos,
		const FVector& AimAnchorPos,
		const FVector2D& AimScreenPosition,
		float RollRad,
		float TanHalfHOR,
		float AspectRatio)
	{
		const float CosR = FMath::Cos(RollRad);
		const float SinR = FMath::Sin(RollRad);
		const FVector2D PreRotated = PreRotateScreenForRoll(
			AimScreenPosition, CosR, SinR, AspectRatio);

		const FVector AimDir = AimAnchorPos - CamPos;
		auto [PitchDeg, YawDeg] = SolveCameraRotationForScreenTarget(
			TanHalfHOR, AspectRatio, AimDir,
			PreRotated.X, PreRotated.Y);

		return FRotator(PitchDeg, YawDeg, 0.f);
	}

	/**
	 * Computes camera Rotation based on the Shot's Aim layer + Roll.
	 * Returns false (OutRot unchanged) when AimAnchor can't resolve.
	 *
	 * `EffectiveAimScreenPos` overrides `Shot.Aim.ScreenPosition` for
	 * `LookAtAnchor` mode — this is how `SolveShot` injects a zone-derived
	 * effective screen target (Cinemachine-style damped framing). Pass
	 * `Shot.Aim.ScreenPosition` directly to keep V1 hard-constraint
	 * behavior. NoOp ignores this argument (it has no screen constraint).
	 *
	 * `EffectiveRollDeg` is the V2.2 damped Roll (computed in `SolveShot`
	 * from `Shot.Roll`, `PriorPose->LastRoll`, and `Shot.RollSpeed`). Pass
	 * `Shot.Roll` directly to keep V1 hard-constraint behavior.
	 */
	inline bool SolveAim(
		const FComposableCameraShot& Shot,
		const FVector& CamPos,
		const FVector2D& EffectiveAimScreenPos,
		float EffectiveRollDeg,
		float TanHalfHOR,
		float AspectRatio,
		FRotator& OutRot)
	{
		const FShotAim& A = Shot.Aim;

		// NoOp short-circuits before AimAnchor resolution. Output =
		// identity rotation with the effective Roll composed; AimAnchor /
		// Aim.ScreenPosition unread, so they may be left at any value
		// without producing a "unresolvable" failure.
		if (A.Mode == EShotAimMode::NoOp)
		{
			OutRot = FRotator(0.f, 0.f, EffectiveRollDeg);
			return true;
		}

		FVector AimAnchorPos;
		if (!A.AimAnchor.ResolveWorldPosition(Shot.Targets, AimAnchorPos))
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("ShotSolver: Aim.AimAnchor unresolvable; skipping pose update."));
			return false;
		}

		switch (A.Mode)
		{
		case EShotAimMode::LookAtAnchor:
			{
				const float RollRad = FMath::DegreesToRadians(EffectiveRollDeg);
				OutRot = SolveLookAtAnchorRotation(
					CamPos, AimAnchorPos, EffectiveAimScreenPos,
					RollRad, TanHalfHOR, AspectRatio);
				OutRot.Roll = EffectiveRollDeg;   // compose roll
				return true;
			}
		case EShotAimMode::NoOp:
			// Handled above the AimAnchor resolution; case kept for
			// switch-coverage warnings in clang/gcc.
			return true;
		}
		return false;
	}

	/**
	 * Wrap-aware angle damping (degrees). Same `FInterpTo` Speed semantics
	 * as `FMath::FInterpTo` but operates on the *shortest* angular delta —
	 * a transition from `+175°` to `-175°` (visually `+10°`) takes the
	 * short way, not the long way. Returns the unwrapped degrees value
	 * (re-normalized into `[-180, 180]`) so caching the result keeps the
	 * authoring envelope. `Speed <= 0` or `DeltaTime <= 0` ⇒ return Target
	 * (instant snap), matching `FInterpTo`.
	 */
	inline float DampAngleDeg(float LastDeg, float TargetDeg, float DeltaTime, float Speed)
	{
		if (Speed <= 0.f || DeltaTime <= 0.f)
		{
			return TargetDeg;
		}
		const float Delta = FRotator::NormalizeAxis(TargetDeg - LastDeg);
		const float Step  = FMath::Clamp(DeltaTime * Speed, 0.f, 1.f);
		return FRotator::NormalizeAxis(LastDeg + Delta * Step);
	}

	// ─────────────────────────────────────────────────────────────────────
	// Layer 3 — Lens (FOV)
	// ─────────────────────────────────────────────────────────────────────

	/**
	 * Solves FOV from per-target bounds via the Weight-scaled Perceptual
	 * Union Box algorithm. See spec §4.5 for the algorithm; ported from
	 * BlackEyeCameras' ULookAtComponent::GetTargetGroupViewportBoundingBox.
	 *
	 * Edge cases:
	 *   - No contributing bounds → keep CurrentFOV.
	 *   - Any vertex of a target's BB behind camera → skip that target.
	 *
	 * @param CurrentFOV  Used as projection FOV (consistent per-frame
	 *                    projection) AND as the basis for the closed-form
	 *                    atan inversion.
	 */
	inline float SolvePerceptualUnionBoxFOV(
		const FVector& CameraPos,
		const FRotator& CameraRot,
		TConstArrayView<FComposableCameraShotTarget> Targets,
		float DesiredViewportFillRatio,
		float CurrentFOVDeg,
		float AspectRatio,
		const FFloatInterval& FOVClamp)
	{
		const float TanHalfHOR = FMath::Tan(FMath::DegreesToRadians(CurrentFOVDeg * 0.5f));

		struct FProj
		{
			FBox2D    Box     { ForceInit };
			FVector2D Centroid{ FVector2D::ZeroVector };
			float     Weight  { 0.f };
			bool      bValid  { false };
		};
		TArray<FProj, TInlineAllocator<8>> Projections;
		Projections.Reserve(Targets.Num());

		float TotalWeight = 0.f;
		float MaxWeight   = 0.f;

		for (const FComposableCameraShotTarget& T : Targets)
		{
			FProj P;

			const FVector Extent = T.GetEffectiveBoundsExtent();
			if (Extent.IsZero() || T.BoundsContributionWeight <= 0.f)
			{
				Projections.Add(P);
				continue;
			}

			FVector Pivot;
			if (!T.Target.ResolveWorldPoint(Pivot))
			{
				Projections.Add(P);
				continue;
			}

			const FBox WorldBox(Pivot - Extent, Pivot + Extent);
			FVector Vertices[8];
			WorldBox.GetVertices(Vertices);

			bool bAllOnScreen = true;
			FBox2D ViewportBox(ForceInit);
			for (int32 v = 0; v < 8; ++v)
			{
				FVector2D Screen;
				if (!ProjectWorldPointToScreen(Vertices[v], CameraPos, CameraRot,
					TanHalfHOR, AspectRatio, Screen))
				{
					bAllOnScreen = false;
					break;
				}
				ViewportBox += Screen;
			}

			if (!bAllOnScreen)
			{
				Projections.Add(P);
				continue;
			}

			P.Box      = ViewportBox;
			P.Centroid = ViewportBox.GetCenter();
			P.Weight   = T.BoundsContributionWeight;
			P.bValid   = true;
			Projections.Add(P);

			TotalWeight += P.Weight;
			MaxWeight    = FMath::Max(MaxWeight, P.Weight);
		}

		if (TotalWeight < UE_KINDA_SMALL_NUMBER || MaxWeight < UE_KINDA_SMALL_NUMBER)
		{
			return CurrentFOVDeg;
		}

		FVector2D ViewportCentroid = FVector2D::ZeroVector;
		for (const FProj& P : Projections)
		{
			if (P.bValid)
			{
				ViewportCentroid += P.Centroid * (P.Weight / TotalWeight);
			}
		}

		FBox2D PerceptualBox(ViewportCentroid, ViewportCentroid);
		for (const FProj& P : Projections)
		{
			if (!P.bValid)
			{
				continue;
			}
			const float     Importance = P.Weight / MaxWeight;
			const FVector2D AdjCenter  = FMath::Lerp(ViewportCentroid, P.Centroid, Importance);
			const FVector2D AdjExtents = (P.Box.Max - P.Box.Min) * 0.5f * Importance;
			PerceptualBox += FBox2D(AdjCenter - AdjExtents, AdjCenter + AdjExtents);
		}

		const FVector2D PercSize = PerceptualBox.Max - PerceptualBox.Min;
		const float ApparentSize = FMath::Max(PercSize.X, PercSize.Y);
		if (ApparentSize < UE_KINDA_SMALL_NUMBER)
		{
			return CurrentFOVDeg;
		}

		const float ScaleFactor   = ApparentSize / DesiredViewportFillRatio;
		const float NewHalfFOVTan = TanHalfHOR * ScaleFactor;
		const float NewFOVDeg     = FMath::RadiansToDegrees(2.f * FMath::Atan(NewHalfFOVTan));

		return FMath::Clamp(NewFOVDeg, FOVClamp.Min, FOVClamp.Max);
	}

	/** Computes FOV based on the Shot's Lens layer. */
	inline float SolveLens(
		const FComposableCameraShot& Shot,
		const FVector& CamPos,
		const FRotator& CamRot,
		const FShotSolveContext& Context)
	{
		const FShotLens& L = Shot.Lens;
		if (L.FOVMode == EShotFOVMode::Manual)
		{
			return L.ManualFOV;
		}
		return SolvePerceptualUnionBoxFOV(
			CamPos, CamRot, Shot.Targets, L.DesiredViewportFillRatio,
			Context.PreviousFrameFOV, Context.ViewportAspectRatio, L.FOVClamp);
	}

	// ─────────────────────────────────────────────────────────────────────
	// Independent — Focus
	// ─────────────────────────────────────────────────────────────────────

	/**
	 * Focus distance based on Shot.Focus.Mode:
	 *   - Manual                → Focus.ManualDistance.
	 *   - FollowPlacementAnchor → camera-to-PlacementAnchor depth.
	 *   - FollowAimAnchor       → camera-to-AimAnchor depth.
	 *   - FollowCustomAnchor    → camera-to-FocusAnchor depth.
	 *
	 * Depth is on-axis (`(WorldPoint - CameraPos) · CameraForward`), not
	 * Euclidean — same convention as `FocusPullNode` and what
	 * `ApplyPhysicalCameraSettings` consumes downstream. Falls back to
	 * `Manual.ManualDistance` when an anchor mode can't resolve its world
	 * point.
	 */
	inline float SolveFocus(
		const FComposableCameraShot& Shot,
		const FVector& CamPos,
		const FRotator& CamRot)
	{
		const FShotFocus& F = Shot.Focus;
		const auto OnAxisDepth = [&](const FVector& WorldPoint) -> float
		{
			const FVector Forward = CamRot.Vector();
			return FMath::Max(static_cast<float>(FVector::DotProduct(WorldPoint - CamPos, Forward)), 1.f);
		};

		const auto ResolveDepth = [&](const FComposableCameraAnchorSpec& Anchor) -> float
		{
			FVector Pos;
			if (Anchor.ResolveWorldPosition(Shot.Targets, Pos))
			{
				return OnAxisDepth(Pos);
			}
			return F.ManualDistance;
		};

		switch (F.Mode)
		{
		case EShotFocusMode::Manual:
			return F.ManualDistance;
		case EShotFocusMode::FollowPlacementAnchor:
			return ResolveDepth(Shot.Placement.PlacementAnchor);
		case EShotFocusMode::FollowAimAnchor:
			return ResolveDepth(Shot.Aim.AimAnchor);
		case EShotFocusMode::FollowCustomAnchor:
			return ResolveDepth(F.FocusAnchor);
		}
		return F.ManualDistance;
	}

	// ─────────────────────────────────────────────────────────────────────
	// Top-level orchestrator
	// ─────────────────────────────────────────────────────────────────────

	/**
	 * Runs the full pipeline. `bValid == false` when Placement or Aim
	 * fails to resolve — caller preserves upstream pose for the frame
	 * (spec §5.3).
	 *
	 * `PriorPose` + `DeltaTime` enable Cinemachine-style screen-space
	 * framing zones. When non-null AND `Aim.AimZones.bEnabled` /
	 * `Placement.PlacementZones.bEnabled` is set, the solver projects
	 * the corresponding anchor through `*PriorPose` and substitutes a
	 * zone-derived effective screen target for the V1 hard ScreenPosition
	 * read. When null OR zones disabled, V1 hard-constraint behavior is
	 * preserved exactly — every existing call site continues to work
	 * with default arguments.
	 *
	 * `PlacementZones` only meaningfully fires in `AnchorAtScreen`
	 * placement (the only mode that authors a Placement.ScreenPosition);
	 * `AnchorOrbit` and `FixedWorldPosition` ignore the placement zone
	 * configuration regardless of `bEnabled`.
	 */
	inline FShotSolveResult SolveShot(
		const FComposableCameraShot& Shot,
		const FShotSolveContext& Context,
		const FShotPriorPose* PriorPose = nullptr,
		float DeltaTime = 0.f)
	{
		FShotSolveResult R;
		R.Aperture = Shot.Lens.Aperture;   // passthrough

		// Effective projection FOV: Manual mode uses authored FOV (exact
		// one-frame solve); SolvedFromBoundsFit uses prev frame (1-2 frame
		// convergence).
		const float EffectiveProjFOV = (Shot.Lens.FOVMode == EShotFOVMode::Manual)
			? Shot.Lens.ManualFOV
			: Context.PreviousFrameFOV;
		const float TanHalfHOR = FMath::Tan(FMath::DegreesToRadians(EffectiveProjFOV * 0.5f));

		// Pre-fill Lens / Focus so that *pose-failure* exits below still
		// emit a usable FieldOfView and FocusDistance. The editor viewport
		// applies these regardless of `bValid` so the Manual FOV / Manual
		// Focus sliders stay live even when the joint Picard fails to
		// converge or an anchor can't resolve. Runtime
		// `CompositionFramingNode` ignores `R` entirely on `bValid=false`
		// (it preserves the upstream pose for the frame), so the runtime
		// path is unaffected — these pre-fills are observable only via the
		// editor's "Lens always applied" path.
		//
		// Manual modes are exact: `ManualFOV` / `ManualDistance` reflect
		// the authored value 1:1 even without a valid pose.
		// `SolvedFromBoundsFit` and `Follow*` modes both need a pose to
		// project bounds / measure depth — on pose-failure we fall back to
		// the previous-frame FOV (best-known approximation) and the
		// authored `Focus.ManualDistance` (designer's safety value).
		R.FieldOfView = (Shot.Lens.FOVMode == EShotFOVMode::Manual)
			? Shot.Lens.ManualFOV
			: Context.PreviousFrameFOV;
		R.FocusDistance = Shot.Focus.ManualDistance;

		// ─── Distance damping (V2.2) ─────────────────────────────────
		// `Placement.DistanceSpeed > 0` + valid PriorPose with a stored
		// `LastDistance` ⇒ FInterpTo from prior toward authored. Falls
		// back to the authored value when any of those is missing — V1
		// "snap to authored" behavior.
		const float AuthoredDistance = Shot.Placement.Distance;
		float EffectiveDistance = AuthoredDistance;
		if (PriorPose != nullptr && PriorPose->LastDistance > 0.f
			&& Shot.Placement.DistanceSpeed > 0.f && DeltaTime > 0.f)
		{
			EffectiveDistance = FMath::FInterpTo(
				PriorPose->LastDistance, AuthoredDistance,
				DeltaTime, Shot.Placement.DistanceSpeed);
		}
		// 1cm floor — same invariant the per-mode pre-flights enforce.
		EffectiveDistance = FMath::Max(EffectiveDistance, 1.f);
		R.EffectiveDistance = EffectiveDistance;

		// ─── Roll damping (V2.2) ─────────────────────────────────────
		// `RollSpeed > 0` + valid prior `LastRoll` ⇒ wrap-aware IIR.
		// Sentinel for LastRoll is `FLT_MAX`; any other value means a
		// real prior is available.
		const float AuthoredRoll = Shot.Roll;
		float EffectiveRoll = AuthoredRoll;
		if (PriorPose != nullptr
			&& PriorPose->LastRoll != TNumericLimits<float>::Max()
			&& Shot.RollSpeed > 0.f && DeltaTime > 0.f)
		{
			EffectiveRoll = DampAngleDeg(
				PriorPose->LastRoll, AuthoredRoll, DeltaTime, Shot.RollSpeed);
		}

		// ─── Zone preprocessing ───────────────────────────────────────
		// Resolve effective screen targets for Aim and Placement (the
		// latter only when AnchorAtScreen actually consumes one). Each
		// resolver is a no-op when zones are disabled OR the prior pose
		// is null — both fall back to the authored ScreenPosition, and
		// the V1 path below runs unchanged.
		const FVector2D EffectiveAimScreen = (PriorPose != nullptr)
			? ResolveEffectiveScreenTarget(
				Shot.Aim.AimAnchor, Shot.Targets,
				Shot.Aim.ScreenPosition, Shot.Aim.AimZones,
				*PriorPose, TanHalfHOR, Context.ViewportAspectRatio, DeltaTime)
			: Shot.Aim.ScreenPosition;

		const FVector2D EffectivePlacementScreen = (PriorPose != nullptr
			&& Shot.Placement.Mode == EShotPlacementMode::AnchorAtScreen)
			? ResolveEffectiveScreenTarget(
				Shot.Placement.PlacementAnchor, Shot.Targets,
				Shot.Placement.ScreenPosition, Shot.Placement.PlacementZones,
				*PriorPose, TanHalfHOR, Context.ViewportAspectRatio, DeltaTime)
			: Shot.Placement.ScreenPosition;

		// Dispatch on Placement.Mode:
		//   - AnchorAtScreen → closed-form Position pass
		//     (`SolveAnchorAtScreenPos`) under an assumed rotation, then
		//     normal Aim pass for rotation. First-frame hard seeds run a
		//     joint Picard pass; prior-pose steady-state stays decoupled.
		//     PlacementAnchor's projected screen position drifts O(rotation
		//     delta) from `Placement.ScreenPosition` once the Aim pass
		//     diverges from the assumed rotation. Drift is washed out by
		//     framing-zone damping in practice.
		//   - other Placement modes → existing decoupled SolvePlacement →
		//     SolveAim pipeline.
		if (Shot.Placement.Mode == EShotPlacementMode::AnchorAtScreen)
		{
			FVector PlacementAnchorPos;
			if (!Shot.Placement.PlacementAnchor.ResolveWorldPosition(Shot.Targets, PlacementAnchorPos))
			{
				UE_LOG(LogComposableCameraSystem, Warning,
					TEXT("ShotSolver: Placement.PlacementAnchor unresolvable; skipping pose update."));
				return R;
			}
			// EffectiveDistance was already clamped to >= 1cm above.
			const float SafeDistance = EffectiveDistance;

			// Determine the assumed rotation for the Position pass.
				FRotator AssumedRot;
				bool bSolvedAnchorAtScreenJointSeed = false;
			if (Shot.Aim.Mode == EShotAimMode::NoOp)
			{
				// Aim NoOp: rotation is fixed at identity + EffectiveRoll.
				// The Aim pass below will simply re-emit this same value.
				AssumedRot = FRotator(0.f, 0.f, EffectiveRoll);
			}
			else   // LookAtAnchor
			{
				if (PriorPose != nullptr)
				{
					// Best decoupling: previous frame's rotation. Drift is
					// O(rotation delta per frame), typically a few pixels.
					AssumedRot = PriorPose->Rotation;
				}
				else
				{
					// First-frame seed: build a rotation looking from
					// PlacementAnchor toward AimAnchor (the typical OTS
					// orientation), composed with EffectiveRoll. Matches
					// what the Aim pass will compute closely enough that
					// the position-pass placement is sensibly close to
					// the authored ScreenPosition on frame 0.
					FVector AimAnchorPosForSeed;
					if (!Shot.Aim.AimAnchor.ResolveWorldPosition(Shot.Targets, AimAnchorPosForSeed))
					{
						UE_LOG(LogComposableCameraSystem, Warning,
							TEXT("ShotSolver: Aim.AimAnchor unresolvable; skipping pose update."));
						return R;
					}
					const FVector A2P = PlacementAnchorPos - AimAnchorPosForSeed;
					if (A2P.IsNearlyZero())
					{
						UE_LOG(LogComposableCameraSystem, Warning,
							TEXT("ShotSolver: AnchorAtScreen with PlacementAnchor == AimAnchor "
							     "is degenerate on the first-frame seed; skipping pose update. "
							     "Switch to AnchorOrbit for single-anchor framing."));
						return R;
					}
						const FVector InitForward = (-A2P).GetSafeNormal();
						AssumedRot = FRotationMatrix::MakeFromX(InitForward).Rotator();
						AssumedRot.Roll = EffectiveRoll;

						if (!SolveAnchorAtScreenLookAtJointSeed(
								PlacementAnchorPos, AimAnchorPosForSeed,
								EffectivePlacementScreen, EffectiveAimScreen,
								SafeDistance, EffectiveRoll,
								TanHalfHOR, Context.ViewportAspectRatio,
								AssumedRot,
								R.CameraPosition, R.CameraRotation))
						{
							return R;
						}
						bSolvedAnchorAtScreenJointSeed = true;
					}
				}

			// Position pass — closed-form.
				if (!bSolvedAnchorAtScreenJointSeed)
				{
				if (!SolveAnchorAtScreenPos(
						PlacementAnchorPos, EffectivePlacementScreen,
					SafeDistance, TanHalfHOR, Context.ViewportAspectRatio,
					AssumedRot,
					R.CameraPosition))
			{
				return R;
			}

			// Rotation pass.
			if (Shot.Aim.Mode == EShotAimMode::NoOp)
			{
				// Same identity + EffectiveRoll. SolveAim would also emit
				// this, but we already have the value — skip the call.
				R.CameraRotation = AssumedRot;
			}
			else   // LookAtAnchor
			{
				if (!SolveAim(Shot, R.CameraPosition, EffectiveAimScreen,
						EffectiveRoll, TanHalfHOR, Context.ViewportAspectRatio,
						R.CameraRotation))
				{
					return R;
				}
				}
				}
			}
			else
		{
			// Decoupled pipeline: Placement (Position) → Aim (Rotation).
			// Placement modes here (AnchorOrbit / FixedWorldPosition) do
			// not consume `Placement.ScreenPosition` — the placement zone
			// resolution above is correctly a no-op for these.
			if (!SolvePlacement(Shot, EffectiveDistance,
					TanHalfHOR, Context.ViewportAspectRatio, R.CameraPosition))
			{
				return R;
			}
			if (!SolveAim(Shot, R.CameraPosition, EffectiveAimScreen,
					EffectiveRoll, TanHalfHOR, Context.ViewportAspectRatio,
					R.CameraRotation))
			{
				return R;
			}
		}

		// Layer 3 — Lens (FOV).
		R.FieldOfView = SolveLens(Shot, R.CameraPosition, R.CameraRotation, Context);

		// FOV damping (V2.2). Sentinel for LastFOV is `< 0`; any
		// positive value means a real prior is available. Damping is
		// applied AFTER `SolveLens` so it covers both Manual and
		// SolvedFromBoundsFit modes uniformly — the latter benefits
		// from damping when the perceptual-box solve jumps because
		// targets enter / leave the bounds set.
		if (PriorPose != nullptr && PriorPose->LastFOV > 0.f
			&& Shot.Lens.FOVSpeed > 0.f && DeltaTime > 0.f)
		{
			R.FieldOfView = FMath::FInterpTo(
				PriorPose->LastFOV, R.FieldOfView,
				DeltaTime, Shot.Lens.FOVSpeed);
		}

		// Independent — Focus.
		R.FocusDistance = SolveFocus(Shot, R.CameraPosition, R.CameraRotation);

		R.bValid = true;
		return R;
	}

}   // namespace ComposableCameraSystem::ShotSolver
