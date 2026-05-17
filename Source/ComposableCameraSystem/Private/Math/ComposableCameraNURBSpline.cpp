// Copyright 2026 Sulley. All Rights Reserved.

#include "Math/ComposableCameraNURBSpline.h"

FVector UComposableCameraNURBSpline::GetPointOnSplineClosestToWorldSpacePosition(const FVector& WorldPosition)
{
	return FVector::ZeroVector;
}

FVector UComposableCameraNURBSpline::GetWorldSpacePositionByDistanceOnSpline(float DistanceOnSpline)
{
	return FVector::ZeroVector;
}

FRotator UComposableCameraNURBSpline::GetWorldSpaceRotationByDistanceOnSpline(float DistanceOnSpline)
{
	return FRotator::ZeroRotator;
}

float UComposableCameraNURBSpline::GetSplineLength()
{
	return 0.f;
}