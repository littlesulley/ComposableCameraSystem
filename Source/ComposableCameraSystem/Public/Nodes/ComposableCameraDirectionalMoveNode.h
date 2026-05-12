// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "ComposableCameraDirectionalMoveNode.generated.h"

/**
 * Moves the camera from InitialTransform along a camera-space direction.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (ToolTip = "Moves the camera from an initial transform along a camera-space direction at a fixed speed. Negative Duration moves forever."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraDirectionalMoveNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	UComposableCameraDirectionalMoveNode() { PaletteCategory = TEXT("Position"); }

public:
	virtual void OnInitialize_Implementation() override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

	virtual EComposableCameraNodePatchCompatibility GetPatchCompatibility_Implementation() const override
	{
		return EComposableCameraNodePatchCompatibility::Incompatible;
	}

public:
	// Direction in camera-local space. X=forward, Y=right, Z=up.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	FVector Direction { FVector::ForwardVector };

	// Starting transform for the continuous move.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	FTransform InitialTransform { FTransform::Identity };

	// Movement speed in centimeters per second.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	float Speed { 100.f };

	// Seconds spent moving. Negative values move forever; zero holds InitialTransform.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	float Duration { -1.f };

private:
	float ElapsedTime { 0.f };
};
