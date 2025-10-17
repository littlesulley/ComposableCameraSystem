// Copyright Sulley. All rights reserved.

#include "Math/ComposableCameraBezierSpline.h"

FVector UComposableCameraBezierSpline::GetPointOnSplineClosestToWorldSpacePosition(const FVector& WorldPosition)
{
	return FVector::ZeroVector;
}

FVector UComposableCameraBezierSpline::GetWorldSpacePositionByDistanceOnSpline(float DistanceOnSpline)
{
	return FVector::ZeroVector;
}

FRotator UComposableCameraBezierSpline::GetWorldSpaceRotationByDistanceOnSpline(float DistanceOnSpline)
{
	return FRotator::ZeroRotator;
}

float UComposableCameraBezierSpline::GetSplineLength()
{
	return 0.f;
}
