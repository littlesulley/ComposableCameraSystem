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
 *   - Coupling between Placement.ScreenPosition (lateral camera shift) and
 *     Aim.ScreenPosition (rotation) is intentionally one-way: Placement
 *     determines Position with a TENTATIVE look-at-PlacementAnchor
 *     rotation; Aim then OVERRIDES rotation. So when AimAnchor !=
 *     PlacementAnchor, the placement anchor's *final* projected screen
 *     position drifts from `Placement.ScreenPosition`. Document this and
 *     let designers set both equal in the typical AimAnchor==PlacementAnchor
 *     case.
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
	 * Defined here (above the first caller, `SolveAnchorAtScreen`) so the
	 * order of inline definitions in this header matches the order of use
	 * — C++ requires inline functions to be defined before use within the
	 * same translation unit.
	 */
	inline FVector2D PreRotateScreenForRoll(
		const FVector2D& Authored, float CosRoll, float SinRoll, float AspectRatio)
	{
		return FVector2D(
			 Authored.X * CosRoll + (Authored.Y / AspectRatio) * SinRoll,
			-Authored.X * AspectRatio * SinRoll + Authored.Y * CosRoll);
	}

	// ─────────────────────────────────────────────────────────────────────
	// AnchorAtScreen — joint Placement + Aim solve
	// ─────────────────────────────────────────────────────────────────────

	/**
	 * Joint Position + Rotation solve for `EShotPlacementMode::AnchorAtScreen`.
	 *
	 * In this mode Placement borrows its forward axis from Aim — camera
	 * looks at AimAnchor with `Aim.ScreenPosition` constraint, AND
	 * PlacementAnchor must be at depth `Distance` and screen
	 * `Placement.ScreenPosition` in cam frame. That's 5 constraints
	 * (AimAnchor screen 2 + PlacementAnchor screen 2 + PlacementAnchor
	 * depth 1) on 5 unknowns (CamPos 3 + Pitch 1 + Yaw 1).
	 *
	 * **Iterative solve** (Picard fixed-point with damping — typically
	 * converges in 3-6 iterations). Closed-form is hard because the
	 * camera's right / up axes (which determine which world direction
	 * `Placement.ScreenPosition` shifts the camera) depend on the forward
	 * direction, which depends on CamPos itself. Iterating breaks the
	 * chicken-and-egg cleanly:
	 *
	 *   1. Pre-flight (return false on hard failures, clamp soft ones):
	 *      - `Distance < 1` → return false (caller pre-clamps via
	 *        `SafeDistance`; defensive secondary clamp).
	 *      - `dist_AP < ε` → return false (AimAnchor ≡ PlacementAnchor;
	 *        the joint solve has no canonical answer — designer should
	 *        switch to `AnchorOrbit` for single-anchor framing).
	 *      - Authored screen positions outside `[-0.49, +0.49]²` are
	 *        clamped (silent for valid envelope, warning when clamp fires).
	 *
	 *   2. Initial guess: place camera at `PlacementAnchor` displaced
	 *      `Distance` away along the (PlacementAnchor → AimAnchor)
	 *      direction (rough OTS starting point).
	 *
	 *   3. Loop (Picard with relaxation factor `α = 0.7`):
	 *      a. Compute camera rotation from current CamPos via
	 *         `SolveCameraRotationForScreenTarget(AimAnchor - CamPos,
	 *         AimScreenPos)` — gives (Pitch, Yaw) putting AimAnchor at
	 *         the authored screen position.
	 *      b. Candidate CamPos = `PlacementAnchor - R · (D, ly_p, lz_p)`.
	 *      c. Damped update: `CamPos ← (1-α)·CamPos + α·Candidate`.
	 *         Damping (0 < α < 1) suppresses oscillation under
	 *         off-center / short-distance geometries where the un-damped
	 *         iteration can ping-pong instead of converging.
	 *      d. Convergence on un-damped residual `||Candidate - CamPos||²
	 *         < (0.01 cm)²`. Damping doesn't change the fixed point, so
	 *         convergence on the raw step is the right signal.
	 *
	 *   4. Non-convergence → return false (caller preserves upstream pose
	 *      for the frame). The previous behavior of "warn + return last
	 *      estimate" silently produced wrong cameras; failing loud lets
	 *      the framing-node fallback take over and the designer's HUD
	 *      shows the previous-frame pose unchanged.
	 *
	 *   5. Compose `Shot.Roll` onto output rotation. Authored screen
	 *      positions pre-rotated by `-Roll` (anisotropic AR transform)
	 *      before the iteration so they end up at the original values
	 *      under the rolled rotation — same trick as `LookAtAnchor`.
	 *
	 * Note: `Aim.Mode == NoOp` is handled by the separate
	 * `SolveAnchorAtScreenIdentityRot` (closed-form algebraic, no Picard).
	 */
	inline bool SolveAnchorAtScreen(
		const FVector& AimAnchorPos,
		const FVector& PlacementAnchorPos,
		FVector2D AimScreenPos,
		FVector2D PlacementScreenPos,
		float Distance,
		float TanHalfHOR,
		float AspectRatio,
		float RollDeg,
		FVector& OutCamPos,
		FRotator& OutCamRot)
	{
		// Pre-flight 1: defensive Distance check. Caller is expected to
		// clamp via `SafeDistance`, but the joint solve is sensitive enough
		// to a sub-1cm distance that we re-check here and bail loudly.
		if (Distance < 1.f)
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("ShotSolver: AnchorAtScreen Distance=%.4f below 1cm floor; "
				     "skipping pose update."),
				Distance);
			return false;
		}

		// Pre-flight 2: anchor coincidence. The joint solve's initial
		// `(PlacementAnchor → AimAnchor)` direction is undefined when the
		// anchors collapse, and even with a fallback forward the iteration
		// produces a camera whose pose is meaningless (any Yaw / Pitch
		// satisfies the placement constraint trivially). Hard-fail and let
		// the caller preserve upstream pose; designer sees a stable shot
		// instead of a nonsense one.
		const FVector A2P = PlacementAnchorPos - AimAnchorPos;
		const float dist_AP = static_cast<float>(A2P.Size());
		if (dist_AP < UE_KINDA_SMALL_NUMBER)
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("ShotSolver: AnchorAtScreen with PlacementAnchor == AimAnchor "
				     "is degenerate; skipping pose update. Switch to AnchorOrbit "
				     "for single-anchor framing."));
			return false;
		}

		// Pre-flight 3: clamp authored screen positions to a soft frustum
		// envelope so the inner rotation solve has convergence margin.
		// Values inside `[-0.49, +0.49]²` pass through unchanged; values
		// outside trigger a one-shot warning + clamp so the designer sees
		// their input was modified.
		if (ClampAuthoredScreenPosition(AimScreenPos))
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("ShotSolver: AnchorAtScreen Aim.ScreenPosition clamped to %.2f²; "
				     "authored value was outside the frustum-safe envelope."),
				ShotSolverScreenClampLimit);
		}
		if (ClampAuthoredScreenPosition(PlacementScreenPos))
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("ShotSolver: AnchorAtScreen Placement.ScreenPosition clamped to %.2f²; "
				     "authored value was outside the frustum-safe envelope."),
				ShotSolverScreenClampLimit);
		}

		const float TanHalfVOR = TanHalfHOR / AspectRatio;
		const FVector InitForward = -A2P / dist_AP;

		// Pre-rotate authored screen positions by -Roll so the post-Roll
		// composition lands AimAnchor / PlacementAnchor at their authored
		// positions (anisotropic Roll-screen transform — see spec §4.8).
		const float RollRad = FMath::DegreesToRadians(RollDeg);
		const float CosR = FMath::Cos(RollRad);
		const float SinR = FMath::Sin(RollRad);
		const FVector2D AimSP_pre =
			PreRotateScreenForRoll(AimScreenPos, CosR, SinR, AspectRatio);
		const FVector2D PlSP_pre =
			PreRotateScreenForRoll(PlacementScreenPos, CosR, SinR, AspectRatio);

		// PlacementAnchor's lateral offsets in cam frame at depth `Distance`.
		const float ly_p = PlSP_pre.X * 2.f * TanHalfHOR * Distance;
		const float lz_p = PlSP_pre.Y * 2.f * TanHalfVOR * Distance;
		const FVector CamAnchorP(Distance, ly_p, lz_p);

		// Smarter initial CamPos seed (Polish D.2): instead of a pure
		// `PlacementAnchor - Distance * InitForward` — which assumes the
		// authored `Placement.ScreenPosition` is at center — pre-shift
		// laterally / vertically to land closer to the geometric solution
		// when PlSP is off-center. The initial cam axes come from the
		// tentative P→A forward; lateral shift uses the same
		// `2·TanH·Distance` projection the iterative step uses internally.
		// Reduces typical iteration count by ~1-2 and rescues a class of
		// off-center stress cases that previously hit `MaxIters` and
		// hard-failed.
		const FVector InitWorldUp(0.f, 0.f, 1.f);
		FVector InitRight = FVector::CrossProduct(InitWorldUp, InitForward);
		if (!InitRight.Normalize(UE_KINDA_SMALL_NUMBER))
		{
			// Degenerate: P→A is parallel to world up. Pick any horizontal
			// right axis to seed lateral shift.
			InitRight = FVector(0.f, 1.f, 0.f);
		}
		const FVector InitUp = FVector::CrossProduct(InitForward, InitRight);
		const float InitOffsetRight = -PlSP_pre.X * 2.f * TanHalfHOR * Distance;
		const float InitOffsetUp    = -PlSP_pre.Y * 2.f * TanHalfVOR * Distance;
		OutCamPos = PlacementAnchorPos
			- Distance * InitForward
			+ InitOffsetRight * InitRight
			+ InitOffsetUp * InitUp;

		// Picard iteration with damping. Converges in 3-6 iterations for
		// sensible geometries; damping (α, default 0.7) suppresses
		// oscillation for off-center ScreenPosition / short-Distance edge
		// cases. Tuning lives on `UComposableCameraProjectSettings` (P.3) —
		// projects with unusual scale / framing ranges can adjust without
		// recompiling. Read once into locals so per-iter `GetDefault<>`
		// cost stays out of the inner loop.
		const UComposableCameraProjectSettings* CCSSettings =
			GetDefault<UComposableCameraProjectSettings>();
		const int32 MaxIters = CCSSettings
			? FMath::Max(1, CCSSettings->PicardMaxIterations)
			: 16;
		const float ConvergenceTolCm = CCSSettings
			? FMath::Max(KINDA_SMALL_NUMBER, CCSSettings->PicardConvergenceTolerance)
			: 0.01f;
		const float ConvergenceSqThreshold = ConvergenceTolCm * ConvergenceTolCm;
		const float Relaxation = CCSSettings
			? FMath::Clamp(CCSSettings->PicardRelaxation, 0.05f, 1.0f)
			: 0.7f;
		FRotator IterRot(0.f, 0.f, 0.f);
		bool bConverged = false;
		for (int32 Iter = 0; Iter < MaxIters; ++Iter)
		{
			const FVector AimDir = AimAnchorPos - OutCamPos;
			if (AimDir.IsNearlyZero())
			{
				UE_LOG(LogComposableCameraSystem, Warning,
					TEXT("ShotSolver: AnchorAtScreen iteration: camera coincident "
					     "with AimAnchor; skipping pose update."));
				return false;
			}
			auto [PitchDeg, YawDeg] = SolveCameraRotationForScreenTarget(
				TanHalfHOR, AspectRatio, AimDir, AimSP_pre.X, AimSP_pre.Y);
			IterRot = FRotator(PitchDeg, YawDeg, 0.f);   // Roll=0 in pre-rotated frame
			const FQuat R = IterRot.Quaternion();

			const FVector CandidateCamPos = PlacementAnchorPos - R.RotateVector(CamAnchorP);

			// Convergence test on the un-damped residual: damping changes
			// the path length but not the fixed point, so the right signal
			// is "did the un-damped step shrink?".
			const float DeltaSq =
				static_cast<float>(FVector::DistSquared(CandidateCamPos, OutCamPos));

			// Damped update: blend toward the candidate by α.
			OutCamPos = FMath::Lerp(OutCamPos, CandidateCamPos, Relaxation);

			if (DeltaSq < ConvergenceSqThreshold)
			{
				// Recompute rotation from the final CamPos so the output
				// rotation matches the *converged* CamPos, not the rotation
				// computed from the previous (pre-damped) CamPos.
				const FVector AimDirFinal = AimAnchorPos - OutCamPos;
				auto [PitchDegF, YawDegF] = SolveCameraRotationForScreenTarget(
					TanHalfHOR, AspectRatio, AimDirFinal, AimSP_pre.X, AimSP_pre.Y);
				IterRot = FRotator(PitchDegF, YawDegF, 0.f);
				bConverged = true;
				break;
			}
		}

		if (!bConverged)
		{
			// Hard failure: the iterate didn't settle within tolerance.
			// Caller preserves upstream pose (CompositionFramingNode +
			// editor viewport client both honor `bValid == false` by
			// holding the previous frame's pose). This is strictly better
			// than the previous "warn + return last estimate" behavior,
			// which silently produced contorted cameras the designer had
			// no easy way to debug.
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("ShotSolver: AnchorAtScreen iteration did not converge in %d iters; "
				     "skipping pose update. Geometry may be physically unsolvable "
				     "(Distance too short relative to anchor separation, ScreenPosition "
				     "too off-center, etc.)."),
				MaxIters);
			return false;
		}

		// Compose authored Roll onto the final rotation. The pre-rotation
		// of screen targets above ensures AimAnchor and PlacementAnchor
		// land at their authored screen positions under the rolled view.
		OutCamRot = IterRot;
		OutCamRot.Roll = RollDeg;
		return true;
	}

	/**
	 * Direct algebraic solve for `AnchorAtScreen` + Aim NoOp combination.
	 *
	 * When Aim is NoOp the rotation is fixed at `(Pitch=0, Yaw=0, Roll=Shot.Roll)`
	 * — there's no Aim screen constraint, so the joint quadratic in
	 * `SolveAnchorAtScreen` doesn't apply. With rotation known the camera
	 * position is closed-form algebraic:
	 *
	 *     cam_anchor = (D, sx · 2·TanH · D, sy · 2·TanV · D)   // cam frame
	 *     CamPos     = PlacementAnchor - R(Roll) · cam_anchor
	 *
	 * which makes PlacementAnchor project to `ScreenPosition` at depth `D`
	 * under the Roll-only rotation.
	 */
	inline bool SolveAnchorAtScreenIdentityRot(
		const FVector& PlacementAnchorPos,
		FVector2D ScreenPos,
		float Distance,
		float TanHalfHOR,
		float AspectRatio,
		float RollDeg,
		FVector& OutCamPos,
		FRotator& OutCamRot)
	{
		// Pre-flight: defensive Distance check (caller pre-clamps via
		// `SafeDistance`, but algebraic path is brittle at sub-1cm depths).
		if (Distance < 1.f)
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("ShotSolver: AnchorAtScreen+NoOp Distance=%.4f below 1cm floor; "
				     "skipping pose update."),
				Distance);
			return false;
		}

		// Pre-flight: clamp ScreenPos to the soft frustum envelope so
		// authored values outside `[-0.49, +0.49]²` don't push the camera
		// to an unintended pose. Algebraic mode is more forgiving than the
		// joint solve (no iteration to diverge), but consistency matters
		// for designer feedback — same warning shape as the joint path.
		if (ClampAuthoredScreenPosition(ScreenPos))
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("ShotSolver: AnchorAtScreen+NoOp Placement.ScreenPosition "
				     "clamped to %.2f²; authored value was outside the frustum-safe "
				     "envelope."),
				ShotSolverScreenClampLimit);
		}

		const float TanHalfVOR = TanHalfHOR / AspectRatio;
		const FVector CamAnchor(
			Distance,
			ScreenPos.X * 2.f * TanHalfHOR * Distance,
			ScreenPos.Y * 2.f * TanHalfVOR * Distance);
		OutCamRot = FRotator(0.f, 0.f, RollDeg);
		OutCamPos = PlacementAnchorPos - OutCamRot.RotateVector(CamAnchor);
		return true;
	}

	/**
	 * Computes camera Position based on the Shot's Placement layer.
	 * Returns false (CamPos unchanged) when an essential anchor can't
	 * resolve — caller handles the no-pose fallback.
	 */
	inline bool SolvePlacement(
		const FComposableCameraShot& Shot,
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
				const float SafeDistance = FMath::Max(P.Distance, 1.f);
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
			// Joint solve handled in `SolveShot` orchestrator (needs Aim
			// data alongside Placement data). `SolvePlacement` is called
			// only for the decoupled-pipeline modes; in the joint mode the
			// orchestrator skips both `SolvePlacement` and `SolveAim` in
			// favor of `SolveAnchorAtScreen`.
			ensureMsgf(false,
				TEXT("SolvePlacement should not be called for "
				     "AnchorAtScreen — use joint solve."));
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
	// `PreRotateScreenForRoll` lives above the AnchorAtScreen section
	// (the joint solve calls it too) — see the helper block before
	// `SolveAnchorAtScreen` for the math derivation.

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
	 */
	inline bool SolveAim(
		const FComposableCameraShot& Shot,
		const FVector& CamPos,
		float TanHalfHOR,
		float AspectRatio,
		FRotator& OutRot)
	{
		const FShotAim& A = Shot.Aim;

		// NoOp short-circuits before AimAnchor resolution. Output =
		// identity rotation with `Shot.Roll` composed; AimAnchor /
		// Aim.ScreenPosition unread, so they may be left at any value
		// without producing a "unresolvable" failure.
		if (A.Mode == EShotAimMode::NoOp)
		{
			OutRot = FRotator(0.f, 0.f, Shot.Roll);
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
				const float RollRad = FMath::DegreesToRadians(Shot.Roll);
				OutRot = SolveLookAtAnchorRotation(
					CamPos, AimAnchorPos, A.ScreenPosition,
					RollRad, TanHalfHOR, AspectRatio);
				OutRot.Roll = Shot.Roll;   // compose roll
				return true;
			}
		case EShotAimMode::NoOp:
			// Handled above the AimAnchor resolution; case kept for
			// switch-coverage warnings in clang/gcc.
			return true;
		}
		return false;
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
	 */
	inline FShotSolveResult SolveShot(
		const FComposableCameraShot& Shot,
		const FShotSolveContext& Context)
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

		// Dispatch on (Placement.Mode, Aim.Mode):
		//   - AnchorAtScreen + LookAtAnchor → joint Position+Rotation
		//     solve (`SolveAnchorAtScreen`, 5-on-5 closed form).
		//   - AnchorAtScreen + NoOp         → direct algebraic solve
		//     (`SolveAnchorAtScreenIdentityRot`, R = Roll-only).
		//   - other Placement modes         → decoupled Placement→Aim
		//     pipeline (existing).
		if (Shot.Placement.Mode == EShotPlacementMode::AnchorAtScreen)
		{
			FVector PlacementAnchorPos;
			if (!Shot.Placement.PlacementAnchor.ResolveWorldPosition(Shot.Targets, PlacementAnchorPos))
			{
				UE_LOG(LogComposableCameraSystem, Warning,
					TEXT("ShotSolver: Placement.PlacementAnchor unresolvable; skipping pose update."));
				return R;
			}
			const float SafeDistance = FMath::Max(Shot.Placement.Distance, 1.f);

			if (Shot.Aim.Mode == EShotAimMode::NoOp)
			{
				// Aim NoOp: rotation fixed at identity + Roll. Algebraic
				// CamPos derived directly from PlacementAnchor + Distance
				// + ScreenPosition. Returns false when the algebraic
				// pre-flight rejects the input (sub-1cm Distance).
				if (!SolveAnchorAtScreenIdentityRot(
						PlacementAnchorPos, Shot.Placement.ScreenPosition,
						SafeDistance, TanHalfHOR, Context.ViewportAspectRatio,
						Shot.Roll,
						R.CameraPosition, R.CameraRotation))
				{
					return R;
				}
			}
			else   // LookAtAnchor (only other AimMode value)
			{
				FVector AimAnchorPos;
				if (!Shot.Aim.AimAnchor.ResolveWorldPosition(Shot.Targets, AimAnchorPos))
				{
					UE_LOG(LogComposableCameraSystem, Warning,
						TEXT("ShotSolver: Aim.AimAnchor unresolvable; skipping pose update."));
					return R;
				}
				if (!SolveAnchorAtScreen(
					AimAnchorPos, PlacementAnchorPos,
					Shot.Aim.ScreenPosition, Shot.Placement.ScreenPosition,
					SafeDistance,
					TanHalfHOR, Context.ViewportAspectRatio,
					Shot.Roll,
					R.CameraPosition, R.CameraRotation))
				{
					return R;
				}
			}
		}
		else
		{
			// Decoupled pipeline: Placement (Position) → Aim (Rotation).
			if (!SolvePlacement(Shot, TanHalfHOR, Context.ViewportAspectRatio, R.CameraPosition))
			{
				return R;
			}
			if (!SolveAim(Shot, R.CameraPosition, TanHalfHOR, Context.ViewportAspectRatio, R.CameraRotation))
			{
				return R;
			}
		}

		// Layer 3 — Lens (FOV).
		R.FieldOfView = SolveLens(Shot, R.CameraPosition, R.CameraRotation, Context);

		// Independent — Focus.
		R.FocusDistance = SolveFocus(Shot, R.CameraPosition, R.CameraRotation);

		R.bValid = true;
		return R;
	}

}   // namespace ComposableCameraSystem::ShotSolver
