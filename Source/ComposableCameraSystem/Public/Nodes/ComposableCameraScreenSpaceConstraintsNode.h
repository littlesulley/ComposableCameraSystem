// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "Nodes/ComposableCameraScreenSpacePivotNode.h"
#include "ComposableCameraScreenSpaceConstraintsNode.generated.h"

/**
 * Node for constraining a pivot position in screen using either translation or rotation.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraScreenSpaceConstraintsNode : public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	virtual void OnBeginPlayNode_Implementation(const FComposableCameraPose& CurrentCameraPose) override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void BeginDestroy() override;

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
	
	UPROPERTY(EditAnywhere, Category = ContextParameters)
	FActorComposableCameraContextParameter ContextPivotActor;
	
private:
	FDelegateHandle DrawDebugHandle;

private:
	FVector EnsureWithinBoundsTranslation(const FVector& Pivot, const FComposableCameraPose& CurrentPose, const float& AspectRatio, const float& TanHalfHOR);
	FRotator EnsureWithinBoundsRotation(const FVector& Pivot, const FComposableCameraPose& CurrentPose, float AspectRatio, float DegTanHalfHor);
	std::pair<float, float> GetTanHalfHORAndAspectRatio(const FComposableCameraPose& OutCameraPose);

	FVector GetCurrentPivot();
	void DrawDebugInfo(AHUD* HUD, UCanvas* Canvas);
};
