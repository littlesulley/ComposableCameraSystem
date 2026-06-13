// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "ComposableCameraSplineInterface.h"
#include "Kismet/KismetMathLibrary.h"
#include "ComposableCameraBasicSpline.generated.h"

UCLASS(ClassGroup = ComposableCameraSystem, CollapseCategories)
class COMPOSABLECAMERASYSTEM_API UComposableCameraSplineBase
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