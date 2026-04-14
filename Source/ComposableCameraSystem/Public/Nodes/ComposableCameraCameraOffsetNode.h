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
	// Offset to apply on the pivot position in camera space.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	FVector CameraOffset;
};
