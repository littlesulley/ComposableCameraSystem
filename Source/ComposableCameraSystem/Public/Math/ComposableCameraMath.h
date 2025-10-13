// Copyright Sulley. All rights reserved.

#pragma once

#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"

namespace ComposableCameraSystem
{
	inline float SmoothStep(float T)
	{
		return T * T * (3.f - 2.f * T);
	}

	inline float SmootherStep(float T)
	{
		return T * T * T * (T * (T * 6.f - 15.f) + 10.f);
	}

	inline double SimpleExpDamp(float DeltaTime, float DampTime, float Input)
	{
		if (DeltaTime <= 0.f)
		{
			return 0.f;
		}

		if (DampTime <= 0.f)
		{
			return Input;
		}

		float lnResidual = FMath::Loge(0.01);
		return Input * (1.0f - FMath::Exp(lnResidual * DeltaTime / DampTime));
	}

	inline double NormalizeYaw(double InYaw)
	{
		return FMath::UnwindDegrees(InYaw);
	}

	inline FVector4 NormalizeVector4(const FVector4& V, float Tolerance = UE_KINDA_SMALL_NUMBER)
	{
		float SquaredSum = V.X * V.X + V.Y * V.Y + V.Z * V.Z + V.W * V.W;
		
		if (SquaredSum > Tolerance)
		{
			const float Scale = FMath::InvSqrt(SquaredSum);
			return FVector4(V.X * Scale, V.Y * Scale, V.Z * Scale, V.W * Scale);
		}
		
		return V;
	}

	inline FVector SlerpNormalized(const FVector& Start, const FVector& End, float Alpha)
	{
		float Dot = Start.Dot(End);
		float Theta = UKismetMathLibrary::DegAcos(Dot);

		if (FMath::Abs(Theta) < 0.001f)
		{
			return UKismetMathLibrary::VLerp(Start, End, Alpha);
		}
    
		float SinTheta = UKismetMathLibrary::DegSin(Theta);
		float StartRatio = UKismetMathLibrary::DegSin((1 - Alpha) * Theta) / SinTheta;
		float EndRatio = UKismetMathLibrary::DegSin(Alpha * Theta) / SinTheta;
    
		return StartRatio * Start + EndRatio * End;
	}

	inline FVector Slerp(const FVector& Start, const FVector& End, float Alpha)
	{
		float StartMag = Start.Length();
		float EndMag = End.Length();

		FVector StartDirection = Start.GetSafeNormal();
		FVector EndDirection = End.GetSafeNormal();
		FVector Direction = SlerpNormalized(StartDirection, EndDirection, Alpha);

		float Mag = FMath::Lerp(StartMag, EndMag, Alpha);

		return Direction * Mag;
	}
	
	// Power iteration to find eigenvector. Ref https://en.wikipedia.org/wiki/Power_iteration 
	// Rayleigh quotient iteration converges faster, but involving computing matrix inverse. Ref https://en.wikipedia.org/wiki/Rayleigh_quotient_iteration
	inline FVector4 FindEigenVectorByPowerIteration(const FMatrix& M, const FVector4& V, const int Steps, const float Epsilon = UE_SMALL_NUMBER)
	{
		FVector4 EigenVector = V;
		float EigenValue = M.TransformFVector4(EigenVector).X / EigenVector.X;

		for (int i = 0; i < Steps; ++i)
		{
			FVector4 Mul = M.TransformFVector4(EigenVector);
			FVector4 NewEigenVector = NormalizeVector4(Mul);
			
			float NewEigenValue = M.TransformFVector4(NewEigenVector).X / NewEigenVector.X;

			if (FMath::Abs(EigenValue - NewEigenValue) < Epsilon)
			{
				break;
			}

			EigenVector = NewEigenVector;
			EigenValue = NewEigenValue;
		}

		return EigenVector;
	}

	inline FRotator MatrixInterpRotation(const TArray<FRotator>& Rotations, const TArray<float>& Weights)
	{
		// Must initialize as all-zeros
		FMatrix Accumulated = FMatrix(
			FPlane(0, 0, 0, 0),
			FPlane(0, 0, 0, 0),
			FPlane(0, 0, 0, 0),
			FPlane(0, 0, 0, 0)
		);

		for (int i = 0; i < Rotations.Num(); ++i)
		{
			FQuat Q = Rotations[i].Quaternion();

			FMatrix M = FMatrix(
				FPlane(Q.X * Q.X, Q.X * Q.Y, Q.X * Q.Z, Q.X * Q.W),
				FPlane(Q.Y * Q.X, Q.Y * Q.Y, Q.Y * Q.Z, Q.Y * Q.W),
				FPlane(Q.Z * Q.X, Q.Z * Q.Y, Q.Z * Q.Z, Q.Z * Q.W),
				FPlane(Q.W * Q.X, Q.W * Q.Y, Q.W * Q.Z, Q.W * Q.W)
			);

			Accumulated += M * Weights[i];
		}
	
		FVector4 V = FindEigenVectorByPowerIteration(Accumulated, FVector4(0, 0, 0, 1), 64);
		FQuat Q = FQuat(V.X, V.Y, V.Z, V.W);
		Q = Q.W < 0 ? -Q : Q;
		
		return Q.Rotator();
	}

	inline FRotator CircularInterpRotation(const TArray<FRotator>& Rotations, const TArray<float>& Weights, float Epsilon)
	{
		float SumSinYaw = 0.f, SumCosYaw = 0.f;
		float SumSinPitch = 0.f, SumCosPitch = 0.f;
		float SumSinRoll = 0.f, SumCosRoll = 0.f;

		for (int i = 0; i < Rotations.Num(); ++i)
		{
			FRotator Q = Rotations[i];
		
			SumSinYaw   += Weights[i] * UKismetMathLibrary::DegSin(Q.Yaw);
			SumCosYaw   += Weights[i] * UKismetMathLibrary::DegCos(Q.Yaw);
			SumSinPitch += Weights[i] * UKismetMathLibrary::DegSin(Q.Pitch);
			SumCosPitch += Weights[i] * UKismetMathLibrary::DegCos(Q.Pitch);
			SumSinRoll  += Weights[i] * UKismetMathLibrary::DegSin(Q.Roll);
			SumCosRoll  += Weights[i] * UKismetMathLibrary::DegCos(Q.Roll);
		}

		return FRotator(
			FMath::RadiansToDegrees(UKismetMathLibrary::Atan2(SumSinPitch, SumCosPitch + Epsilon)),
			FMath::RadiansToDegrees(UKismetMathLibrary::Atan2(SumSinYaw, SumCosYaw + Epsilon)),
			FMath::RadiansToDegrees(UKismetMathLibrary::Atan2(SumSinRoll, SumCosRoll + Epsilon))
		);
	}

	inline FRotator QuaternionInterpRotation(const TArray<FRotator>& Rotations, const TArray<float>& Weights)
	{
		FQuat R {0, 0, 0, 0};

		for (int i = 0; i < Rotations.Num(); ++i)
		{
			FQuat Q = Rotations[i].Quaternion();

			if (UKismetMathLibrary::Vector4_DotProduct(FVector4(R.W, R.X, R.Y, R.Z), FVector4(Q.W, Q.X, Q.Y, Q.Z)) >= 0)
			{
				R += Weights[i] * Q;
			}
			else
			{
				R -= Weights[i] * Q;
			}
		}

		return R.GetNormalized().Rotator();
	}

	inline FRotator AngleInterpRotation(const TArray<FRotator>& Rotations, const TArray<float>& Weights)
	{
		FRotator AccumulatedRotation = FRotator::ZeroRotator;
		float TotalWeight = 0.0f;

		for (int i = 0; i < Rotations.Num(); ++i)
		{
			TotalWeight += Weights[i];

			float Alpha = Weights[i] / TotalWeight;
			FRotator CameraRotation = Rotations[i];

			// On the same side, just interp it!
			if ((AccumulatedRotation.Yaw >= 0 && CameraRotation.Yaw >= 0) || (AccumulatedRotation.Yaw <= 0 && CameraRotation.Yaw <= 0))
			{
				AccumulatedRotation = (1 - Alpha) * AccumulatedRotation + Alpha * CameraRotation;
			}

			// Otherwise, should check the angle difference.
			else
			{
				float AngleDifference = AccumulatedRotation.Yaw - CameraRotation.Yaw;

				if (FMath::Abs(AngleDifference) > 180)
				{
					AccumulatedRotation.Yaw >= 0 ? AccumulatedRotation.Yaw -= 360 : CameraRotation.Yaw -= 360;
				}

				AccumulatedRotation = (1 - Alpha) * AccumulatedRotation + Alpha * CameraRotation;
			}
		}

		return AccumulatedRotation;
	}
	
}
