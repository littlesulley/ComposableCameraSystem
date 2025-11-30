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
	
	
	BasicSpline
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
};
