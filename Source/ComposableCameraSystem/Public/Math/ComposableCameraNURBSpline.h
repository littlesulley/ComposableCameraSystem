// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraSplineInterface.h"
#include "UObject/Object.h"
#include "ComposableCameraNURBSpline.generated.h"

/**
 * Non-uniform rational B-Splines.
 */
UCLASS(ClassGroup = ComposableCameraSystem, CollapseCategories)
class COMPOSABLECAMERASYSTEM_API UComposableCameraNURBSpline
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
