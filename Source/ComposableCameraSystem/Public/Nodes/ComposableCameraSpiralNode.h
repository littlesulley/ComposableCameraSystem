// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "Utils/ComposableCameraActorInputSource.h"
#include "ComposableCameraSpiralNode.generated.h"

class UCurveFloat;

/**
 * Source of the pivot the spiral is built around.
 *
 * FromActor  - PivotActor->GetActorLocation() is sampled each frame. The
 *              actor's Up / Forward are also available as Spiral-Space axis
 *              sources.
 * FromVector - PivotPosition is used directly. When this mode is active,
 *              RotationAxis = PivotActorUp and ReferenceDirection =
 *              PivotActorForward silently fall back to WorldUp / WorldX
 *              with a runtime warning because there is no actor to read from.
 */
UENUM()
enum class EComposableCameraSpiralPivotSourceType : uint8
{
	FromActor,
	FromVector
};

/**
 * Axis around which the camera orbits. Defines Spiral Space's Up direction.
 */
UENUM()
enum class EComposableCameraSpiralRotationAxis : uint8
{
	WorldUp,
	PivotActorUp,
	Custom
};

/**
 * Direction that anchors theta = 0 in the plane perpendicular to the rotation
 * axis. Defines Spiral Space's Forward direction after projection. The chosen
 * direction is projected onto the plane perpendicular to the rotation axis
 * and renormalized; it does not need to be pre-orthogonal to the axis.
 *
 * CameraInitialForward captures the camera's forward vector on the first
 * tick after activation and reuses it for the lifetime of the node, so the
 * spiral starts seamlessly from the current camera orientation.
 *
 * PivotActorInitialForward captures the resolved pivot actor's forward vector
 * on the first tick where the actor is valid, then reuses it for the node lifetime.
 */
UENUM()
enum class EComposableCameraSpiralReferenceDirection : uint8
{
	WorldX = 0,
	PivotActorForward = 1,
	CameraInitialForward = 2,
	// Declared before Custom for UI order; value stays appended for asset compatibility.
	PivotActorInitialForward = 4,
	Custom = 3
};

/**
 * How the spiral evolves past Duration seconds. In every mode, theta / Radius / Height
 * are direct curve evaluations at NormalizedTime; there is no per-frame integration
 * and no accumulated state, so the pose at any arbitrary t is computable in O(1).
 *
 * Once      - NormalizedTime clamps at 1 after Duration; all three curves hold their
 *             Y at X=1. The pose freezes at the terminal frame.
 * Loop      - NormalizedTime = Fmod(Elapsed, Duration) / Duration. Theta visually wraps
 *             cleanly when AngleCurve's Y(1) - Y(0) is a multiple of 360 (trig
 *             periodicity absorbs the jump); non-multiples snap at the cycle seam,
 *             which is the author's explicit choice.
 * PingPong  - NormalizedTime oscillates 0 -> 1 -> 0 -> 1 every 2 * Duration seconds.
 *             All three curves are sampled at the mirrored time, so theta / Radius /
 *             Height naturally retrace on the return half. No sign flip is needed;
 *             the X mirror alone carries the symmetry.
 */
UENUM()
enum class EComposableCameraSpiralPlayMode : uint8
{
	Once,
	Loop,
	PingPong
};

/**
 * Positions the camera on a helical path around a pivot point.
 *
 * Position-only node; rotation is left untouched, to be authored by a
 * downstream LookAtNode (or similar). The position formula, evaluated each
 * tick, is:
 *
 *     P(t) = EffectivePivot
 *          + Axis * HeightCurve(NormalizedTime)
 *          + PerpDir(theta) * RadiusCurve(NormalizedTime)
 *
 *     EffectivePivot = ResolvedPivot
 *                    + Forward * PivotOffset.X
 *                    + Right   * PivotOffset.Y
 *                    + Axis    * PivotOffset.Z
 *
 *     PerpDir(theta) = Forward * cos(theta) + Right * sin(theta)
 *     theta          = InitialAngleDegrees + AngleCurve(NormalizedTime)
 *
 * Where the Spiral-Space basis (Up, Forward, Right) is resolved from
 * RotationAxis and ReferenceDirection each tick; Forward is the
 * ReferenceDirection vector projected onto the plane perpendicular to Axis
 * and renormalized, and Right = Cross(Axis, Forward).
 *
 * Curve authoring convention (Progress pattern, matching SplineNode's
 * AutomaticMoveCurve): all three curves use X in [0, 1] as normalized time
 * within Duration; Y in absolute world units: Radius / Height in cm,
 * AngleCurve in degrees. Direct curve evaluation means position at any
 * arbitrary t is O(1) computable; no integration history, no accumulated
 * state. A Loop-mode orbit typically authors AngleCurve as Y(0)=0,
 * Y(1)=360*N for a seamless N-turn cycle; non-360 multiples produce an
 * intentional retrace at the cycle seam.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (ToolTip = "Positions the camera on a helical path around a pivot; rotation is left for a downstream LookAtNode."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraSpiralNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	UComposableCameraSpiralNode() { PaletteCategory = TEXT("Position"); }

public:
	virtual void OnInitialize_Implementation() override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

#if !UE_BUILD_SHIPPING
	virtual void DrawNodeDebug(UWorld* World, bool bViewerIsOutsideCamera) const override;
#endif

public:
	// Pivot source

	/** Whether the pivot comes from an Actor's location or a raw vector. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraSpiralPivotSourceType PivotSourceType { EComposableCameraSpiralPivotSourceType::FromActor };

	/** Selects whether PivotActor is read from the controller's controlled
	 *  pawn or from the explicitly supplied PivotActor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "PivotSourceType == EComposableCameraSpiralPivotSourceType::FromActor", EditConditionHides))
	EComposableCameraActorInputSource PivotActorSource { EComposableCameraActorInputSource::ExplicitActor };

	/** Actor whose world location is used as the pivot. Typically driven by
	 *  an upstream ReceivePivotActorNode's output, or set on the instance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "PivotSourceType == EComposableCameraSpiralPivotSourceType::FromActor && PivotActorSource == EComposableCameraActorInputSource::ExplicitActor", EditConditionHides))
	TObjectPtr<AActor> PivotActor { nullptr };

	/** Raw pivot position in world space. Typically driven by an upstream
	 *  pivot-producing node via wire (PivotOffsetNode's output). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "PivotSourceType == EComposableCameraSpiralPivotSourceType::FromVector", EditConditionHides))
	FVector PivotPosition { FVector::ZeroVector };

	/** Offset applied to the resolved pivot, expressed in Spiral Space:
	 *  X = along Forward, Y = along Right, Z = along Axis (Up). Spiral Space
	 *  is re-derived from RotationAxis and ReferenceDirection each tick, so
	 *  this offset automatically tracks a pivot actor's orientation when
	 *  Spiral Space is anchored to the actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	FVector PivotOffset { FVector::ZeroVector };

	// Spiral Space

	/** The axis around which the camera orbits. PivotActorUp silently falls
	 *  back to WorldUp with a warning when PivotSourceType == FromVector. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraSpiralRotationAxis RotationAxis { EComposableCameraSpiralRotationAxis::WorldUp };

	/** Custom rotation axis (world space). Normalized at runtime; falls back
	 *  to WorldUp if near-zero. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "RotationAxis == EComposableCameraSpiralRotationAxis::Custom", EditConditionHides))
	FVector CustomAxis { FVector::UpVector };

	/** The direction that anchors theta = 0 in the plane perpendicular to
	 *  RotationAxis. PivotActorForward and PivotActorInitialForward silently
	 *  fall back to WorldX with a warning when PivotSourceType == FromVector. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraSpiralReferenceDirection ReferenceDirection { EComposableCameraSpiralReferenceDirection::CameraInitialForward };

	/** Custom theta = 0 direction (world space). Projected onto the plane
	 *  perpendicular to the rotation axis at runtime. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "ReferenceDirection == EComposableCameraSpiralReferenceDirection::Custom", EditConditionHides))
	FVector CustomDirection { FVector::ForwardVector };

	/** Starting angular offset applied to theta. Added to the value read from
	 *  AngleCurve each tick, so the spiral can begin at any azimuth around
	 *  the axis without re-authoring the curve. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	float InitialAngleDegrees { 0.f };

	// Time-varying shape (Y = absolute world units)

	/** Radial distance from Axis over normalized time. X in [0, 1], Y in cm.
	 *  A null curve is treated as a constant 0 radius (camera collapses onto Axis). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	TObjectPtr<UCurveFloat> RadiusCurve { nullptr };

	/** Signed distance along Axis from the pivot over normalized time.
	 *  X in [0, 1], Y in cm (positive = along Axis, negative = against Axis).
	 *  A null curve is treated as a constant 0 height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	TObjectPtr<UCurveFloat> HeightCurve { nullptr };

	/** Angular position (degrees, absolute) over normalized time. X in [0, 1],
	 *  Y in degrees; positive = right-handed rotation around Axis. Progress
	 *  pattern, same as SplineNode's AutomaticMoveCurve: theta at any instant is
	 *  a direct curve read, not an integral of speed. A null curve is
	 *  treated as a constant 0 angle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	TObjectPtr<UCurveFloat> AngleCurve { nullptr };

	// Timing

	/** Length of one "cycle" of the three curves, in seconds. Values at or
	 *  below SMALL_NUMBER are treated as a degenerate duration; all three
	 *  curves are sampled at NormalizedTime = 0 and the pose stays frozen
	 *  at the initial frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (ClampMin = "0.0"))
	float Duration { 3.f };

	/** How the node behaves after the first cycle ends. See enum comment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraSpiralPlayMode PlayMode { EComposableCameraSpiralPlayMode::Once };

private:
	// Per-activation state (reset in OnInitialize)

	/** Seconds elapsed since OnInitialize. Drives NormalizedTime. Accumulated
	 *  unbounded; Fmod inside OnTickNode handles the wrap. Float precision
	 *  on the ElapsedTime input to Fmod remains acceptable for realistic
	 *  gameplay durations (hours at 60 fps); if this node is ever used for
	 *  multi-day always-on installations, wrap ElapsedTime once per cycle. */
	float ElapsedTime { 0.f };

	/** Camera forward vector captured on the first tick. Used only when
	 *  ReferenceDirection == CameraInitialForward. */
	FVector CapturedInitialForward { FVector::ForwardVector };

	/** True once CapturedInitialForward has been written. Cleared in
	 *  OnInitialize so re-activation captures a fresh forward. */
	bool bHasCapturedInitialForward { false };

	// Pivot actor forward captured on the first tick with a valid pivot actor.
	FVector CapturedInitialPivotActorForward { FVector::ForwardVector };

	// Cleared on activation so each camera run captures a fresh pivot actor forward.
	bool bHasCapturedInitialPivotActorForward { false };

	// Spiral Space resolution

	/** Resolve the pivot location from PivotSourceType + PivotActor/PivotPosition.
	 *  Returns false and logs an error when the chosen source is invalid
	 *  (null actor). Output is written to OutPivot. */
	bool ResolvePivot(FVector& OutPivot) const;

	/** Resolve the Spiral-Space basis (Axis, Forward, Right). Forward is the
	 *  chosen ReferenceDirection projected onto the plane perpendicular to
	 *  Axis and renormalized. Falls back to WorldUp / WorldX on degeneracy. */
	void ResolveSpiralBasis(FVector& OutAxis, FVector& OutForward, FVector& OutRight);

#if !UE_BUILD_SHIPPING
	// Debug mirrors (populated by OnTickNode, consumed by DrawNodeDebug)

	FVector DebugEffectivePivot { FVector::ZeroVector };
	FVector DebugAxis { FVector::UpVector };
	FVector DebugForward { FVector::ForwardVector };
	FVector DebugRight { FVector::RightVector };
	float DebugCurrentAngleDegrees { 0.f };
	FVector DebugCurrentPosition { FVector::ZeroVector };
#endif
};
