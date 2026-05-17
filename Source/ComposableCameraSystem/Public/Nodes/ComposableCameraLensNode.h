// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "ComposableCameraLensNode.generated.h"

/**
 * Authors physical-lens parameters on the camera pose (focal length, aperture,
 * focus distance, diaphragm blade count) and gates the pose's DoF physical
 * settings through PhysicalCameraBlendWeight.
 *
 * Exposure is intentionally separate: use ExposureNode to author ISO,
 * ShutterSpeed, and ExposureBlendWeight.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (ToolTip = "Authors physical lens parameters (focal length, aperture, focus distance, blade count) on the camera pose."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraLensNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	UComposableCameraLensNode() { PaletteCategory = TEXT("Optics"); }

public:
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

public:
	/** Focal length in millimetres. Drives both physical DoF and (optionally) FOV. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters, meta = (ClampMin = "1.0", ClampMax = "1000.0", Units = "mm"))
	float FocalLength { 35.f };

	/** Lens aperture (f-stops). Smaller number = wider aperture = more DoF blur. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters, meta = (ClampMin = "0.1", ClampMax = "64.0"))
	float Aperture { 2.8f };

	/** Distance to the focus subject in world units. <= 0 leaves the pose's FocusDistance sentinel in place. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters, meta = (ClampMin = "-1.0"))
	float FocusDistance { -1.f };

	/** Number of blades in the lens diaphragm. Affects bokeh polygon shape. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters, meta = (ClampMin = "3", ClampMax = "16"))
	int32 DiaphragmBladeCount { 8 };

	/**
	 * Physical camera DoF contribution weight.
	 * 0 disables lens-driven DoF; 1 applies it at full strength.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PhysicalCameraBlendWeight { 1.f };

	/**
	 * When true, this node puts the pose in focal-length-drives-FOV mode by
	 * clearing FieldOfView to -1. When false, FOV is left as authored upstream.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	bool bOverrideFieldOfViewFromFocalLength { true };
};
