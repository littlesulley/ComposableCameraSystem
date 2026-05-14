// Copyright Sulley. All rights reserved.

#include "Transitions/ComposableCameraSplineTransition.h"

#include "ComposableCameraSystemModule.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "Math/ComposableCameraMath.h"
#include "Utils/ComposableCameraBlueprintLibrary.h"

#if !UE_BUILD_SHIPPING
#include "Debug/ComposableCameraViewportDebug.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "SceneManagement.h"   // SDPG_Foreground

namespace
{
	static TAutoConsoleVariable<int32> CVarShowSplineTransitionGizmo(
		TEXT("CCS.Debug.Viewport.Transitions.Spline"),
		0,
		TEXT("Show SplineTransition gizmo:\n")
		TEXT("  - The standard source/target/progress triplet in sky-blue accent.\n")
		TEXT("  - The full spline curve sampled as a 32-segment polyline so you\n")
		TEXT("    can see the shape you authored with the current SplineType /\n")
		TEXT("    tangent / control-point settings.\n")
		TEXT("Requires `CCS.Debug.Viewport 1`. Gizmo disappears when the transition finishes."),
		ECVF_Default);
}
#endif

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
			// Accessed via index arithmetic - no copy, no insert, zero allocation.
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

	// Debug draw for the spline lives on the `CCS.Debug.Viewport.Transitions.Spline`
	// path now - see DrawTransitionDebug below, which samples the curve at
	// many t values using EvaluatePositionOnCurve (the same math used above).

	return ResultPose;
}

float UComposableCameraSplineTransition::GetBlendWeightAt(float NormalizedTime) const
{
	// Mirrors the exact switch OnEvaluate_Implementation uses to remap
	// DurationPct into BlendWeight. Debug sparkline reads the same shape
	// the camera actually moves at along the time axis.
	const float T = FMath::Clamp(NormalizedTime, 0.f, 1.f);
	switch (EvaluationCurveType)
	{
		case EComposableCameraSplineTransitionEvaluationCurveType::Linear:
			return T;
		case EComposableCameraSplineTransitionEvaluationCurveType::Cubic:
			return FMath::CubicInterp(0.f, 0.f, 1.f, 0.f, T);
		case EComposableCameraSplineTransitionEvaluationCurveType::Smooth:
			return ComposableCameraSystem::SmoothStep(T);
		case EComposableCameraSplineTransitionEvaluationCurveType::Smoother:
			return ComposableCameraSystem::SmootherStep(T);
		default:
			return T;
	}
}

#if !UE_BUILD_SHIPPING
FVector UComposableCameraSplineTransition::EvaluatePositionOnCurve(
	float t, const FVector& StartPos, const FVector& EndPos) const
{
	// NOTE: This is the SAME math as the OnEvaluate_Implementation switch
	// above. The only reason it lives in a separate function is that the
	// debug path wants to sample many t values per frame without going
	// through the full OnEvaluate pipeline (which mutates Percentage, walks
	// BlendBy on pose fields we don't care about here, etc.). If you change
	// one of the formulas, change both - or, better, route OnEvaluate
	// through this helper too.
	switch (SplineType)
	{
	case EComposableCameraSplineTransitionType::Hermite:
		{
			const FVector P0 = StartPos;
			const FVector P1 = EndPos;
			const FRotator R = UKismetMathLibrary::MakeRotFromX(P1 - P0);
			const FVector V0 = R.RotateVector(StartTangent);
			const FVector V1 = R.RotateVector(EndTangent);

			const FVector A =  V0 + V1 + 2.f * P0 - 2.f * P1;
			const FVector B = -2.f * V0 - V1 - 3.f * P0 + 3.f * P1;
			const FVector C =  V0;
			const FVector D =  P0;
			return (((A * t) + B) * t + C) * t + D;
		}
	case EComposableCameraSplineTransitionType::Bezier:
		{
			const FVector P0 = StartPos;
			const FVector P3 = EndPos;
			const FRotator R = UKismetMathLibrary::MakeRotFromX(P3 - P0);
			const FVector P1 = R.RotateVector(StartControlPoint) + P0;
			const FVector P2 = R.RotateVector(EndControlPoint) + P3;

			const float M1 = 1.f - t;
			const float M2 = M1 * M1;
			const float M3 = M2 * M1;
			const float N1 = t;
			const float N2 = N1 * N1;
			const float N3 = N2 * N1;
			return M3 * P0 + 3.f * M2 * N1 * P1 + 3.f * M1 * N2 * P2 + N3 * P3;
		}
	case EComposableCameraSplineTransitionType::CatmullRom:
		{
			const FVector EndVec = FVector{ (EndPos - StartPos).Length(), 0., 0. };
			const int32 NumVirtual = ControlPoints.Num() + 2;

			auto GetVirtual = [&](int32 Idx) -> FVector
			{
				if (Idx <= 0)                 { return FVector::ZeroVector; }
				if (Idx >= NumVirtual - 1)    { return EndVec; }
				return ControlPoints[Idx - 1];
			};

			const float SegmentFloat = t * (NumVirtual - 1);
			const int32 SegmentIndex = FMath::Clamp(FMath::FloorToInt(SegmentFloat), 0, NumVirtual - 2);

			const FRotator R = UKismetMathLibrary::MakeRotFromX(EndPos - StartPos);
			const FVector P0 = StartPos + R.RotateVector(GetVirtual(FMath::Clamp(SegmentIndex - 1, 0, NumVirtual - 1)));
			const FVector P1 = StartPos + R.RotateVector(GetVirtual(SegmentIndex));
			const FVector P2 = StartPos + R.RotateVector(GetVirtual(SegmentIndex + 1));
			const FVector P3 = StartPos + R.RotateVector(GetVirtual(FMath::Clamp(SegmentIndex + 2, 0, NumVirtual - 1)));

			const float Tau  = SegmentFloat - SegmentIndex;
			const float Tau2 = Tau * Tau;
			const float Tau3 = Tau2 * Tau;

			return 0.5f * ((2.f * P1) +
						   (-P0 + P2) * Tau +
						   (2.f * P0 - 5.f * P1 + 4.f * P2 - P3) * Tau2 +
						   (-P0 + 3.f * P1 - 3.f * P2 + P3) * Tau3);
		}
	case EComposableCameraSplineTransitionType::Arc:
		{
			const FVector P0 = StartPos;
			const FVector P1 = EndPos;
			const float   D  = FVector::Dist(P0, P1);
			const float   CosHalf = UKismetMathLibrary::DegCos(ArcAngle / 2.f);
			const float   SinHalf = UKismetMathLibrary::DegSin(ArcAngle / 2.f);
			// Protect against the near-colinear case - OnBeginPlay already
			// nudges ArcAngle away from exactly 180 deg to avoid this, but the
			// debug path could be called between OnBeginPlay runs.
			if (FMath::IsNearlyZero(SinHalf))
			{
				return FMath::Lerp(P0, P1, t);
			}
			const FVector Cpt = FVector{ D / 2.f, -D / 2.f * CosHalf / SinHalf, 0 };
			const FVector V0  = FVector::ZeroVector - Cpt;
			const FVector V1  = FVector{ D, 0., 0. } - Cpt;
			FVector       L   = ComposableCameraSystem::Slerp(V0, V1, t);
			L += Cpt;
			const FRotator R = (UKismetMathLibrary::MakeRotFromX(P1 - P0).Quaternion() * FRotator{ 0., 0., ArcRoll }.Quaternion()).Rotator();
			return P0 + R.RotateVector(L);
		}
	}
	// Fallback - shouldn't hit unless a new SplineType was added without
	// updating this switch. Draw a straight line so debug still shows something.
	return FMath::Lerp(StartPos, EndPos, t);
}

void UComposableCameraSplineTransition::DrawTransitionDebug(UWorld* World, bool bViewerIsOutsideCamera) const
{
	if (!World) { return; }
	if (CVarShowSplineTransitionGizmo.GetValueOnGameThread() == 0
		&& !FComposableCameraViewportDebug::ShouldShowAllTransitionGizmos()) { return; }

	// Sky-blue accent for this transition type. Same blue family as the
	// target-pose marker (which is blue) but noticeably lighter so the
	// progress sphere is still distinguishable against the target sphere.
	static const FColor AccentColor { 140, 200, 255 };

	// Standard source/target/progress draw first.
	DrawStandardTransitionDebug(World, bViewerIsOutsideCamera, AccentColor);

	// Then the full authored curve - this is why SplineTransition warrants
	// its own gizmo. Sampled 32 times; cheap (pure math, no allocation).
	constexpr int32 NumSamples = 32;
	const FVector StartPos = LastDebugSource.Position;
	const FVector EndPos   = LastDebugTarget.Position;

	FVector PrevPoint = EvaluatePositionOnCurve(0.f, StartPos, EndPos);
	for (int32 i = 1; i <= NumSamples; ++i)
	{
		const float t = static_cast<float>(i) / static_cast<float>(NumSamples);
		const FVector NextPoint = EvaluatePositionOnCurve(t, StartPos, EndPos);
		DrawDebugLine(World, PrevPoint, NextPoint, AccentColor,
			/*bPersistent=*/false, /*LifeTime=*/-1.f,
			/*DepthPriority=*/SDPG_Foreground, /*Thickness=*/1.f);
		PrevPoint = NextPoint;
	}
}
#endif // !UE_BUILD_SHIPPING

