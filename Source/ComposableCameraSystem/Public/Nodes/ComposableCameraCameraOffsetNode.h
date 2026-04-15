// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
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
};
