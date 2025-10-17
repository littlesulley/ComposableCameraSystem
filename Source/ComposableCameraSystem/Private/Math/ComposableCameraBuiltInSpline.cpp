// Copyright Sulley. All rights reserved.

#include "Math/ComposableCameraBuiltInSpline.h"

FVector UComposableCameraBuiltInSpline::GetPointOnSplineClosestToWorldSpacePosition(const FVector& WorldPosition)
{
	return FVector::ZeroVector;
}

FVector UComposableCameraBuiltInSpline::GetWorldSpacePositionByDistanceOnSpline(float DistanceOnSpline)
{
	return FVector::ZeroVector;
}

FRotator UComposableCameraBuiltInSpline::GetWorldSpaceRotationByDistanceOnSpline(float DistanceOnSpline)
{
	return FRotator::ZeroRotator;
}

float UComposableCameraBuiltInSpline::GetSplineLength()
{
	return 0.f;
}
