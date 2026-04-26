// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "ComposableCameraCameraNodeBase.h"
#include "ComposableCameraFilmbackNode.generated.h"

/**
 * Authors filmback (sensor) and aspect-ratio parameters on the camera pose:
 * sensor width/height, anamorphic squeeze, overscan, and aspect-ratio
 * constraint configuration. Mirrors Epic's UFilmbackCameraNode but integrates
 * with the CCS pose-authoritative policy rather than a dedicated filmback
 * struct.
 *
 * The sensor dimensions are consumed by FComposableCameraPose::GetEffectiveFieldOfView()
 * when the pose is in focal-length mode (FieldOfView <= 0), so changing the
 * filmback while holding focal length constant naturally changes the effective
 * FOV — exactly as on a real camera.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (ToolTip = "Authors sensor, overscan, and aspect-ratio parameters on the camera pose."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraFilmbackNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	UComposableCameraFilmbackNode() { PaletteCategory = TEXT("Optics"); }

public:
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

public:
	/** Sensor width in mm. Super35 default (24.89 mm). Used by FOV resolution when FocalLength drives FOV. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters, meta = (ClampMin = "1.0", Units = "mm"))
	float SensorWidth { 24.89f };

	/** Sensor height in mm. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters, meta = (ClampMin = "1.0", Units = "mm"))
	float SensorHeight { 18.67f };

	/** Anamorphic squeeze factor. 1.0 = spherical (no squeeze). 2.0 = classic 2x anamorphic. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters, meta = (ClampMin = "0.1"))
	float SqueezeFactor { 1.f };

	/** Sensor overscan percentage (0 = none). Used by the post-process / renderer to render a larger area than the final framing. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters, meta = (ClampMin = "0.0", ClampMax = "100.0", Units = "%"))
	float Overscan { 0.f };

	/** Whether to constrain the aspect ratio (letterbox / pillarbox) when the viewport ratio differs from the sensor ratio. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	bool bConstrainAspectRatio { false };

	/** Whether to override the project-wide default aspect-ratio axis constraint. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	bool bOverrideAspectRatioAxisConstraint { false };

	/** Axis constraint to use when bOverrideAspectRatioAxisConstraint is true. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters, meta = (EditCondition = "bOverrideAspectRatioAxisConstraint"))
	TEnumAsByte<EAspectRatioAxisConstraint> AspectRatioAxisConstraint { EAspectRatioAxisConstraint::AspectRatio_MaintainYFOV };
};
