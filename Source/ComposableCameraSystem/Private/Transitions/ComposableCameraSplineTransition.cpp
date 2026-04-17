// Copyright Sulley. All rights reserved.

#include "Transitions/ComposableCameraSplineTransition.h"

#include "ComposableCameraSystemModule.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "Math/ComposableCameraMath.h"
#include "Utils/ComposableCameraBlueprintLibrary.h"

void UComposableCameraSplineTransition::OnBeginPlay_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose)
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

FComposableCameraPose UComposableCameraSplineTransition::OnEvaluate_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose)
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
		BlendWeight = ComposableCameraSystem::SmoothStep(DurationPct);
		break;
	case EComposableCameraSplineTransitionEvaluationCurveType::Smoother:
		BlendWeight = ComposableCameraSystem::SmootherStep(DurationPct);
		break;
	default:
		break;
	}

	Percentage = BlendWeight;
	
	FComposableCameraPose ResultPose = CurrentSourcePose;
	ResultPose.BlendBy(CurrentTargetPose, BlendWeight);
	
	switch (SplineType)
	{
	case EComposableCameraSplineTransitionType::Hermite:
		{
			FVector P0 = CurrentSourcePose.Position;
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
			FVector P0 = CurrentSourcePose.Position;
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
	case EComposableCameraSplineTransitionType::CatmullRom:
		{
			// Virtual control-point array: {ZeroVector, ControlPoints[0..N-1], EndVec}
			// Accessed via index arithmetic — no copy, no insert, zero allocation.
			FVector StartPoint = CurrentSourcePose.Position;
			FVector EndPoint = CurrentTargetPose.Position;
			FVector EndVec = FVector{ (EndPoint - StartPoint).Length(), 0., 0. };
			int32 NumVirtual = ControlPoints.Num() + 2; // +2 for virtual start & end

			auto GetVirtualControlPoint = [&](int32 Idx) -> FVector
			{
				if (Idx <= 0) return FVector::ZeroVector;
				if (Idx >= NumVirtual - 1) return EndVec;
				return ControlPoints[Idx - 1];
			};

			// Map BlendWeight to segment
			float SegmentFloat = BlendWeight * (NumVirtual - 1);
			int32 SegmentIndex = FMath::Clamp(FMath::FloorToInt(SegmentFloat), 0, NumVirtual - 2);

			// Choose 4 points for this segment (with clamping at endpoints)
			FRotator R = UKismetMathLibrary::MakeRotFromX(EndPoint - StartPoint);
			FVector P0 = StartPoint + R.RotateVector(GetVirtualControlPoint(FMath::Clamp(SegmentIndex - 1, 0, NumVirtual - 1)));
			FVector P1 = StartPoint + R.RotateVector(GetVirtualControlPoint(SegmentIndex));
			FVector P2 = StartPoint + R.RotateVector(GetVirtualControlPoint(SegmentIndex + 1));
			FVector P3 = StartPoint + R.RotateVector(GetVirtualControlPoint(FMath::Clamp(SegmentIndex + 2, 0, NumVirtual - 1)));

			float T = SegmentFloat - SegmentIndex;
			float T2 = T * T;
			float T3 = T2 * T;

			FVector PositionOnSpline = 0.5f * ((2.f * P1) +
									  (-P0 + P2) * T +
									  (2.f * P0 - 5.f * P1 + 4.f * P2 - P3) * T2 +
									  (-P0 + 3.f * P1 - 3.f * P2 + P3) * T3);

			ResultPose.Position = PositionOnSpline;
		}
		break;
	case EComposableCameraSplineTransitionType::Arc:
		{
			FVector P0 = CurrentSourcePose.Position;
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

#if ENABLE_DRAW_DEBUG
	// Draw debug spline points.
	if (AComposableCameraPlayerCameraManager* PCM = UComposableCameraBlueprintLibrary::GetComposableCameraPlayerCameraManager(this, 0))
	{
		if (PCM->bDrawDebugInformation)
		{
			DrawDebugSpline(CurrentSourcePose, CurrentTargetPose);
		}
	}
#endif
	
	return ResultPose;
}

#if ENABLE_DRAW_DEBUG
void UComposableCameraSplineTransition::DrawDebugSpline(const FComposableCameraPose& StartPose, const FComposableCameraPose& TargetPose)
{
	// Draw debug points inline — no intermediate TArray allocation.
	constexpr int32 NumSplinePoints = 128;
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	switch (SplineType)
	{
	case EComposableCameraSplineTransitionType::Hermite:
		{
			FVector P0 = StartPose.Position;
			FVector P1 = TargetPose.Position;
			FRotator R = UKismetMathLibrary::MakeRotFromX(P1 - P0);
			FVector V0 = R.RotateVector(StartTangent);
			FVector V1 = R.RotateVector(EndTangent);

			FVector A = V0 + V1 + 2. * P0 - 2. * P1;
			FVector B = -2. * V0 - V1 - 3. * P0 + 3. * P1;
			FVector C = V0;
			FVector D = P0;

			for (int32 i = 0; i < NumSplinePoints; ++i)
			{
				float TimeStamp = static_cast<float>(i) / NumSplinePoints;
				FVector Point = (((A * TimeStamp) + B) * TimeStamp + C) * TimeStamp + D;
				DrawDebugPoint(World, Point, 8.f, FColor::Cyan, false, 0.f, 1.f);
			}
			break;
		}
	case EComposableCameraSplineTransitionType::Bezier:
		{
			FVector P0 = StartPose.Position;
			FVector P3 = TargetPose.Position;
			FRotator R = UKismetMathLibrary::MakeRotFromX(P3 - P0);
			FVector P1 = R.RotateVector(StartControlPoint) + P0;
			FVector P2 = R.RotateVector(EndControlPoint) + P3;

			for (int32 i = 0; i < NumSplinePoints; ++i)
			{
				float TimeStamp = static_cast<float>(i) / NumSplinePoints;
				float M1 = 1.f - TimeStamp;
				float M2 = M1 * M1;
				float M3 = M2 * M1;
				float N1 = TimeStamp;
				float N2 = N1 * N1;
				float N3 = N2 * N1;
				FVector Point = M3 * P0 + 3. * M2 * N1 * P1 + 3. * M1 * N2 * P2 + N3 * P3;
				DrawDebugPoint(World, Point, 8.f, FColor::Cyan, false, 0.f, 1.f);
			}
			break;
		}
	case EComposableCameraSplineTransitionType::CatmullRom:
		{
			// Same virtual-index pattern as OnEvaluate — zero allocation.
			FVector StartPoint = StartPose.Position;
			FVector EndPoint = TargetPose.Position;
			FVector EndVec = FVector{ (EndPoint - StartPoint).Length(), 0., 0. };
			int32 NumVirtual = ControlPoints.Num() + 2;

			auto GetVirtualControlPoint = [&](int32 Idx) -> FVector
			{
				if (Idx <= 0) return FVector::ZeroVector;
				if (Idx >= NumVirtual - 1) return EndVec;
				return ControlPoints[Idx - 1];
			};

			FRotator R = UKismetMathLibrary::MakeRotFromX(EndPoint - StartPoint);

			for (int32 i = 0; i < NumSplinePoints; ++i)
			{
				float TimeStamp = static_cast<float>(i) / NumSplinePoints;

				float SegmentFloat = TimeStamp * (NumVirtual - 1);
				int32 SegmentIndex = FMath::Clamp(FMath::FloorToInt(SegmentFloat), 0, NumVirtual - 2);

				FVector CP0 = StartPoint + R.RotateVector(GetVirtualControlPoint(FMath::Clamp(SegmentIndex - 1, 0, NumVirtual - 1)));
				FVector CP1 = StartPoint + R.RotateVector(GetVirtualControlPoint(SegmentIndex));
				FVector CP2 = StartPoint + R.RotateVector(GetVirtualControlPoint(SegmentIndex + 1));
				FVector CP3 = StartPoint + R.RotateVector(GetVirtualControlPoint(FMath::Clamp(SegmentIndex + 2, 0, NumVirtual - 1)));

				float T = SegmentFloat - SegmentIndex;
				float T2 = T * T;
				float T3 = T2 * T;

				FVector Point = 0.5f * ((2.f * CP1) +
									(-CP0 + CP2) * T +
									(2.f * CP0 - 5.f * CP1 + 4.f * CP2 - CP3) * T2 +
									(-CP0 + 3.f * CP1 - 3.f * CP2 + CP3) * T3);
				DrawDebugPoint(World, Point, 8.f, FColor::Cyan, false, 0.f, 1.f);
			}
			break;
		}
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
			FRotator R = (UKismetMathLibrary::MakeRotFromX(P1 - P0).Quaternion() * FRotator{ 0, 0., ArcRoll }.Quaternion()).Rotator();

			for (int32 i = 0; i < NumSplinePoints; ++i)
			{
				float TimeStamp = static_cast<float>(i) / NumSplinePoints;
				FVector L = ComposableCameraSystem::Slerp(V0, V1, TimeStamp) + C;
				FVector Point = P0 + R.RotateVector(L);
				DrawDebugPoint(World, Point, 8.f, FColor::Cyan, false, 0.f, 1.f);
			}
			break;
		}
	default:
		break;
	}
}
#endif
