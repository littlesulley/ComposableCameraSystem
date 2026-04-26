// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "ComposableCameraPivotRotateNode.generated.h"

class UComposableCameraInterpolatorBase;

/**
 * Synchronises the camera's rotation to a pivot actor's world rotation, with
 * an authored offset and an optional rotator interpolator for damping.
 *
 * Useful for vehicle / mount / vehicle-cockpit cameras where the camera should
 * adopt the rig's heading (and optionally pitch / roll) but with a fixed
 * relative offset (e.g. a slight downward tilt) and a smooth catch-up rather
 * than a hard lock.
 *
 * Compose semantics: target rotation is `PivotActor.Quat * RotationOffset.Quat`,
 * i.e. the offset is applied in the pivot's LOCAL space — equivalent to
 * authoring a child component attached to PivotActor with that relative
 * rotation. This avoids the gimbal artifacts a raw FRotator add produces when
 * the pivot has non-trivial pitch / roll.
 *
 * @InputParameter PivotActor      Source actor whose world rotation drives the
 *                                 target each frame.
 * @InputParameter RotationOffset  Local-space offset added on top of the pivot
 *                                 rotation.
 *
 * `Interpolator` is an Instanced subobject — its inner properties surface as
 * pins automatically via the base class's subobject-pin pipeline; no manual
 * pin declaration needed.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem,
	meta = (ToolTip = "Synchronises camera rotation to a pivot actor's rotation, with optional offset and interpolator damping."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraPivotRotateNode : public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	UComposableCameraPivotRotateNode() { PaletteCategory = TEXT("Rotation"); }

public:
	virtual void OnInitialize_Implementation() override;
	virtual void OnTickNode_Implementation(float DeltaTime,
		const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

public:
	// Actor whose world rotation drives the camera's target rotation each frame.
	// Typically wired at runtime via a context parameter (e.g. the player's
	// vehicle / mount), but kept as a UPROPERTY so the Details panel renders a
	// proper object picker and an authored default is available when unwired.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	TObjectPtr<AActor> PivotActor;

	// Rotation offset added on top of the pivot actor's rotation. Composed in
	// the pivot's LOCAL space (quaternion multiply) — same convention as a
	// child USceneComponent's RelativeRotation. Set to zero to copy the pivot
	// rotation exactly.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	FRotator RotationOffset { FRotator::ZeroRotator };

	// Optional rotator interpolator that damps the camera's approach to the
	// target rotation (PivotActor.Rotation composed with RotationOffset).
	// When null the camera snaps to the target each frame; when set, the
	// camera eases toward it on the interpolator's curve.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = InputParameters)
	TObjectPtr<UComposableCameraInterpolatorBase> Interpolator;

private:
	// Built once in OnInitialize from the authored Interpolator instance; null
	// when no interpolator is wired (snap mode). Reused every tick — no
	// per-frame allocation on the hot path.
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<FRotator>>> Interpolator_T;
};
