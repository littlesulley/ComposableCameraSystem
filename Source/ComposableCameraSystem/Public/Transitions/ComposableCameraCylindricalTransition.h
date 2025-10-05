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
		
		FVector O1 = this->Origin + this->Direction * this->MinimumDistance;
		FVector O2 = OtherRay.Origin + OtherRay.Direction * OtherRay.MinimumDistance;
		FVector W0 = O1 - O2;
		
		float W1 = FVector::DotProduct(D1, W0);
		float W2 = FVector::DotProduct(D2, W0);
		float Dot = FVector::DotProduct(D1, D2);
		float SquaredDot = FMath::Square(Dot);

		if (FMath::IsNearlyZero(Dot, 1e-2f))
		{
			Result.bIsParallel = true;

			if (W2 <= 0.f)
			{
				Result.SecondPoint = O2;
				Result.FirstPoint = O1 + (-W1) * D1;
				Result.Distance = FVector::Dist(Result.FirstPoint, Result.SecondPoint);
			}
			else
			{
				Result.FirstPoint = O1;
				Result.SecondPoint = O2 + W2 * D2;
				Result.Distance = FVector::Dist(Result.FirstPoint, Result.SecondPoint);
			}

			return Result;
		}

		float T1 = (Dot * W2 - W1) / SquaredDot;
		float T2 = (Dot * W1 - W2) / SquaredDot;

		// Considering if T1 or T2 is negative.
		if (T1 < 0.f)
		{
			T1 = 0.f;
			T2 = W2 > 0.f ? W2 : 0.0f;
		}
		else if (T2 < 0.f)
		{
			T2 = 0.f;
			T1 = -W1 > 0.f ? -W1 : 0.0f;
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

	// Pivot move interpolator.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced)
	UComposableCameraInterpolatorBase* PivotInterpolator;

private:
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<FVector3d>>> Interpolator_T;
};
