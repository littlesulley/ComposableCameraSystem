// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraSplineInterface.h"
#include "UObject/Object.h"
#include "ComposableCameraBuiltInSpline.generated.h"

/**
 * Unreal's built-in splines. 
 */
UCLASS(ClassGroup = ComposableCameraSystem, CollapseCategories)
class COMPOSABLECAMERASYSTEM_API UComposableCameraBuiltInSpline
	: public UObject, public IComposableCameraSplineInterface
{
	GENERATED_BODY()
	
public:
	// A simple wrapper for USplineComponent, can be set directly from outside.
	TObjectPtr<USplineComponent> SplineComponent;

	// ~ IComposableCameraSplineInterface
	virtual FVector GetPointOnSplineClosestToWorldSpacePosition(const FVector& WorldPosition) override;
	virtual FVector GetWorldSpacePositionByDistanceOnSpline(float DistanceOnSpline) override;
	virtual FRotator GetWorldSpaceRotationByDistanceOnSpline(float DistanceOnSpline) override;
	virtual float GetSplineLength() override;
	// ~ IComposableCameraSplineInterface
};
