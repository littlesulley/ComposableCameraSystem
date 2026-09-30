// Copyright 2026 Sulley. All Rights Reserved.

#include "Modifiers/ComposableCameraModifierTransition.h"

#include "Curves/CurveFloat.h"

float UComposableCameraModifierTransitionBase::EvaluateWeight(float ElapsedTime) const
{
	if (Duration <= 0.f)
	{
		return 1.f;
	}
	if (ElapsedTime >= Duration)
	{
		return 1.f;
	}

	const float T = FMath::Clamp(ElapsedTime / Duration, 0.f, 1.f);
	switch (BlendFunction)
	{
	case EComposableCameraModifierBlendFunction::Linear:
		return T;
	case EComposableCameraModifierBlendFunction::SmoothStep:
		return FMath::SmoothStep(0.f, 1.f, T);
	case EComposableCameraModifierBlendFunction::SmootherStep:
		return T * T * T * (T * (T * 6.f - 15.f) + 10.f);
	case EComposableCameraModifierBlendFunction::EaseIn:
		return FMath::Pow(T, FMath::Max(BlendExponent, UE_SMALL_NUMBER));
	case EComposableCameraModifierBlendFunction::EaseOut:
		return 1.f - FMath::Pow(1.f - T, FMath::Max(BlendExponent, UE_SMALL_NUMBER));
	case EComposableCameraModifierBlendFunction::EaseInOut:
		return T < 0.5f
			? 0.5f * FMath::Pow(T * 2.f, FMath::Max(BlendExponent, UE_SMALL_NUMBER))
			: 1.f - 0.5f * FMath::Pow((1.f - T) * 2.f, FMath::Max(BlendExponent, UE_SMALL_NUMBER));
	case EComposableCameraModifierBlendFunction::CustomCurve:
		return CustomCurve
			? FMath::Clamp(CustomCurve->GetFloatValue(T), 0.f, 1.f)
			: T;
	default:
		return T;
	}
}
