// Copyright 2026 Sulley. All Rights Reserved.

#include "Math/ComposableCameraLockOnAimPoint.h"

#include "Kismet/KismetMathLibrary.h"

namespace
{
	constexpr float MinSafeRadius = UE_SMALL_NUMBER;
	constexpr float MinSafeDenominator = 1.e-4f;
	constexpr float MaxAbsPitchDegrees = 89.f;
	constexpr float AdditionDoneTolerance = 0.01f;

	FVector GetSafePlanarDirection(const FVector& Preferred, const FVector& CameraForward)
	{
		FVector Planar(Preferred.X, Preferred.Y, 0.f);
		if (!Planar.Normalize())
		{
			Planar = FVector(CameraForward.X, CameraForward.Y, 0.f);
			if (!Planar.Normalize())
			{
				Planar = FVector::ForwardVector;
			}
		}
		return Planar;
	}

	float ClampPitchToRange(float Pitch, const FVector2D& PitchRange)
	{
		const float RangeMin = FMath::Min(PitchRange.X, PitchRange.Y);
		const float RangeMax = FMath::Max(PitchRange.X, PitchRange.Y);
		const float SafeMin = FMath::Clamp(RangeMin, -MaxAbsPitchDegrees, MaxAbsPitchDegrees);
		const float SafeMax = FMath::Clamp(RangeMax, -MaxAbsPitchDegrees, MaxAbsPitchDegrees);
		return FMath::Clamp(Pitch, SafeMin, SafeMax);
	}

	FVector ComputePitchAddition(
		const FVector& FollowToAim,
		const FVector& CameraForward,
		float Radius,
		float Pitch)
	{
		const FVector PlanarDirection = GetSafePlanarDirection(FollowToAim, CameraForward);
		const FVector TargetFollowToAim =
			PlanarDirection * Radius
			+ FVector::UpVector * Radius * FMath::Tan(FMath::DegreesToRadians(Pitch));

		return TargetFollowToAim - FollowToAim;
	}

	FVector ComputeCameraToAimAddition(
		const FVector& FollowPosition,
		const FVector& AimPosition,
		const FVector& CameraPosition,
		float Radius)
	{
		FVector CameraToAim = AimPosition - CameraPosition;
		const float CurrentLength = static_cast<float>(CameraToAim.Size());
		if (!CameraToAim.Normalize())
		{
			return FVector::ZeroVector;
		}

		const FVector FollowToCamera = CameraPosition - FollowPosition;
		const float A = CameraToAim.X * CameraToAim.X + CameraToAim.Y * CameraToAim.Y;
		if (A <= MinSafeDenominator)
		{
			return FVector::ZeroVector;
		}

		const float B = 2.f * (FollowToCamera.X * CameraToAim.X + FollowToCamera.Y * CameraToAim.Y);
		const float C = FollowToCamera.X * FollowToCamera.X + FollowToCamera.Y * FollowToCamera.Y - Radius * Radius;
		const float Delta = B * B - 4.f * A * C;
		if (Delta < 0.f)
		{
			return FVector::ZeroVector;
		}

		const float TargetLength = (-B + FMath::Sqrt(Delta)) / (2.f * A);
		if (TargetLength <= 0.f)
		{
			return FVector::ZeroVector;
		}

		return CameraToAim * (TargetLength - CurrentLength);
	}

	FVector ComputeCameraForwardAddition(
		const FVector& FollowPosition,
		const FVector& AimPosition,
		const FVector& CameraForward,
		float Radius)
	{
		const FVector CameraDirection = GetSafePlanarDirection(CameraForward, FVector::ForwardVector);
		const FVector FollowToAim = AimPosition - FollowPosition;

		const float A = CameraDirection.X * CameraDirection.X + CameraDirection.Y * CameraDirection.Y;
		if (A <= MinSafeDenominator)
		{
			return FVector::ZeroVector;
		}

		const float B = 2.f * (FollowToAim.X * CameraDirection.X + FollowToAim.Y * CameraDirection.Y);
		const float C = FollowToAim.X * FollowToAim.X + FollowToAim.Y * FollowToAim.Y - Radius * Radius;
		const float Delta = B * B - 4.f * A * C;
		if (Delta < 0.f)
		{
			return FVector::ZeroVector;
		}

		const float Magnitude = (-B + FMath::Sqrt(Delta)) / (2.f * A);
		return Magnitude > 0.f ? CameraDirection * Magnitude : FVector::ZeroVector;
	}

	FVector BlendOutCurrentAddition(
		const FVector& AimPosition,
		FComposableCameraLockOnAimPointState& State,
		float DeltaTime,
		float BlendOutTime)
	{
		State.bInModify = false;
		if (!State.bHasCurrentAddition)
		{
			return AimPosition;
		}

		if (BlendOutTime <= UE_SMALL_NUMBER)
		{
			State.CurrentAddition = FVector::ZeroVector;
		}
		else
		{
			if (!State.bIsBlendingOut)
			{
				State.bIsBlendingOut = true;
				State.BlendOutElapsedTime = 0.f;
				State.BlendOutStartAddition = State.CurrentAddition;
			}

			if (DeltaTime > UE_SMALL_NUMBER)
			{
				State.BlendOutElapsedTime += DeltaTime;
			}

			const float Alpha = FMath::Clamp(State.BlendOutElapsedTime / BlendOutTime, 0.f, 1.f);
			State.CurrentAddition = State.BlendOutStartAddition * (1.f - Alpha);
		}

		if (State.CurrentAddition.IsNearlyZero(AdditionDoneTolerance))
		{
			State.CurrentAddition = FVector::ZeroVector;
			State.bHasCurrentAddition = false;
			State.bIsBlendingOut = false;
			State.BlendOutElapsedTime = 0.f;
			State.BlendOutStartAddition = FVector::ZeroVector;
			return AimPosition;
		}

		return AimPosition + State.CurrentAddition;
	}

	float GetPitchForRange(
		const FVector& FollowPosition,
		const FVector& AimPosition,
		const FVector2D& PitchRange)
	{
		const float CurrentPitch = UKismetMathLibrary::FindLookAtRotation(FollowPosition, AimPosition).Pitch;
		return ClampPitchToRange(CurrentPitch, PitchRange);
	}
}

namespace ComposableCameraSystem
{
	FVector ComputeLockOnAimPoint(
		const FVector& FollowPosition,
		const FVector& AimPosition,
		const FVector& CameraPosition,
		const FVector& CameraForward,
		float Radius,
		const FVector& Weights,
		const FVector2D& PitchRange,
		FComposableCameraLockOnAimPointState& State,
		float DeltaTime,
		float BlendOutTime)
	{
		if (Radius <= MinSafeRadius)
		{
			return BlendOutCurrentAddition(AimPosition, State, DeltaTime, BlendOutTime);
		}

		const float PlanarDistance = FVector::Dist2D(FollowPosition, AimPosition);
		if (PlanarDistance >= Radius)
		{
			return BlendOutCurrentAddition(AimPosition, State, DeltaTime, BlendOutTime);
		}

		State.bInModify = true;

		const FVector FollowToAim = AimPosition - FollowPosition;
		const float Pitch = GetPitchForRange(FollowPosition, AimPosition, PitchRange);
		const FVector PitchAddition = ComputePitchAddition(FollowToAim, CameraForward, Radius, Pitch);
		const FVector CameraToAimAddition = ComputeCameraToAimAddition(FollowPosition, AimPosition, CameraPosition, Radius);
		const FVector CameraForwardAddition = ComputeCameraForwardAddition(FollowPosition, AimPosition, CameraForward, Radius);

		const FVector Addition =
			PitchAddition * Weights.X
			+ CameraToAimAddition * Weights.Y
			+ CameraForwardAddition * Weights.Z;

		State.CurrentAddition = Addition;
		State.bHasCurrentAddition = true;
		State.bIsBlendingOut = false;
		State.BlendOutElapsedTime = 0.f;
		State.BlendOutStartAddition = FVector::ZeroVector;

		return AimPosition + State.CurrentAddition;
	}
}
