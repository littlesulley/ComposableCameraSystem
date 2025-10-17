// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ComposableCameraSplineInterface.generated.h"

/**
 * Interface for different splines.
 */
UINTERFACE()
class COMPOSABLECAMERASYSTEM_API UComposableCameraSplineInterface : public UInterface
{
	GENERATED_BODY()
};

class IComposableCameraSplineInterface
{
	GENERATED_BODY()

public:
	virtual FVector GetPointOnSplineClosestToWorldSpacePosition(const FVector& WorldPosition) = 0;
	virtual FVector GetWorldSpacePositionByDistanceOnSpline(float DistanceOnSpline) = 0;
	virtual FRotator GetWorldSpaceRotationByDistanceOnSpline(float DistanceOnSpline) = 0;
	virtual float GetSplineLength() = 0;
};
