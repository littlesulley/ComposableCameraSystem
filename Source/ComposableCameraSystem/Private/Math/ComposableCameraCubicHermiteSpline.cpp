// Copyright 2026 Sulley. All Rights Reserved.

#include "Math/ComposableCameraCubicHermiteSpline.h"

FVector UComposableCameraCubicHermiteSpline::GetPointOnSplineClosestToWorldSpacePosition(const FVector& WorldPosition)
{
	return FVector::ZeroVector;
}

FVector UComposableCameraCubicHermiteSpline::GetWorldSpacePositionByDistanceOnSpline(float DistanceOnSpline)
{
	return FVector::ZeroVector;
}

FRotator UComposableCameraCubicHermiteSpline::GetWorldSpaceRotationByDistanceOnSpline(float DistanceOnSpline)
{
	return FRotator::ZeroRotator;
}

float UComposableCameraCubicHermiteSpline::GetSplineLength()
{
	return 0.f;
}