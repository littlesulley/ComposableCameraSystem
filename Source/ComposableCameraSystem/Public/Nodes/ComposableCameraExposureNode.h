// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "ComposableCameraExposureNode.generated.h"

/**
 * Authors physical exposure inputs on the camera pose.
 *
 * LensNode owns DoF-related physical settings. This node owns ISO,
 * ShutterSpeed, and ExposureBlendWeight so exposure does not brighten just
 * because a lens/DoF node exists in the camera chain.
 *
 * REQUIRES level-side setup to take visual effect. CCS writes `CameraISO` and
 * `CameraShutterSpeed` into post-process settings, but UE's renderer only
 * consumes them inside `CalculateManualAutoExposure`, which runs only when
 * BOTH of the following are true on the resolved post-process stack:
 *   - `AutoExposureMethod = AEM_Manual` (Metering Mode = Manual)
 *   - `AutoExposureApplyPhysicalCameraExposure = true`
 * `AutoExposureApplyPhysicalCameraExposure` is NOT exposed in Project Settings.
 * Set both flags on one of:
 *   - an unbounded PostProcessVolume in the level (Lens -> Exposure section)
 *   - a PostProcessComponent on any actor in the level
 *   - the camera component's own PostProcessSettings override
 * The Method (Manual) can additionally be defaulted project-wide via
 * Project Settings -> Rendering -> Default Settings -> Auto Exposure
 * (`r.DefaultFeature.AutoExposure.Method`), but the Apply-Physical flag
 * always needs a PP source. Matching Epic's GameplayCameras, this node never
 * toggles `AutoExposureApplyPhysicalCameraExposure` itself.
 * See `Docs/TechDoc.md` "Physical Exposure Ownership Gotcha" for the full
 * diagnostic recipe.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (ToolTip = "Authors physical exposure parameters (ISO, shutter speed, exposure blend weight) on the camera pose. REQUIRES BOTH AutoExposureMethod = Manual AND AutoExposureApplyPhysicalCameraExposure = true on the resolved post-process stack (PostProcessVolume / PostProcessComponent / camera component PP override). Otherwise ISO/Shutter are written but ignored by the renderer."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraExposureNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	UComposableCameraExposureNode() { PaletteCategory = TEXT("Optics"); }

public:
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

public:
	/** Sensor sensitivity. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters, meta = (ClampMin = "1.0"))
	float ISO { 100.f };

	/** Shutter speed in 1/seconds, e.g. 60 means 1/60s. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters, meta = (ClampMin = "1.0"))
	float ShutterSpeed { 60.f };

	/** Exposure contribution weight. 0 leaves exposure untouched; 1 applies ISO/Shutter fully. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ExposureBlendWeight { 1.f };
};
