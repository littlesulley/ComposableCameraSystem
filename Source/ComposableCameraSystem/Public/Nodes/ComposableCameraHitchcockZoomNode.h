// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "Math/Interval.h"
#include "ComposableCameraHitchcockZoomNode.generated.h"

class AActor;
class UCurveFloat;

/**
 * Which authored quantity the node drives. The other is solved from the
 * frame-zero lock constant.
 *
 * FromFOVDelta       — author `FOVDeltaCurve`, derive camera distance.
 *                      Natural when you think about the look of the effect
 *                      ("background should distort to N degrees wider").
 * FromDistanceDelta  — author `DistanceDeltaCurve`, derive FOV.
 *                      Natural when you think about the physical move
 *                      ("camera dollies back 3 metres").
 *
 * Both paths preserve the same `distance · tan(FOV/2) = LockConstant`
 * invariant captured on the first tick, so the two authoring styles are
 * physically equivalent — the choice is purely about which curve is
 * easier to shape in the project's authoring pipeline.
 */
UENUM()
enum class EComposableCameraHitchcockZoomDriver : uint8
{
	FromFOVDelta,
	FromDistanceDelta
};

/**
 * The Hitchcock Zoom (also known as the Vertigo effect, dolly zoom, or
 * trombone shot): the camera dollies along its view axis while the FOV
 * changes in the opposite direction, such that the target subject keeps
 * roughly the same on-screen size while the background perspective warps
 * dramatically.
 *
 * Per-tick math:
 *
 *     let InitialDistance = |UpstreamPos_0 - TargetPoint_0|
 *     let InitialFOV      = InitialFOVOverride if > 0
 *                           else OutCameraPose.GetEffectiveFieldOfView()
 *     let LockConstant    = InitialDistance * tan(radians(InitialFOV / 2))
 *
 *     NormalizedTime = min(ElapsedTime / Duration, 1)   // Once-only: clamps
 *     Direction      = (UpstreamPos - TargetPoint).SafeNormal()
 *
 *     if Driver == FromFOVDelta:
 *         FOV(t)  = InitialFOV + FOVDeltaCurve(NormalizedTime)
 *         Dist(t) = LockConstant / tan(radians(FOV(t) / 2))
 *     else:  // FromDistanceDelta
 *         Dist(t) = InitialDistance + DistanceDeltaCurve(NormalizedTime)
 *         FOV(t)  = 2 * degrees(atan(LockConstant / Dist(t)))
 *
 *     OutPose.Position    = TargetPoint + Direction * Dist(t)
 *     OutPose.FieldOfView = FOV(t)
 *     OutPose.FocalLength = -1                 // sentinel: FOV-mode authoritative
 *
 * **Curve convention — additive delta, Y(0) = 0.** Both curves express the
 * *change* from the captured initial state, not the absolute trajectory.
 * A curve of Y(0)=0, Y(1)=-30 on FOVDeltaCurve says "narrow the FOV by 30
 * degrees over the duration", regardless of whether InitialFOV was 60 or
 * 90. This keeps curves portable across cameras and guarantees the first
 * tick outputs `InitialFOV` / `InitialDistance` exactly — no seam at t=0.
 *
 * **Direction is resampled every tick** from the upstream pose, not
 * frozen at activation. This lets an upstream LookAt / CameraOffset
 * continue to steer the view direction during the effect — Hitchcock
 * only owns the radial distance + FOV, leaving rotation composable with
 * the rest of the chain.
 *
 * **FOV ownership.** The node writes `FieldOfView` and clears
 * `FocalLength` to -1 (pose's "FOV-mode" sentinel). If an upstream
 * LensNode is in the chain, set its `bOverrideFieldOfViewFromFocalLength`
 * to false so it doesn't fight for FOV authorship — LensNode's focal
 * length / aperture / blade count still flow through, but FOV stays
 * under this node's control. Alternatively, place HitchcockZoom *after*
 * any FOV-writing node and it will simply overwrite them (last writer
 * wins on the pose).
 *
 * PlayMode is implicit: Once. There is no Loop / PingPong — authors who
 * need a cyclic dolly zoom can drive the node externally (via re-
 * activation or by repeatedly resetting the camera context).
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (ToolTip = "Hitchcock Zoom / Vertigo effect — dollies the camera along its view axis while changing FOV so the target subject keeps constant on-screen size."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraHitchcockZoomNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	UComposableCameraHitchcockZoomNode() { PaletteCategory = TEXT("Focus & Effects"); }

public:
	virtual void OnInitialize_Implementation() override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

#if !UE_BUILD_SHIPPING
	virtual void DrawNodeDebug(UWorld* World, bool bViewerIsOutsideCamera) const override;
#endif

public:
	// ─── Target (aligned with CollisionPush / FocusPull) ──────────────────

	/** The subject the effect locks on. Camera dollies along the
	 *  camera→subject axis; FOV compensates so this subject stays the
	 *  same on-screen size. Required — the node is a pass-through with
	 *  a warning when null. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	TObjectPtr<AActor> PivotActor { nullptr };

	/** When true, target point is the named bone / socket on PivotActor's
	 *  skeletal mesh (if resolvable). Falls back to ActorLocation +
	 *  PivotZOffset on any failure. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	bool bUseBoneForDetection { false };

	/** Bone / socket name. Sampled when bUseBoneForDetection is true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "bUseBoneForDetection == true"))
	FName BoneName;

	/** World-Z offset added to ActorLocation when bUseBoneForDetection is
	 *  false (or the bone can't be found). Typical 50–80 to land on chest
	 *  / head rather than foot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "bUseBoneForDetection == false"))
	float PivotZOffset { 50.f };

	// ─── Initial-state override ───────────────────────────────────────────

	/** Baseline FOV (degrees) captured as InitialFOV on the first tick.
	 *  When > 0, this value wins regardless of what the upstream pose
	 *  carried for FieldOfView — the typical case for camera type assets
	 *  that have no `FieldOfViewNode` / `LensNode` upstream and would
	 *  otherwise inherit a renderer default on the first tick.
	 *
	 *  When ≤ 0 (the default `-1` sentinel matches the plugin's FOV-mode
	 *  sentinel in LensNode and `FComposableCameraPose`), falls back to
	 *  `OutCameraPose.GetEffectiveFieldOfView()` as read from the upstream
	 *  chain — the previous behaviour.
	 *
	 *  Only consulted on the first tick the node captures state. After
	 *  that, `LockConstant` is frozen against whichever FOV was used,
	 *  and subsequent ticks derive FOV from the Driver curve. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (ClampMin = "-1.0"))
	float InitialFOVOverride { -1.f };

	// ─── Driver selection ─────────────────────────────────────────────────

	/** Which authored curve drives the effect. The other quantity is
	 *  solved from the lock constant captured on the first tick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraHitchcockZoomDriver Driver { EComposableCameraHitchcockZoomDriver::FromFOVDelta };

	/** Additive FOV delta (degrees) over normalized time. X ∈ [0, 1],
	 *  Y is DELTA from InitialFOV — author Y(0) = 0 so the first tick
	 *  preserves InitialFOV exactly. Positive Y widens the FOV, negative
	 *  narrows (the classic "zoom in as the camera dollies out" is a
	 *  curve with negative Y values).
	 *
	 *  A null curve is treated as identically zero — the node then leaves
	 *  FOV at InitialFOV and camera at InitialDistance for the full
	 *  duration, which is useful as a placeholder during blockout. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "Driver == EComposableCameraHitchcockZoomDriver::FromFOVDelta", EditConditionHides))
	TObjectPtr<UCurveFloat> FOVDeltaCurve { nullptr };

	/** Additive distance delta (world units) over normalized time. X ∈
	 *  [0, 1], Y is DELTA from InitialDistance — author Y(0) = 0. Positive
	 *  Y dollies the camera back (away from subject), negative Y pushes in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "Driver == EComposableCameraHitchcockZoomDriver::FromDistanceDelta", EditConditionHides))
	TObjectPtr<UCurveFloat> DistanceDeltaCurve { nullptr };

	// ─── Timing ───────────────────────────────────────────────────────────

	/** Seconds the effect takes to play from start to end state. After
	 *  Duration elapses, the curves are evaluated at NormalizedTime = 1
	 *  and the pose freezes at the final state for as long as the node
	 *  remains active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (ClampMin = "0.0"))
	float Duration { 3.f };

	// ─── Safety / behavior ────────────────────────────────────────────────

	/** Master toggle. When false, the node is a pass-through this tick
	 *  (no camera dolly, no FOV override). Useful for Blueprint-gated
	 *  effects: trigger the activation + enable on cue, disable on cut. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	bool bEnable { true };

	/** When true, the derived camera distance is clamped to
	 *  CameraDistanceClamp. Prevents pathological curve shapes from
	 *  flying the camera off to kilometres away (very narrow FOV) or
	 *  plunging into / past the subject (very wide FOV). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	bool bClampCameraDistance { false };

	/** Min/max on the camera-to-subject distance when bClampCameraDistance
	 *  is true. Min should be larger than the subject's extent so the
	 *  camera doesn't end up inside the actor; Max bounds the "infinite
	 *  corridor" look that a near-zero derived FOV produces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "bClampCameraDistance == true"))
	FFloatInterval CameraDistanceClamp { 50.f, 10000.f };

private:
	/** Elapsed seconds since the effect first ran. Drives NormalizedTime. */
	float ElapsedTime { 0.f };

	/** Captured on the first tick the node actually runs (PivotActor
	 *  resolved, bEnable true). FOV is the *effective* FOV of the
	 *  upstream pose (handles FOV-mode / FocalLength-mode conversion via
	 *  `GetEffectiveFieldOfView`). */
	double InitialDistance { 0.0 };
	double InitialFOVDegrees { 0.0 };

	/** Invariant preserved for the full effect duration:
	 *  `distance * tan(radians(FOV / 2))`. Captured from the initial
	 *  distance and FOV on the first tick. */
	double LockConstant { 0.0 };

	/** Cleared in OnInitialize; set true once the initial state has been
	 *  captured. Re-activation resets via OnInitialize. */
	bool bHasCapturedInitialState { false };

	/** Resolve the target world location from PivotActor + BoneName /
	 *  PivotZOffset. Returns false when PivotActor is null. */
	bool ResolveTargetPoint(FVector& OutTargetPoint) const;

#if !UE_BUILD_SHIPPING
	mutable FVector DebugTargetPoint { FVector::ZeroVector };
	mutable FVector DebugCameraPosition { FVector::ZeroVector };
	mutable FRotator DebugCameraRotation { FRotator::ZeroRotator };
	mutable double DebugCurrentDistance { 0.0 };
	mutable double DebugCurrentFOV { 0.0 };
	mutable bool bDebugDrivenThisTick { false };
#endif
};
