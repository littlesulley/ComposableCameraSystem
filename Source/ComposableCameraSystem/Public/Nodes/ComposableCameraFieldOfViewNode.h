// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "ComposableCameraFieldOfViewNode.generated.h"

/**
 * Node for adjusting field of view. This FOV is directly set to the CameraPose each frame.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (ToolTip = "Sets or adjusts the camera field of view."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraFieldOfViewNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

protected:
	
public:
	// The default FOV.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	float FieldOfView { 79.f };

	// Whether to enable dynamic FOV based on the scale of some actor(s).
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	bool bDynamicFov { false };

	// Min FOV if bDynamicFoV.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters, meta = (ClampMin = 1.f, ClampMax = 180.f, Units = "deg", EditCondition = "bDynamicFov", EditConditionHides))
	float MinFoV { 30.f };

	// Max FOV if bDynamicFoV.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters, meta = (ClampMin = 1.f, ClampMax = 180.f, Units = "deg", EditCondition = "bDynamicFov", EditConditionHides))
	float MaxFoV { 120.f };

	// FOV damping if bDynamicFoV.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters, meta = (EditCondition = "bDynamicFov", EditConditionHides))
	float FoVDamping { 0.5f };

	// This sets the object screen size where the camera will stop dynamically zooming.
	// A value of 100% will fill have the subject fill the frame.
	// Smaller percentages will provide some overscan and framing room around the subject.
	// Larger numbers will have the camera push in.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters, meta = (ClampMin = 0.f, ClampMax = 200.f, Units = "%", EditCondition = "bDynamicFov", EditConditionHides))
	float DesiredTargetViewportSize { 40.f };

	// Group of actors to track if bDynamicFoV. Not pin-exposed (TArray is not a
	// pin-mappable type); configured only as a UPROPERTY or through a context
	// parameter that targets this field by reflection.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters, meta = (EditCondition = "bDynamicFov", EditConditionHides))
	TArray<TObjectPtr<AActor>> ActorsForDynamicFoV;
};
