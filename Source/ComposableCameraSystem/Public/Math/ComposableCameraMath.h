// Copyright Sulley. All rights reserved.

#pragma once

#include "Constraint.h"
#include "HeadMountedDisplayTypes.h"
#include "Kismet/KismetMathLibrary.h"

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

	/** Apply Slerp to two normalized vectors. */
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

	/** Apply Slerp to two vectors, no normalization is needed. */
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

	/** Get the unsigned angle between two vectors. */
	inline float UnsignedAngleBetweenVectors(FVector V1, FVector V2)
	{
		V1.Normalize();
		V2.Normalize();
		return UKismetMathLibrary::DegAtan2((V1 - V2).Length(), (V1 + V2).Length()) * 2.f;
	}
	
	/** Get the signed angle between two vectors. */
	inline float SignedAngleBetweenVectors(FVector V1, FVector V2, FVector Up)
	{
		float Angle = UnsignedAngleBetweenVectors(V1, V2);

		// Due to UE's coordinate system.
		if (FMath::Sign(FVector::DotProduct(Up, FVector::CrossProduct(V1, V2))) < 0)
		{
			Angle = -Angle;
		}

		return Angle;
	}

	/** Apply an additive rotation to camera rotation. First about world space yaw using AdditiveRotation.X, then about local space pitch using AdditiveRotation.Y. */
	inline FQuat ApplyAdditiveCameraRotation(FQuat CameraRotation, FVector2D AdditiveRotation)
	{
		if (AdditiveRotation.Length() < 1e-4)
		{
			return CameraRotation;
		}

		FQuat LocalRotation = FQuat { FVector::LeftVector, FMath::DegreesToRadians(AdditiveRotation.Y) };
		FQuat WorldRotation = FQuat { FVector::UpVector, FMath::DegreesToRadians(AdditiveRotation.X) };

		return ((WorldRotation * CameraRotation) * LocalRotation).GetNormalized();
	}

	/** Get world space yaw and local space pitch change from a camera rotation to a look-at direction. */
	inline FVector2D GetCameraRotationFromTarget(FQuat CameraRotation, FVector LookAtDirection)
	{
		if (LookAtDirection.Length() < 1e-4)
		{
			return FVector2D::ZeroVector;
		}

		FVector LocalDirection = UKismetMathLibrary::Quat_UnrotateVector(CameraRotation, LookAtDirection);

		// Align yaw.
		FVector ProjLookDirection = FVector::VectorPlaneProject(LocalDirection, FVector::UpVector);
		FVector ProjForwardDirection = FVector::VectorPlaneProject(FVector::ForwardVector, FVector::UpVector);
		float Yaw = SignedAngleBetweenVectors(ProjForwardDirection, ProjLookDirection, FVector::UpVector);
		
		// Align pitch.
		FQuat Q { FVector::UpVector, FMath::DegreesToRadians(Yaw) };
		float Pitch = SignedAngleBetweenVectors(Q * FVector::ForwardVector, LocalDirection, Q * FVector::LeftVector);

		return { Yaw, Pitch };
	}

	/** Get camera rotation from V1 to V2 with up vector Up. */
	inline FQuat GetCameraRotationFromVectors(FVector V1, FVector V2, FVector Up = FVector::UpVector)
	{
		V1.Normalize();
		V2.Normalize();
		
		FVector P1 = FVector::VectorPlaneProject(V1, Up);
		FVector P2 = FVector::VectorPlaneProject(V2, Up);

		if (P1.Length() < 1e-4 || P2.Length() < 1e-4)
		{
			FVector Axis = FVector::CrossProduct(V1, V2);
			return FQuat { Axis, FMath::DegreesToRadians(SignedAngleBetweenVectors(V1, V2, Up)) };
		}

		float DeltaPitch = UnsignedAngleBetweenVectors(V1, Up) - UnsignedAngleBetweenVectors(V2, Up);
		return FQuat { Up, FMath::DegreesToRadians(SignedAngleBetweenVectors(P1, P2, Up)) } * FQuat { FVector::CrossProduct(Up, V1), FMath::DegreesToRadians(DeltaPitch) }.GetNormalized();
	}
	
	/** Power iteration to find eigenvector. Ref https://en.wikipedia.org/wiki/Power_iteration 
	 *  Rayleigh quotient iteration converges faster, but involving computing matrix inverse. Ref https://en.wikipedia.org/wiki/Rayleigh_quotient_iteration
	 */
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

		return NormalizeVector4(EigenVector);
	}

	inline std::pair<FRotator, FVector4> MatrixInterpRotation(const TArray<FRotator>& Rotations, const TArray<float>& Weights, FVector4 InitialEigenVector = FVector4{ 0, 0, 0, 1})
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

		InitialEigenVector = NormalizeVector4(InitialEigenVector);
		FVector4 V = FindEigenVectorByPowerIteration(Accumulated, InitialEigenVector, 64);
		
		FQuat Q = FQuat(V.X, V.Y, V.Z, V.W);
		Q = Q.W < 0 ? -Q : Q;
		
		return { Q.Rotator(), V };
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

	template<typename... Args>
		requires (std::is_floating_point_v<Args> && ...)
	inline float GetClosestAngleDegree(float InAngle, Args... Angles)
	{
		constexpr static uint32 AngleCount = sizeof...(Angles);
		std::array<float, AngleCount> AnglesArray = { Angles... };

		const float* TargetAnglePtr = Algo::MinElement(AnglesArray, [InAngle](const float& A, const float& B)
		{
			float DeltaA = FMath::Abs(FMath::FindDeltaAngleDegrees(InAngle, A));
			float DeltaB = FMath::Abs(FMath::FindDeltaAngleDegrees(InAngle, B));
			return DeltaA < DeltaB;
		});
		
		return *TargetAnglePtr;
	}
	
}
