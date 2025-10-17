// Copyright Sulley. All rights reserved.

#include "Math/ComposableCameraBasicSpline.h"

FVector UComposableCameraSplineBase::GetPointOnSplineClosestToWorldSpacePosition(const FVector& WorldPosition)
{
	return FVector::ZeroVector;
}

FVector UComposableCameraSplineBase::GetWorldSpacePositionByDistanceOnSpline(float DistanceOnSpline)
{
	return FVector::ZeroVector;
}

FRotator UComposableCameraSplineBase::GetWorldSpaceRotationByDistanceOnSpline(float DistanceOnSpline)
{
	return FRotator::ZeroRotator;
}

float UComposableCameraSplineBase::GetSplineLength()
{
	return 0.f;
}
