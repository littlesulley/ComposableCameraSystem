// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraSmoothTransition.h"
#include "ComposableCameraTransitionBase.h"
#include "ComposableCameraSplineTransition.generated.h"

UENUM()
enum class EComposableCameraSplineTransitionType : uint8
{
	// Using a cubic polynomial to generate the transition spline, given start tangent and end tangent.
	Hermite,
	
	// Using a cubic Bézier curve to generate the transition spline, given start tangent and end tangent.
	Bezier,
	
	// Using a cubic basic spline to generate the transition spline, given knots between start and end points.
	BasicSpline,

	// Using an arc as the transition spline with a fixed given curvature.
	Arc
};

UENUM()
enum class EComposableCameraSplineTransitionEvaluationCurveType : uint8
{
	Smooth,
	Smoother,
	Linear,
	Cubic
};

/**
 * Spline transition.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraSplineTransition : public UComposableCameraTransitionBase
{
	GENERATED_BODY()
	
public:
	virtual void OnBeginPlay_Implementation(float DeltaTime, const FComposableCameraPose& CurrentTargetPose) override;
	virtual FComposableCameraPose OnEvaluate_Implementation(float DeltaTime, const FComposableCameraPose& CurrentTargetPose) override;

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EComposableCameraSplineTransitionType SplineType { EComposableCameraSplineTransitionType::Hermite };
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EComposableCameraSplineTransitionEvaluationCurveType EvaluationCurveType { EComposableCameraSplineTransitionEvaluationCurveType::Smoother };
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "SplineType == EComposableCameraSplineTransitionType::Hermite", EditConditionHides))
	FVector StartTangent { 0.f, 100.f, 0.f };
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "SplineType == EComposableCameraSplineTransitionType::Hermite", EditConditionHides))
	FVector EndTangent { 0.f, 100.f, 0.f };

	// The angle that the desired arc spans. 180 means a half circle, 90 means a quarter circle. 270 is also a quarter circle but in the opposite direction.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1", ClampMax = "359", EditCondition = "SplineType == EComposableCameraSplineTransitionType::Arc", EditConditionHides))
	float ArcAngle { 180.f };

	// The roll that the desired arc has along the forward direction defined by the start pose location and the target pose location.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "-180", ClampMax = "180", EditCondition = "SplineType == EComposableCameraSplineTransitionType::Arc", EditConditionHides))
	float ArcRoll { 0.f };

private:

	void DrawDebugSpline(const FComposableCameraPose& StartPose, const FComposableCameraPose& TargetPose);
	void DrawDebugSplinePoints(const TArray<FVector>& SplinePoints);
};
