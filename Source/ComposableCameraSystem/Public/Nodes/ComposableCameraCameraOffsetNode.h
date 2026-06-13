// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "Curves/CurveFloat.h"
#include "ComposableCameraCameraOffsetNode.generated.h"

/**
 * Applies a positional offset to the camera in camera-local space.
 */
UCLASS(NotBlueprintable, ClassGroup =  ComposableCameraSystem, meta = (ToolTip = "Applies a positional offset to the camera in camera-local space."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraCameraOffsetNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	UComposableCameraCameraOffsetNode() { PaletteCategory = TEXT("Position"); }

public:
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

protected:

public:
	// Pivot position the camera offset is applied from. Almost always driven by an
	// upstream pivot-producing node via wire (or a context parameter); kept as a
	// UPROPERTY so the Details panel renders a native FVector widget and an
	// authored default is available when unwired.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	FVector PivotPosition { FVector::ZeroVector };

	// Offset to apply on the pivot position in camera space.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	FVector CameraOffset;

	// Additive forward offset sampled by current pitch. Curve X = pitch in
	// degrees, Y = additive CameraOffset.X in cm. Stored inline on the node so
	// authors can edit keys without creating a separate UCurveFloat asset.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	FRuntimeFloatCurve ForwardOffsetDeltaByPitchCurve;
};
