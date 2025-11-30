// Copyright Sulley. All rights reserved.

#include "Transitions/ComposableCameraSplineTransition.h"
#include "Math/ComposableCameraMath.h"

FComposableCameraPose UComposableCameraSplineTransition::OnEvaluate_Implementation(float DeltaTime, const FComposableCameraPose& CurrentTargetPose)
{
	float DurationPct = (GetTransitionTime() - GetRemainingTime()) / GetTransitionTime();
	float BlendWeight = DurationPct;
	
	switch (EvaluationCurveType)
	{
	case EComposableCameraSplineTransitionEvaluationCurveType::Linear:
		BlendWeight = DurationPct;
		break;
	case EComposableCameraSplineTransitionEvaluationCurveType::Cubic:
		BlendWeight = FMath::CubicInterp(0.f, 0.f, 1.f, 0.f, DurationPct);
		break;
	case EComposableCameraSplineTransitionEvaluationCurveType::Smooth:
		BlendWeight = ComposableCameraSystem::SmootherStep(DurationPct);
		break;
	case EComposableCameraSplineTransitionEvaluationCurveType::Smoother:
		BlendWeight = ComposableCameraSystem::SmootherStep(DurationPct);
		break;
	default:
		break;
	}
	
	FComposableCameraPose ResultPose = StartCameraPose;
	ResultPose.BlendBy(CurrentTargetPose, BlendWeight);
	
	switch (SplineType)
	{
	case EComposableCameraSplineTransitionType::Hermite:
		{
			FVector P1 = CurrentTargetPose.Position;
			FVector P0 = StartCameraPose.Position;
			FRotator R = UKismetMathLibrary::MakeRotFromX(P1 - P0);
			FVector V0 = R.RotateVector(StartTangent);
			FVector V1 = R.RotateVector(EndTangent);
			
			// f(t) = A * t^3 + B * t^2 + C * t + D
			FVector A = V0 + V1 + 2. * P0 - 2. * P1;
			FVector B = -2. * V0 - V1 - 3. * P0 + 3. * P1;
			FVector C = V0;
			FVector D = P0;
			
			// Evaluate f(BlendWeight)
			FVector PositionOnSpline = (((A * BlendWeight) + B) * BlendWeight + C) * BlendWeight + D;
			
			// Set to result pose.
			ResultPose.Position = PositionOnSpline;
		}
		break;
	case EComposableCameraSplineTransitionType::Bezier:
		break;
	case EComposableCameraSplineTransitionType::BasicSpline:
		break;
	default:
		break;
	}
	
	return ResultPose;
}
