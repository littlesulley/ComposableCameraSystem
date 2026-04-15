// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "ComposableCameraLensNode.generated.h"

/**
 * Authors physical-lens parameters on the camera pose (focal length, aperture,
 * focus distance, diaphragm blade count) and gates whether the pose's physical
 * camera settings are applied to post-process.
 *
 * Conceptually mirrors Epic's GameplayCameras ULensParametersCameraNode, but
 * split between two concerns:
 *   1. Writing focal length + aperture + focus distance + blade count onto the
 *      pose, so they are available both to the post-process blend and to any
 *      downstream node that reads the pose.
 *   2. Toggling PhysicalCameraBlendWeight so that ApplyPhysicalCameraSettings()
 *      actually writes DoF / exposure into FPostProcessSettings.
 *
 * FOV mode coupling: writing FocalLength is orthogonal to SetFieldOfViewDegrees.
 * To put the pose in "focal length drives FOV" mode, this node clears the pose's
 * FieldOfView sentinel (FieldOfView = -1) iff bOverrideFieldOfViewFromFocalLength
 * is true — otherwise whatever the upstream FieldOfViewNode authored remains
 * authoritative for FOV resolution, and FocalLength is only used as a post-process
 * DoF input.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (ToolTip = "Authors physical lens parameters (focal length, aperture, focus distance, blade count) on the camera pose."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraLensNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

public:
	/** Focal length in millimetres. Drives both physical DoF and (optionally) FOV. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters, meta = (ClampMin = "1.0", ClampMax = "1000.0", Units = "mm"))
	float FocalLength { 35.f };

	/** Lens aperture (f-stops). Smaller number = wider aperture = more DoF blur. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters, meta = (ClampMin = "0.7", ClampMax = "64.0"))
	float Aperture { 2.8f };

	/** Distance to the focus subject in world units. <= 0 leaves the pose's FocusDistance sentinel in place (no DoF override). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters, meta = (ClampMin = "-1.0"))
	float FocusDistance { -1.f };

	/** Number of blades in the lens diaphragm. Affects bokeh polygon shape. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters, meta = (ClampMin = "3", ClampMax = "16"))
	int32 DiaphragmBladeCount { 8 };

	/**
	 * Physical camera contribution weight. 0 disables ApplyPhysicalCameraSettings
	 * entirely; 1 applies it at full strength. Default 1.0 so that merely placing
	 * this node in the chain is enough to get DoF / auto-exposure driven off the
	 * authored lens values — callers who want a per-frame fade should wire this
	 * pin explicitly.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PhysicalCameraBlendWeight { 1.f };

	/**
	 * When true, this node also puts the pose in focal-length-drives-FOV mode by
	 * clearing FieldOfView to -1. Use this when the LensNode is the authoritative
	 * FOV source (no separate FieldOfViewNode upstream). When false, FOV is left
	 * alone — the pose keeps whatever FOV mode an upstream node wrote.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	bool bOverrideFieldOfViewFromFocalLength { true };
};
