// Copyright Sulley. All rights reserved.

#include "Transitions/ComposableCameraSplineTransition.h"

#include "Core/ComposableCameraPlayerCamaraManager.h"
#include "Math/ComposableCameraMath.h"

void UComposableCameraSplineTransition::OnBeginPlay_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentTargetPose)
{
	if (ArcAngle <= 180.f && FMath::IsNearlyEqual(ArcAngle, 180.f, 1e-1))
	{
		ArcAngle = 179.f;
	}
	if (ArcAngle >= 180.f && FMath::IsNearlyEqual(ArcAngle, 180.f, 1e-1))
	{
		ArcAngle = 181.f;
	}
}

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
			FVector P0 = StartCameraPose.Position;
			FVector P1 = CurrentTargetPose.Position;
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
			
			// Set to result pose
			ResultPose.Position = PositionOnSpline;
		}
		break;
	case EComposableCameraSplineTransitionType::Bezier:
		{
			FVector P0 = StartCameraPose.Position;
			FVector P3 = CurrentTargetPose.Position;
			FRotator R = UKismetMathLibrary::MakeRotFromX(P3 - P0);
			FVector P1 = R.RotateVector(StartControlPoint) + P0;
			FVector P2 = R.RotateVector(EndControlPoint) + P3;

			// B(t) = (1 - t)^3 * P0 + 3 * (1 - t)^2 * t * P1 + 3 * (1 - t) * t^2 * P2 + t^3 * P3
			float M1 = 1.f - BlendWeight;
			float M2 = M1 * M1;
			float M3 = M2 * M1;
			float N1 = BlendWeight;
			float N2 = N1 * N1;
			float N3 = N2 * N1;
			
			// Evaluate f(BlendWeight)
			FVector PositionOnSpline = M3 * P0 + 3. * M2 * N1 * P1 + 3. * M1 * N2 * P2 + N3 * P3;

			// Set to result pose
			ResultPose.Position = PositionOnSpline;
		}
		break;
	case EComposableCameraSplineTransitionType::BasicSpline:
		break;
	case EComposableCameraSplineTransitionType::Arc:
		{
			FVector P0 = StartCameraPose.Position;
			FVector P1 = CurrentTargetPose.Position;
			float D = FVector::Dist(P0, P1);
			float CosHalfAngle = UKismetMathLibrary::DegCos(ArcAngle / 2.f);
			float SinHalfAngle = UKismetMathLibrary::DegSin(ArcAngle / 2.f);
			FVector C  = FVector { D / 2.f, -D / 2.f * CosHalfAngle / SinHalfAngle, 0 };
			FVector V0 = FVector::ZeroVector - C;
			FVector V1 = FVector { D, 0., 0. } - C;
			FVector L  = ComposableCameraSystem::Slerp(V0, V1,  BlendWeight);
			L += C;

			FRotator R = (UKismetMathLibrary::MakeRotFromX(P1 - P0).Quaternion() * FRotator{ 0., 0., ArcRoll }.Quaternion()).Rotator();
			FVector PositionOnSpline = P0 + R.RotateVector(L);

			// Set to result pose
			ResultPose.Position = PositionOnSpline;
		}
		break;
	default:
		break;
	}

	// Draw debug spline points.
	if (TargetCamera && TargetCamera->GetOwningPlayerCameraManager())
	{
		if (TargetCamera->GetOwningPlayerCameraManager()->bDrawDebugInformation)
		{
			DrawDebugSpline(StartCameraPose, CurrentTargetPose);
		}
	}
	
	return ResultPose;
}

void UComposableCameraSplineTransition::DrawDebugSpline(const FComposableCameraPose& StartPose, const FComposableCameraPose& TargetPose)
{
	constexpr static int NumSplinePoints = 128;
	TArray<FVector> SplinePoints{};
	SplinePoints.Reserve(NumSplinePoints);
			
	switch (SplineType)
	{
	case EComposableCameraSplineTransitionType::Hermite:
		{
			FVector P0 = StartPose.Position;
			FVector P1 = TargetPose.Position;
			FRotator R = (UKismetMathLibrary::MakeRotFromX(P1 - P0).Quaternion() * FRotator{ 0., 0., ArcRoll }.Quaternion()).Rotator();
			FVector V0 = R.RotateVector(StartTangent);
			FVector V1 = R.RotateVector(EndTangent);
			
			FVector A = V0 + V1 + 2. * P0 - 2. * P1;
			FVector B = -2. * V0 - V1 - 3. * P0 + 3. * P1;
			FVector C = V0;
			FVector D = P0;

			for (int i = 0; i < NumSplinePoints; ++i)
			{
				float TimeStamp = 1.f / NumSplinePoints * i;
				SplinePoints.Add((((A * TimeStamp) + B) * TimeStamp + C) * TimeStamp + D);
			}
			break;
		}
	case EComposableCameraSplineTransitionType::Bezier:
		{
			FVector P0 = StartCameraPose.Position;
			FVector P3 = TargetPose.Position;
			FRotator R = UKismetMathLibrary::MakeRotFromX(P3 - P0);
			FVector P1 = R.RotateVector(StartControlPoint) + P0;
			FVector P2 = R.RotateVector(EndControlPoint) + P3;

			for (int i = 0; i < NumSplinePoints; ++i)
			{
				float TimeStamp = 1.f / NumSplinePoints * i;
				float M1 = 1.f - TimeStamp;
				float M2 = M1 * M1;
				float M3 = M2 * M1;
				float N1 = TimeStamp;
				float N2 = N1 * N1;
				float N3 = N2 * N1;
				SplinePoints.Add(M3 * P0 + 3. * M2 * N1 * P1 + 3. * M1 * N2 * P2 + N3 * P3);
			}
			break;
		}
	case EComposableCameraSplineTransitionType::BasicSpline:
		break;
	case EComposableCameraSplineTransitionType::Arc:
		{
			FVector P0 = StartPose.Position;
			FVector P1 = TargetPose.Position;
			float D = FVector::Dist(P0, P1);
			float CosHalfAngle = UKismetMathLibrary::DegCos(ArcAngle / 2.f);
			float SinHalfAngle = UKismetMathLibrary::DegSin(ArcAngle / 2.f);
			FVector C  = FVector { D / 2.f, -D / 2.f * CosHalfAngle / SinHalfAngle, 0 };
			FVector V0 = FVector::ZeroVector - C;
			FVector V1 = FVector { D, 0., 0. } - C;
			FRotator R = UKismetMathLibrary::MakeRotFromX(P1 - P0);

			for (int i = 0; i < NumSplinePoints; ++i)
			{
				float TimeStamp = 1.f / NumSplinePoints * i;
				FVector L  = ComposableCameraSystem::Slerp(V0, V1,  TimeStamp) + C;
				SplinePoints.Add(P0 + R.RotateVector(L));
			}
			break;
		}
	default:
		break;
	}

	DrawDebugSplinePoints(SplinePoints);
}

void UComposableCameraSplineTransition::DrawDebugSplinePoints(const TArray<FVector>& SplinePoints)
{
	for (const FVector& Point : SplinePoints)
	{
		DrawDebugPoint(GetWorld(), Point, 8.f, FColor::Cyan, false, 0.f, 1.f);
	}
}
