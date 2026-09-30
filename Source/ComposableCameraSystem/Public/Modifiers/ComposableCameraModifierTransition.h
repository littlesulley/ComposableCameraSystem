// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ComposableCameraModifierTransition.generated.h"

class UCurveFloat;

/** Scalar timing functions supported by modifier-value transitions. */
UENUM(BlueprintType)
enum class EComposableCameraModifierBlendFunction : uint8
{
	Linear,
	SmoothStep,
	SmootherStep,
	EaseIn,
	EaseOut,
	EaseInOut,
	CustomCurve
};

/**
 * Stateless timing template for an in-place Modifier value transition.
 *
 * This class returns only a normalized weight. Property-type interpolation
 * lives in the camera-owned modifier runtime state. It never evaluates poses
 * and never participates in the Evaluation Tree.
 */
UCLASS(BlueprintType, DefaultToInstanced, EditInlineNew, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraModifierTransitionBase : public UObject
{
	GENERATED_BODY()

public:
	/** Transition duration. Zero means immediate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Modifier Transition", meta = (ClampMin = "0"))
	float Duration = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Modifier Transition")
	EComposableCameraModifierBlendFunction BlendFunction =
		EComposableCameraModifierBlendFunction::SmoothStep;

	/** Exponent used by EaseIn, EaseOut, and EaseInOut. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Modifier Transition",
		meta = (ClampMin = "0.01", EditCondition = "BlendFunction==EComposableCameraModifierBlendFunction::EaseIn || BlendFunction==EComposableCameraModifierBlendFunction::EaseOut || BlendFunction==EComposableCameraModifierBlendFunction::EaseInOut"))
	float BlendExponent = 2.f;

	/** Optional normalized 0..1 curve used by CustomCurve. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Modifier Transition",
		meta = (EditCondition = "BlendFunction==EComposableCameraModifierBlendFunction::CustomCurve", EditConditionHides))
	TObjectPtr<UCurveFloat> CustomCurve;

	/**
	 * Discrete properties switch when the evaluated weight reaches this value.
	 * 1 = AtEnd, 0.5 = AtHalf, 0 = AtStart.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Modifier Transition",
		meta = (ClampMin = "0", ClampMax = "1"))
	float DiscreteSwitchWeight = 1.f;

	float EvaluateWeight(float ElapsedTime) const;
};
