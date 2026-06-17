// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "ComposableCameraVolumeConstraintNode.generated.h"

class AActor;
class UComposableCameraInterpolatorBase;

/**
 * How the constraint volume is sourced.
 *
 * FromActor: Pull the shape from the first `UShapeComponent` on VolumeActor.
 *             UBoxComponent and USphereComponent are supported; the component's
 *             world transform + scaled extents drive the volume. Capsule and
 *             other shape subclasses are rejected with a warning.
 * Inline: The node carries its own world-space volume definition via
 *             VolumeCenter / VolumeRotation / BoxExtents / SphereRadius.
 */
UENUM()
enum class EComposableCameraVolumeSource : uint8
{
	FromActor,
	Inline
};

/**
 * Shape of the constraint volume in Inline mode. FromActor mode resolves the
 * shape from the component's concrete class.
 */
UENUM()
enum class EComposableCameraVolumeShape : uint8
{
	Box,
	Sphere
};

/**
 * Constrains the camera to stay inside a single world-space volume. When the
 * upstream camera position is outside the volume, it is projected to the
 * nearest point on the volume's boundary. When it is inside, the node is a
 * no-op and passes the pose through untouched.
 *
 * Single volume only (MVP). Multiple-volume setups with priority / blend
 * radius (PostProcessVolume-style) are not supported; swap cameras via
 * transitions to change the active volume. Only the "keep inside" semantic
 * is implemented; "keep outside" (forbidden region) is not.
 *
 * The clamp is a hard projection by default. An optional ClampInterpolator
 * adds per-axis temporal smoothing so three discontinuity modes stop reading
 * as visible snaps:
 *
 *   1. **Release snap**. Upstream crosses from outside to inside in one
 *      frame. Without smoothing the output jumps from the boundary point
 *      back to the freely-moving upstream position.
 *   2. **Corner face switch**. Upstream orbits past a corner where the
 *      nearest-point face flips (e.g. +X face->corner ->+Y face). Position
 *      is still Lipschitz continuous but the tangent direction can change
 *      abruptly, reading as a crease.
 *   3. **Teleport / warp**. Any scripted camera jump across the boundary.
 *
 * When ClampInterpolator is null, the node is fully stateless and the pose
 * is deterministic given the upstream input. When it is set, per-axis
 * smoothing introduces controllable lag.
 *
 * Position formula each tick:
 *
 *     let Volume = Resolve(VolumeSource)
 *     if IsInside(OutPose.Position, Volume):
 *         OutPose.Position unchanged
 *     else:
 *         OutPose.Position = NearestPointInsideVolume(OutPose.Position, Volume)
 *
 * For a Box volume (OBB), "nearest point" is computed in the volume's local
 * space by per-axis clamping against the half-extents, then transformed back
 * to world. For a Sphere, it is `Center + (Pos - Center).SafeNormal * Radius`.
 *
 * Chain placement: runs on the camera's position, so put it after
 * `CameraOffsetNode` / `LookAtNode` / any position-writing node that
 * produces the "desired" position, and before `CollisionPushNode` so the
 * collision push operates on the already-clamped input.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (ToolTip = "Constrains the camera position to stay inside a single Box or Sphere volume, clamping to the nearest boundary point when outside."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraVolumeConstraintNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	UComposableCameraVolumeConstraintNode() { PaletteCategory = TEXT("Collision & Occlusion"); }

public:
	virtual void OnInitialize_Implementation() override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

#if !UE_BUILD_SHIPPING
	virtual void DrawNodeDebug(FComposableCameraDebugDrawSink& Draw, bool bViewerIsOutsideCamera) const override;
#endif

public:
	// --- Volume source ----------------------------------------------------

	/** Where the volume geometry comes from. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraVolumeSource VolumeSource { EComposableCameraVolumeSource::FromActor };

	/** Actor with a `UShapeComponent` (UBoxComponent / USphereComponent) whose
	 *  transform and scaled extents define the volume. The first shape
	 *  component found via `GetComponents<UShapeComponent>` wins. Multiple
	 *  shape components on one actor are not supported; use Inline mode or
	 *  a dedicated actor with a single shape when precise control is needed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "VolumeSource == EComposableCameraVolumeSource::FromActor", EditConditionHides))
	TObjectPtr<AActor> VolumeActor { nullptr };

	/** Inline-mode shape selector. Ignored in FromActor mode (the shape comes
	 *  from the component's class). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "VolumeSource == EComposableCameraVolumeSource::Inline", EditConditionHides))
	EComposableCameraVolumeShape Shape { EComposableCameraVolumeShape::Box };

	/** Inline-mode volume center in world space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "VolumeSource == EComposableCameraVolumeSource::Inline", EditConditionHides))
	FVector VolumeCenter { FVector::ZeroVector };

	/** Inline-mode volume rotation (world space). Only affects the Box shape - an OBB is the difference from an AABB. Ignored for Sphere. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "VolumeSource == EComposableCameraVolumeSource::Inline && Shape == EComposableCameraVolumeShape::Box", EditConditionHides))
	FRotator VolumeRotation { FRotator::ZeroRotator };

	/** Inline-mode box half-extents in the volume's local space (pre-rotation). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "VolumeSource == EComposableCameraVolumeSource::Inline && Shape == EComposableCameraVolumeShape::Box", EditConditionHides))
	FVector BoxExtents { FVector(500.f, 500.f, 300.f) };

	/** Inline-mode sphere radius in world units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "VolumeSource == EComposableCameraVolumeSource::Inline && Shape == EComposableCameraVolumeShape::Sphere", EditConditionHides, ClampMin = "0.0"))
	float SphereRadius { 500.f };

	// --- Smoothing --------------------------------------------------------

	/** Optional interpolator applied per axis to the output position. When
	 *  set, the node keeps a private `LastSmoothedPosition` and each tick
	 *  smooths it toward the (clamp or pass-through) target using THREE
	 *  independent 1D interpolator instances (one per world-space axis),
	 *  preserving the filter's dynamics per axis. When null, the node is
	 *  stateless and the output is a hard projection / pass-through.
	 *
	 *  Picks the same UInterpolatorBase instanced subobject pattern that
	 *  CollisionPush / PivotDamping use. Users choose between SpringDamper,
	 *  IIR, SimpleSpring, etc. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = InputParameters)
	TObjectPtr<UComposableCameraInterpolatorBase> ClampInterpolator;

private:
	/** Resolved volume packed for the debug draw + clamp logic. All fields
	 *  populated in-tick; not persisted across ticks (node is otherwise
	 *  stateless and scrubbable). */
	struct FResolvedVolume
	{
		EComposableCameraVolumeShape Shape = EComposableCameraVolumeShape::Box;
		FVector Center = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;  // only meaningful for Box
		FVector BoxExtents = FVector::ZeroVector;   // only meaningful for Box
		float SphereRadius = 0.f;                   // only meaningful for Sphere
	};

	/** Fill OutVolume from the current VolumeSource / VolumeActor / Inline
	 *  properties. Returns false when the source can't provide a usable
	 *  volume (e.g. FromActor with null actor, or no supported shape
	 *  component). Logs the specific reason. */
	bool ResolveVolume(FResolvedVolume& OutVolume) const;

	/** Return the nearest point inside the volume to WorldPos. `OutIsAlreadyInside`
	 *  is set to true when WorldPos was already inside (the returned point
	 *  equals WorldPos in that case). */
	static FVector NearestPointInVolume(const FResolvedVolume& Volume, const FVector& WorldPos, bool& OutIsAlreadyInside);

	/** Per-axis 1D interpolator instances built from ClampInterpolator in
	 *  OnInitialize. Three independent filter states let the X/Y/Z smoothing
	 *  dynamics stay decoupled. A spring overshoot on X shouldn't bleed
	 *  into Y / Z. */
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<double>>> ClampInterpolatorX_T;
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<double>>> ClampInterpolatorY_T;
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<double>>> ClampInterpolatorZ_T;

	/** Persistent smoothed output. Seeded on the first tick from the
	 *  upstream position so the first frame isn't a snap from origin. */
	FVector LastSmoothedPosition { FVector::ZeroVector };

	/** Cleared in OnInitialize; set to true the first time OnTickNode runs
	 *  so the seed happens against the live upstream pose. Re-activation
	 *  resets this via OnInitialize. */
	bool bHasSeededSmoothing { false };

#if !UE_BUILD_SHIPPING
	// --- Debug mirrors (populated by OnTickNode, consumed by DrawNodeDebug) ---

	mutable FResolvedVolume DebugResolvedVolume;
	mutable bool DebugHasResolvedVolume { false };
	mutable bool DebugIsClamping { false };
	mutable FVector DebugClampedPosition { FVector::ZeroVector };
	mutable FVector DebugUpstreamPosition { FVector::ZeroVector };
#endif
};
