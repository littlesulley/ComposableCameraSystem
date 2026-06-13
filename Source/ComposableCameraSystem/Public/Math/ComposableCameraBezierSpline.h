// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraSplineInterface.h"
#include "UObject/Object.h"
#include "ComposableCameraBezierSpline.generated.h"

/**
 * Custom bezier curves.
 */
UCLASS(ClassGroup = ComposableCameraSystem, CollapseCategories)
class COMPOSABLECAMERASYSTEM_API UComposableCameraBezierSpline
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
