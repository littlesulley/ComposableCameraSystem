// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraTypes.h"
#include "ComposableCameraCameraNodeBase.h"
#include "ComposableCameraOrthographicNode.generated.h"

/**
 * Switches the camera pose into orthographic projection and authors the
 * associated ortho parameters (view width and clip planes). Mirrors Epic's
 * UOrthographicCameraNode, but uses the CCS pose-authoritative policy:
 * values written here override whatever CineCameraComponent defaults would
 * otherwise apply, and transitions snap the projection-mode boolean at 50%
 * blend weight per the BlendBy() contract in FComposableCameraPose.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (ToolTip = "Switches the camera pose into orthographic projection and authors view width + clip planes."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraOrthographicNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

public:
	/**
	 * Projection mode to write onto the pose. Defaults to Orthographic since the
	 * point of this node is to author an ortho setup — authors who want a
	 * perspective camera should simply not place this node. Exposed anyway so
	 * the mode can be toggled at runtime via a wire (e.g. a blueprint action
	 * flipping between ortho and perspective views).
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	TEnumAsByte<ECameraProjectionMode::Type> ProjectionMode { ECameraProjectionMode::Orthographic };

	/** Orthographic view width in world units. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters, meta = (ClampMin = "1.0"))
	float OrthographicWidth { 512.f };

	/** Ortho near clip plane in world units. 0 is valid (= near clip at camera origin). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters, meta = (ClampMin = "0.0"))
	float OrthoNearClipPlane { 0.f };

	/** Ortho far clip plane in world units. Must be > OrthoNearClipPlane. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters, meta = (ClampMin = "1.0"))
	float OrthoFarClipPlane { 10000.f };
};
