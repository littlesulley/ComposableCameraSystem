// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "Constraint.h"
#include "HeadMountedDisplayTypes.h"
#include "Kismet/KismetMathLibrary.h"
#include "VT/VirtualTextureScalability.h"

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
		
		if (FMath::IsNearlyEqual(FMath::Abs(Theta), 0.f, 1e-2))
		{
			return Start;
		}

		if (FMath::IsNearlyEqual(FMath::Abs(Theta), 180.f, 1e-2))
		{
			FVector UpAxis = UKismetMathLibrary::MakeRotFromX(Start).RotateVector(FVector::UpVector);
			return Start.RotateAngleAxis(Theta * Alpha, UpAxis);
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

	/**
	 * Closed-form solver for (Pitch X, Yaw Y) rotation (Roll = 0) such that
	 * the world-space ray `Direction` (from camera origin) projects onto the
	 * normalized screen coords `(ScreenX, ScreenY)` in [-0.5, 0.5]. Returns
	 * { Pitch, Yaw } in degrees, UE convention (positive pitch = up,
	 * positive yaw = right). Replaces the iterative Newton solver formerly
	 * duplicated inside ScreenSpacePivotNode and ScreenSpaceConstraintsNode.
	 *
	 * -- Derivation -------------------------------------------------------
	 *
	 * Camera basis under (Pitch X, Yaw Y, Roll 0), expressed in world frame:
	 *
	 *     F = ( cos X cos Y,  cos X sin Y,  sin X)        // forward (cam +X)
	 *     R = (-sin Y,        cos Y,        0    )        // right   (cam +Y)
	 *     U = (-sin X cos Y,. Sin X sin Y,  cos X)        // up      (cam +Z)
	 *
	 * Direction in camera space is Px = F*D, Py = R*D, Pz = U*D, with
	 * D = (A, B, C) = Direction.
	 *
	 * Screen mapping is Py / (2*m*Px) and Pz / (2*n*Px) where m = TanHalfHOR
	 * and n = TanHalfHOR / AspectRatio. Letting u = 2*ScreenX*m, v = 2*ScreenY*n,
	 * the constraint is
	 *
	 *     Py = u*Px,    Pz = v*Px                                   (?
	 *
	 * (? means(Px, Py, Pz) in(1, u, v). Geometrically: the pivot must lie on
	 * the ray from camera origin through the screen-plane point (1, u, v).
	 * The unit direction in camera space is therefore
	 *
	 *     d_cam = (1, u, v) / s,    s = in1 + u + v)
	 *
	 * The same physical ray in world frame is d_world = D / L, with L = -
	 * With R the camera-to-world rotation matrix (whose columns are F, R, U),
	 *
	 *     R * (1, u, v)- = K * (A, B, C)-        where K <=s / L
	 *
	 * Component-wise:
	 *
	 *     cos X cos Y - u sin Y - v sin X cos Y = K*A           (I)
	 *     cos X sin Y + u cos Y - v sin X sin Y = K*B           (II)
	 *     sin X            + v cos X            = K*C           (III)
	 *
	 * (III) contains only X. That is why the system decouples.
	 *
	 * Solve X.  By the harmonic identity
	 *     sin X + v cos X = in1+v) * sin(X + arctan v),
	 * (III) becomes
	 *     X = arcsin(K*C / in1+v)) - arctan v               --- (X)
	 * The other branch X =  - arcsin(...) - arctan v corresponds to a
	 * back-facing camera and is discarded.
	 *
	 * Solve Y.  With X known, let  = cos X - v sin X. (I)+(II) become a
	 * 2x2 linear system in (cos Y, sin Y):
	 *
	 *     [ . U] [cos Y]     [A]
	 *     [u   ] [sin Y] = K [B]
	 *
	 * Determinant  + u > 0 generically, Cramer gives a Y where K cancels:
	 *
	 *     Y = atan2(*B - u*A,  *A + u*B)                   --- (Y)
	 *
	 * Y is independent of --depends only on the direction of D.
	 *
	 * Consistency.  (X)+(III) automatically imply  + u = K(A + B),
	 * i.e. (cos Y, sin Y) lies on the unit circle. No extra check required
	 * in the regular regime.
	 *
	 * -- Edge cases -------------------------------------------------------
	 *
	 *   |T| > 1, T <=K*C / in1+v)
	 *       The pivot cannot be placed at (ScreenX, ScreenY) without
	 *       exceeding the FOV cone. Clamped to 1 so the pivot lands at the
	 *       closest reachable on-FOV pitch. EnsureWithinBoundsRotation
	 *       callers usually pre-clamp to a safe zone, so this hits only when
	 *       the pivot direction itself is outside the FOV.
	 *
	 *   A + B ->0    Direction parallel to world Z (gimbal lock). Yaw is
	 *       genuinely indeterminate at this configuration. A property of
	 *       the Pitch+Yaw parameterization, not of the algorithm. Returns
	 *       Yaw = 0 as the stable choice.
	 *
	 *   L <     Zero-length Direction (pivot at camera position). Returns
	 *       (0, 0); upstream code should guard before calling here.
	 */
	inline std::pair<float, float> SolveCameraRotationForScreenTarget(
		float TanHalfHOR,
		float AspectRatio,
		const FVector& Direction,
		float ScreenX,
		float ScreenY)
	{
		// Math is done in double. Arcsin near 1 and atan2 near (0, 0) both
		// benefit from the extra precision; cost is negligible vs. the trig.
		const double TanHalfVOR = TanHalfHOR / AspectRatio;
		const double u = 2.0 * ScreenX * TanHalfHOR;
		const double v = 2.0 * ScreenY * TanHalfVOR;
		const double s = FMath::Sqrt(1.0 + u * u + v * v);

		const double A = Direction.X;
		const double B = Direction.Y;
		const double C = Direction.Z;
		const double L = FMath::Sqrt(A * A + B * B + C * C);

		if (L < UE_DOUBLE_KINDA_SMALL_NUMBER)
		{
			// Pivot collocated with camera. Identity preserves the caller's
			// existing rotation through downstream NormalizedDeltaRotator.
			return { 0.f, 0.f };
		}

		const double K = s / L;

		// Solve X (Pitch) from equation (III).
		const double SqrtOnePlusVSq = FMath::Sqrt(1.0 + v * v);
		// T saturates at 1 when the pivot is outside the FOV cone; clamp
		// so arcsin stays in its real domain. Result is the closest in-FOV
		// pitch.
		const double T = FMath::Clamp((K * C) / SqrtOnePlusVSq, -1.0, 1.0);
		const double X = FMath::Asin(T) - FMath::Atan(v);

		// Solve Y (Yaw) from equations (I) and (II).
		if (A * A + B * B < UE_DOUBLE_KINDA_SMALL_NUMBER)
		{
			// Direction parallel to world Z. Gimbal lock. Yaw is
			// genuinely free; return 0 as the stable choice.
			return { static_cast<float>(FMath::RadiansToDegrees(X)), 0.f };
		}

		const double SinX = FMath::Sin(X);
		const double CosX = FMath::Cos(X);
		const double Alpha = CosX - v * SinX;
		const double Y = FMath::Atan2(Alpha * B - u * A, Alpha * A + u * B);

		return { static_cast<float>(FMath::RadiansToDegrees(X)),
		         static_cast<float>(FMath::RadiansToDegrees(Y)) };
	}

	/**
	 * Forward projection: a world point ->normalized screen coords [-0.5, 0.5],
	 * matching the convention used by SafeZoneCenter on ScreenSpacePivotNode and
	 * Placement.ScreenPosition / Aim.ScreenPosition on the Shot data structs.
	 *
	 * Companion to SolveCameraRotationForScreenTarget (which goes the other
	 * direction). Both use the same projection model and the same screen-coord
	 * convention so callers can round-trip cleanly.
	 *
	 *   1. Transform WorldPoint into camera space:
	 *        P_cam = R?* (WorldPoint - CameraPos)
	 *      where R is the camera-to-world rotation. Px = depth (forward),
	 *      Py = right, Pz = up.
	 *
	 *   2. If Px <= 0, the point is behind the camera or on the near plane -
	 *      no valid screen projection. Returns false; OutScreenCoord left
	 *      unchanged.
	 *
	 *   3. Apply the perspective division using the screen-coord convention
	 *      (Py / (2m*Px), Pz / (2n*Px)) where m = tan(FOV_h/2),
	 *      n = m / AspectRatio.
	 *
	 * @param WorldPoint        Point to project.
	 * @param CameraPos         Camera world position.
	 * @param CameraRot         Camera world rotation (Pitch, Yaw, Roll allowed).
	 * @param TanHalfHOR        tan(FOV_horizontal / 2). Same input convention as
	 *                          SolveCameraRotationForScreenTarget.
	 * @param AspectRatio       Viewport aspect ratio (width / height).
	 * @param OutScreenCoord    Normalized screen coords in [-0.5, 0.5] when the
	 *                          point is on screen. But values OUTSIDE that range
	 *                          are returned for off-screen points (no clamping).
	 *                          The Composition Solver's micro-refinement pass
	 *                          uses the unclamped values as a gradient signal.
	 *
	 * @return  True iff the point is in front of the camera (Px > 0).
	 */
	inline bool ProjectWorldPointToScreen(
		const FVector& WorldPoint,
		const FVector& CameraPos,
		const FRotator& CameraRot,
		float TanHalfHOR,
		float AspectRatio,
		FVector2D& OutScreenCoord)
	{
		const FVector PCam = CameraRot.UnrotateVector(WorldPoint - CameraPos);
		if (PCam.X <= UE_KINDA_SMALL_NUMBER)
		{
			return false;
		}

		const float TanHalfVOR = TanHalfHOR / AspectRatio;
		OutScreenCoord.X = PCam.Y / (2.f * TanHalfHOR * PCam.X);
		OutScreenCoord.Y = PCam.Z / (2.f * TanHalfVOR * PCam.X);
		return true;
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

	/** Get the perpendicular vector's length from any vector B projecting onto a unit vector A.
	 * i.e., (B - A * (A.Dot(B)).Length().
	 */
	inline float GetProjectPerpLength(const FVector& A, const FVector& B)
	{
		return (B - A * (A.Dot(B))).Length();
	}

	/** Get the point projected from B to a unit vector A. */
	inline FVector GetProjectedPoint(const FVector& A, const FVector& B)
	{
		return A * (A.Dot(B));
	}
	
}
