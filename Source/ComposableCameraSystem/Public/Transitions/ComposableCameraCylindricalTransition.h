// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraTransitionBase.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "ComposableCameraCylindricalTransition.generated.h"

class UComposableCameraInterpolatorBase;

struct FComposableCameraNearestPointsOnRaysResult
{
	// The first point.
	FVector FirstPoint;

	// The second point.
	FVector SecondPoint;

	// Minimum distance between two rays.
	float Distance;

	// If two rays are parallel, in which case the points are randomly selected.
	bool bIsParallel;
};

struct FComposableCameraRayDefinition
{
	FComposableCameraRayDefinition(FVector InOrigin, FVector InDirection)
		: Origin(InOrigin), Direction(InDirection), MinimumDistance(0.f), bInfiniteDistance(true)
	{}

	FComposableCameraRayDefinition(FVector InOrigin, FVector InDirection, float InMinimumDistance)
		: Origin(InOrigin), Direction(InDirection), MinimumDistance(InMinimumDistance), bInfiniteDistance(true)
	{}

	FComposableCameraNearestPointsOnRaysResult FindNearestPointsByOtherRay(const FComposableCameraRayDefinition& OtherRay)
	{
		FComposableCameraNearestPointsOnRaysResult Result;
		
		FVector D1 = this->Direction.GetSafeNormal();
		FVector D2 = OtherRay.Direction.GetSafeNormal();
		
		FVector O1 = this->Origin + D1 * this->MinimumDistance;
		FVector O2 = OtherRay.Origin + D2 * OtherRay.MinimumDistance;
		FVector W0 = O1 - O2;
		
		float W1 = FVector::DotProduct(D1, W0);
		float W2 = FVector::DotProduct(D2, W0);
		float Dot = FVector::DotProduct(D1, D2);
		float SquaredDot = FMath::Square(Dot);

		if (FMath::IsNearlyEqual(FMath::Abs(Dot), 1.f, 1e-2f))
		{
			Result.bIsParallel = true;

			// In the same direction.
			if (Dot > 0.f)
			{
				if (W2 <= 0.f)  // Camera pushes forward.
				{
					Result.SecondPoint = O2;
					Result.FirstPoint = O1 + (-W1) * D1;
					Result.Distance = FVector::Dist(Result.FirstPoint, Result.SecondPoint);
				}
				else  // Camera pulls back.
				{
					Result.FirstPoint = O1;
					Result.SecondPoint = O2 + W2 * D2;
					Result.Distance = FVector::Dist(Result.FirstPoint, Result.SecondPoint);
				}
			}
			// In the opposite direction.
			else
			{
				if (W2 <= 0.f)                       //   <---------x (O1)
				{                                    //                  (O2) x-------->
					Result.FirstPoint = O1;
					Result.SecondPoint = O2;
					Result.Distance = FVector::Dist(Result.FirstPoint, Result.SecondPoint);
				}
				else                                //  <---------x (O1)
				{                                   //    (O2) x-------->
					Result.FirstPoint = O1 + (-W1 / 2.) * D1;
					Result.SecondPoint = O2 + (W2 / 2.) * D2;
					Result.Distance = FVector::Dist(Result.FirstPoint, Result.SecondPoint);
				}
			}

			return Result;
		}

		float T1 = (Dot * W2 - W1) / (1. - SquaredDot);
		float T2 = (W2 - Dot * W1) / (1. - SquaredDot);

		// Considering if T1 or T2 is negative.
		if (T1 < 0.f && T2 >= 0.f)
		{
			T1 = 0.f;
			T2 = FMath::Max(0.f, W2);
		}
		else if (T2 < 0.f && T1 >= 0.f)
		{
			T2 = 0.f;
			T1 = FMath::Max(0.f, -W1);
		}
		else if (T1 < 0.f && T2 < 0.f)
		{
			// Case 1: Use O1 and O2
			float BackupT1 = 0.f;
			float BackupT2 = 0.f;
			float BackupDist = FVector::Dist(O1, O2);
			T1 = BackupT1;
			T2 = BackupT2;

			// Case 2: Use O1 and projected O2
			BackupT1 = 0.f;
			BackupT2 = FMath::Max(0.f, W2);
			float Dist = FVector::Dist(O1 + T1 * D1, O2 + T2 * D2);
			if (Dist < BackupDist)
			{
				BackupDist = Dist;
				T1 = BackupT1;
				T2 = BackupT2;
			}

			// Case 3: Use projected O1 and O2
			BackupT1 = FMath::Max(0.f, -W1);
			BackupT2 = 0.f;
			Dist = FVector::Dist(O1 + T1 * D1, O2 + T2 * D2);
			if (Dist < BackupDist)
			{
				T1 = BackupT1;
				T2 = BackupT2;
			}
		}
		
		Result.FirstPoint = O1 + T1 * D1;
		Result.SecondPoint = O2 + T2 * D2;
		Result.Distance = FVector::Dist(Result.FirstPoint, Result.SecondPoint);
		Result.bIsParallel = false;
		
		return Result;
	}

	// Origin of the ray.
	FVector Origin;

	// Direction of the ray.
	FVector Direction;

	// Minimum distance from the origin along direction.
	float MinimumDistance;
	
	// Distance of the ray is bInifiniteDistance == false.
	float Distance;

	// If this ray has an infinite distance.
	bool bInfiniteDistance;
};

/**
 * Cylindrical transition.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraCylindricalTransition : public UComposableCameraTransitionBase
{
	GENERATED_BODY()

public:
	virtual void OnBeginPlay_Implementation(float DeltaTime, const FComposableCameraPose& CurrentTargetPose) override;
	virtual FComposableCameraPose OnEvaluate_Implementation(float DeltaTime, const FComposableCameraPose& CurrentTargetPose) override;

public:
	// Maintaining a minimum distance from origin along the camera's looking direction.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MinimumDistanceFromOrigin { 10.f };

	// Whether to lock the camera's rotation to the pivot.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bLockToPivot { true };
};
