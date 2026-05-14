// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "Math/Interval.h"
#include "Utils/ComposableCameraActorInputSource.h"
#include "ComposableCameraFocusPullNode.generated.h"

class AActor;
class UComposableCameraInterpolatorBase;

/**
 * Dynamically drives the camera pose's `FocusDistance` from the distance to a
 * target actor. Single-responsibility node. It only touches `FocusDistance`.
 * Everything else DoF needs (aperture, blade count, filmback, and the
 * `PhysicalCameraBlendWeight` that gates whether DoF is applied at all) is
 * expected to come from an upstream `LensNode`. The intended composition:
 *
 *     ... ->LensNode(FocalLength, Aperture, BlendWeight=1, FocusDistance=-1)
 *         ->FocusPullNode(drives FocusDistance from PivotActor)
 *         ->...
 *
 * LensNode's `FocusDistance = -1` sentinel is the "leave for downstream"
 * signal that pairs cleanly with this node. If LensNode instead writes a
 * concrete focus distance, FocusPullNode will **overwrite** it (last writer
 * wins on the pose). Both work, the sentinel just makes the intent
 * obvious at read time.
 *
 * Without any LensNode upstream, the pose's default `PhysicalCameraBlendWeight`
 * is 0 - `ApplyPhysicalCameraSettings()` will not route `FocusDistance` into
 * the post-process DoF slots regardless of what this node writes. Add a
 * LensNode (or at minimum wire `PhysicalCameraBlendWeight > 0` some other
 * way) or the node is a no-op at the renderer level.
 *
 * Target resolution matches the plugin's `CollisionPushNode` /
 * `OcclusionFadeNode` pattern (PivotActor + bone/socket + Z offset) so the
 * same context-parameter wiring flows to all three. Damping is optional and
 * uses the standard interpolator system (SpringDamper / IIR / SimpleSpring)
 * so the focus-pull rate matches the project's broader camera tuning.
 *
 * Per-tick formula:
 *
 *     TargetPoint  = Resolve(PivotActor, BoneName | PivotZOffset)
 *     CameraFwd    = OutCameraPose.Rotation.Vector()
 *     Depth        = (TargetPoint - OutCameraPose.Position) * CameraFwd
 *     if Depth <= 0: pass-through this tick (target is behind camera)
 *     Raw          = Depth + FocusDistanceOffset
 *     if bClampFocusDistance: Raw = FMath::Clamp(Raw, Clamp.Min, Clamp.Max)
 *     if FocusInterpolator:   Raw = Interp.Run(LastFocus->Raw, DeltaTime)
 *     OutCameraPose.FocusDistance = Raw
 *
 * `FocusDistance` is camera-space depth (distance along the view axis),
 * NOT Euclidean distance. That's what `ApplyPhysicalCameraSettings` and
 * the renderer's DoF system consume. For an off-axis target the two
 * diverge significantly (10 m @ 45 deg has depth ~7 m), so projecting onto
 * the camera forward is the correct reduction.
 *
 * First tick after activation bypasses the damping so the initial focus
 * distance snaps to the real depth (avoids a visible focus ramp from
 * whatever the pose's previous FocusDistance was).
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (ToolTip = "Drives the pose's FocusDistance from the distance to a target actor; aperture and DoF blend weight come from an upstream LensNode."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraFocusPullNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	UComposableCameraFocusPullNode() { PaletteCategory = TEXT("Focus & Effects"); }

public:
	virtual void OnInitialize_Implementation() override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

#if !UE_BUILD_SHIPPING
	virtual void DrawNodeDebug(UWorld* World, bool bViewerIsOutsideCamera) const override;
#endif

public:
	// --- Target (aligned with CollisionPushNode's pivot pattern) ----------

	/** Selects whether the focus target actor is the controller's controlled
	 *  pawn or the explicitly supplied PivotActor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraActorInputSource PivotActorSource { EComposableCameraActorInputSource::ExplicitActor };

	/** Actor whose distance from the camera drives the focus distance.
	 *  Typically the player pawn or a narrative focal actor. Required. The
	 *  node pass-throughs with a warning each tick when unset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "PivotActorSource == EComposableCameraActorInputSource::ExplicitActor", EditConditionHides))
	TObjectPtr<AActor> PivotActor { nullptr };

	/** When true, the target point is sampled at the named bone / socket on
	 *  PivotActor's skeletal mesh (if present and resolvable). Falls back to
	 *  ActorLocation + PivotZOffset on any failure. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	bool bUseBoneForDetection { false };

	/** Bone / socket name, sampled when bUseBoneForDetection is true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "bUseBoneForDetection == true"))
	FName BoneName;

	/** World-Z offset added to ActorLocation when bUseBoneForDetection is
	 *  false (or the requested bone can't be found). Typical 50-0 to land
	 *  on a chest/head target rather than foot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "bUseBoneForDetection == false"))
	float PivotZOffset { 50.f };

	// --- Behavior ---------------------------------------------------------

	/** Master toggle. When false, the node is a no-op pass-through this tick
	 * . The previous FocusDistance on the pose is preserved. Useful for
	 *  Blueprint-driven "focus hold" moments (aim down sights, cinematic
	 *  freeze, etc.) where external logic wants to take over. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	bool bEnableFocusPull { true };

	/** Constant offset added to the on-axis camera-to-target depth before clamp
	 *  and damping. Positive = focus farther along the view axis than the
	 *  target (e.g. focus slightly past the subject); negative = focus
	 *  nearer. Applied to the projected depth, not Euclidean distance, so
	 *  it stays visually consistent regardless of how off-axis the target is. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	float FocusDistanceOffset { 0.f };

	// --- Clamp ------------------------------------------------------------

	/** When true, the resolved focus distance is clamped to FocusDistanceClamp. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	bool bClampFocusDistance { false };

	/** Min/max range applied when bClampFocusDistance is true. Min clamps
	 *  against micro-distances (rarely useful below ~10 cm; the renderer's
	 *  near plane usually dominates before that). Max clamps against the
	 *  "background"-tier distances that would produce no visible DoF anyway. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "bClampFocusDistance == true"))
	FFloatInterval FocusDistanceClamp { 10.f, 100000.f };

	// --- Smoothing --------------------------------------------------------

	/** Optional interpolator applied to the focus distance each tick. When
	 *  null, the node is stateless and the pose's FocusDistance equals the
	 *  raw (clamped, offset-adjusted) distance every frame. Visually this
	 *  means focus tracks the target with zero lag, which is jittery for
	 *  fast-moving targets. Pick any of the built-in interpolators
	 *  (SpringDamper / IIR / SimpleSpring) to get a smooth "focus pull"
	 *  that matches the camera's broader tuning. First tick after
	 *  activation bypasses the interpolator to avoid a visible ramp from
	 *  the previous FocusDistance value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = InputParameters)
	TObjectPtr<UComposableCameraInterpolatorBase> FocusInterpolator;

private:
	// --- Helpers ----------------------------------------------------------

	/** Resolve the target world location from the selected actor source +
	 *  BoneName / PivotZOffset. Returns false when no actor resolves. Mirrors
	 *  OcclusionFadeNode::ResolveTargetPoint. */
	bool ResolveTargetPoint(FVector& OutTargetPoint) const;

	/** Runtime instance built from FocusInterpolator in OnInitialize. Null
	 *  when no interpolator is configured or the asset's
	 *  BuildDoubleInterpolator returns null. */
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<double>>> FocusInterpolator_T;

	/** Persistent smoothed focus distance between frames. Seeded on the
	 *  first tick from the raw distance so frame-zero doesn't drift in
	 *  from an arbitrary sentinel. */
	double LastSmoothedFocusDistance { 0.0 };

	/** Cleared in OnInitialize; set true on first successful tick so re-
	 *  activation re-seeds. */
	bool bHasSeededSmoothing { false };

	/** Suppresses per-frame log spam while the focus target is unresolved. */
	bool bHasWarnedMissingTarget { false };

#if !UE_BUILD_SHIPPING
	// --- Debug mirrors (populated by OnTickNode, consumed by DrawNodeDebug) ---

	mutable FVector DebugTargetPoint { FVector::ZeroVector };
	mutable FVector DebugCameraPosition { FVector::ZeroVector };
	mutable FRotator DebugCameraRotation { FRotator::ZeroRotator };
	mutable double DebugResolvedFocusDistance { 0.0 };
	mutable bool bDebugWasDrivenThisTick { false };
#endif
};
