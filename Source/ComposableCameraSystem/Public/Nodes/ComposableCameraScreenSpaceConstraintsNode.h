// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "Nodes/ComposableCameraScreenSpacePivotNode.h"
#include "ComposableCameraScreenSpaceConstraintsNode.generated.h"

/**
 * Node for constraining a pivot position in screen using either translation or rotation.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (ToolTip = "Constrains a pivot actor within a screen-space safe zone."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraScreenSpaceConstraintsNode : public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	virtual void OnInitialize_Implementation() override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void BeginDestroy() override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

protected:

public:
	// The method to keep screen space constraints, translation or rotation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraScreenSpaceMethod Method;
	
	// Screen space safe zone center (w, h). (0, 0) is the screen center, (0.5, 0.5) is the top-right corner. 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (ClampMax = "0.5", ClampMin = "-0.5"))
	FVector2D SafeZoneCenter { 0.0, 0.0 };

	// Screen space safe zone left half-width and right half-width, from -1 to 1.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (ClampMax = "1", ClampMin = "-1"))
	FVector2D SafeZoneWidth { -0.1, 0.1 };

	// Screen space safe zone bottom half-height and top half-height, from -1 to 1.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (ClampMax = "1", ClampMin = "-1"))
	FVector2D SafeZoneHeight { -0.1, 0.1 };
	
private:
	FDelegateHandle DrawDebugHandle;

private:
	FVector EnsureWithinBoundsTranslation(const FVector& Pivot, const FComposableCameraPose& CurrentPose, const float& AspectRatio, const float& TanHalfHOR);
	FRotator EnsureWithinBoundsRotation(const FVector& Pivot, const FComposableCameraPose& CurrentPose, float AspectRatio, float DegTanHalfHor);
	std::pair<float, float> GetTanHalfHORAndAspectRatio(const FComposableCameraPose& OutCameraPose);
	std::pair<float, float> CalibrateRotationOffsetNewton(float TanHalfHOR, float AspectRatio, FVector Direction, FRotator LookAtRotation, float ScreenX, float ScreenY);

	FVector GetCurrentPivot();
	void DrawDebugInfo(AHUD* HUD, UCanvas* Canvas);
};
