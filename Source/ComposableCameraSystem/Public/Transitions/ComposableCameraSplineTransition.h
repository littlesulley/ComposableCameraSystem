// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraTransitionBase.h"
#include "ComposableCameraSplineTransition.generated.h"

UENUM()
enum class EComposableCameraSplineTransitionType : uint8
{
	// Using a cubic polynomial to generate the transition spline, given start tangent and end tangent.
	Hermite,
	
	// Using a cubic Bézier curve to generate the transition spline, given start tangent and end tangent.
	Bezier,
	
	// Using piecewise catmull-rom splines to generate the transition spline, given knots between start and end points.
	CatmullRom,

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
	virtual void OnBeginPlay_Implementation(float DeltaTime, const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose) override;
	virtual FComposableCameraPose OnEvaluate_Implementation(float DeltaTime, const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose) override;

public:
	// Spline type: Hermite, Bezier, B-Spline, Arc.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EComposableCameraSplineTransitionType SplineType { EComposableCameraSplineTransitionType::Hermite };

	// How does the camera will move on the spline.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EComposableCameraSplineTransitionEvaluationCurveType EvaluationCurveType { EComposableCameraSplineTransitionEvaluationCurveType::Smoother };

	// Start tangent, relative to the transform formed by the direction from start position to end position.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "SplineType == EComposableCameraSplineTransitionType::Hermite", EditConditionHides))
	FVector StartTangent { 0.f, 100.f, 0.f };

	// End tangent, relative to the transform formed by the direction from start position to end position.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "SplineType == EComposableCameraSplineTransitionType::Hermite", EditConditionHides))
	FVector EndTangent { 0.f, 100.f, 0.f };

	// Start control point, relative to the start position and the transform formed by the direction from start position to end position.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "SplineType == EComposableCameraSplineTransitionType::Bezier", EditConditionHides))
	FVector StartControlPoint { 0.f, 100.f, 0.f };

	// End control point, relative to the end position and the transform formed by the direction from start position to end position.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "SplineType == EComposableCameraSplineTransitionType::Bezier", EditConditionHides))
	FVector EndControlPoint { 0.f, 100.f, 0.f };

	// Control points for Catmull Roll splines. Defined relative to the transform formed by the direction from start position to end position.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "SplineType == EComposableCameraSplineTransitionType::CatmullRom", EditConditionHides))
	TArray<FVector> ControlPoints;
	
	// The angle that the desired arc spans. 180 means a half circle, 90 means a quarter circle. 270 is also a quarter circle but in the opposite direction.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1", ClampMax = "359", EditCondition = "SplineType == EComposableCameraSplineTransitionType::Arc", EditConditionHides))
	float ArcAngle { 180.f };

	// The roll that the desired arc has along the forward direction defined by the start pose location and the target pose location.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "-180", ClampMax = "180", EditCondition = "SplineType == EComposableCameraSplineTransitionType::Arc", EditConditionHides))
	float ArcRoll { 0.f };

private:

#if ENABLE_DRAW_DEBUG
	void DrawDebugSpline(const FComposableCameraPose& StartPose, const FComposableCameraPose& TargetPose);
#endif
};
