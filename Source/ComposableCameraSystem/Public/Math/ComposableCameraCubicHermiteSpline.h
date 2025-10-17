// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraSplineInterface.h"
#include "UObject/Object.h"
#include "ComposableCameraCubicHermiteSpline.generated.h"

/**
 * Cubic Hermite spline.
 */
UCLASS(ClassGroup = ComposableCameraSystem, CollapseCategories)
class COMPOSABLECAMERASYSTEM_API UComposableCameraCubicHermiteSpline
	: public UObject, public IComposableCameraSplineInterface
{
	GENERATED_BODY()

public:

	// ~ IComposableCameraSplineInterface
	virtual FVector GetPointOnSplineClosestToWorldSpacePosition(const FVector& WorldPosition) override;
	virtual FVector GetWorldSpacePositionByDistanceOnSpline(float DistanceOnSpline) override;
	virtual FRotator GetWorldSpaceRotationByDistanceOnSpline(float DistanceOnSpline) override;
	virtual float GetSplineLength() override;
	// ~ IComposableCameraSplineInterface
};
