// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "ComposableCameraIIRInterpolator.generated.h"

template <typename ValueType>
struct TIIRInterpolatorTraits {};

template <>
struct TIIRInterpolatorTraits<double>
{
	static double InterpTo(double CurrentValue, double TargetValue, float DeltaTime, double Speed)
	{
		return FMath::FInterpTo(CurrentValue, TargetValue, DeltaTime, Speed);
	}
};

template <>
struct TIIRInterpolatorTraits<FVector2d>
{
	static FVector2d InterpTo(const FVector2d& CurrentValue, const FVector2d& TargetValue, float DeltaTime, double Speed)
	{
		return FMath::Vector2DInterpTo(CurrentValue, TargetValue, DeltaTime, Speed);
	}
};

template <>
struct TIIRInterpolatorTraits<FVector3d>
{
	static FVector3d InterpTo(const FVector3d& CurrentValue, const FVector3d& TargetValue, float DeltaTime, double Speed)
	{
		return FMath::VInterpTo(CurrentValue, TargetValue, DeltaTime, Speed);
	}
};

template <>
struct TIIRInterpolatorTraits<FRotator>
{
	static FRotator InterpTo(const FRotator& CurrentValue, const FRotator& TargetValue, float DeltaTime, double Speed)
	{
		return FMath::RInterpTo(CurrentValue, TargetValue, DeltaTime, Speed);
	}
};

template <>
struct TIIRInterpolatorTraits<FQuat>
{
	static FQuat InterpTo(const FQuat& CurrentValue, const FQuat& TargetValue, float DeltaTime, double Speed)
	{
		return FMath::QInterpTo(CurrentValue, TargetValue, DeltaTime, Speed);
	}
};

/**
 * IIR interpolator.
 */
UCLASS(BlueprintType, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraIIRInterpolator
	: public UComposableCameraInterpolatorBase
{
	GENERATED_BODY()

	COMPOSABLECAMERASYSTEM_DECLARE_CAMERA_INTERPOLATOR()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly,  Category = "Interpolator")
	float Speed = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,  Category = "Interpolator")
	bool bUseFixedStep = true;
};

template <typename ValueType>
class TIIRInterpolator : public TCameraInterpolator<TValueTypeWrapper<ValueType>>
{
public:
	TIIRInterpolator(const UComposableCameraIIRInterpolator* Interpolator)
		: TCameraInterpolator<TValueTypeWrapper<ValueType>>(Interpolator)
		, IIRInterpolator(Interpolator)
		, Speed(Interpolator->Speed)
		, bUseFixedStep(Interpolator->bUseFixedStep)
	{}

	TIIRInterpolator(const float InSpeed, const bool InUseFixedStep)
		: TCameraInterpolator<TValueTypeWrapper<ValueType>>(nullptr)
		, IIRInterpolator(nullptr)
		, Speed(InSpeed)
		, bUseFixedStep(InUseFixedStep)
	{}

	using ConstValueType = typename TCameraInterpolator<TValueTypeWrapper<ValueType>>::ConstValueType;
	using WrappedValueType = typename TCameraInterpolator<TValueTypeWrapper<ValueType>>::WrappedValueType;
	
	virtual ValueType Run(const float DeltaTime) override
	{
		if (IIRInterpolator)
		{
			Speed = IIRInterpolator->Speed;
			bUseFixedStep = IIRInterpolator->bUseFixedStep;
		}
		
		if (bUseFixedStep)
		{
			float RemainingTime = DeltaTime;
			if (RemainingTime > KINDA_SMALL_NUMBER)
			{
				WrappedValueType LastTargetToTargetValue = this->TargetValue - LastTargetValue;
				const WrappedValueType EquilibriumStepRate = LastTargetToTargetValue * (1.f / RemainingTime);
				WrappedValueType LerpedTargetValue = LastTargetValue;

				while (RemainingTime > KINDA_SMALL_NUMBER)
				{
					const float StepTime = FMath::Min(MaxSubstepTime, RemainingTime);

					LerpedTargetValue = EquilibriumStepRate * StepTime + LerpedTargetValue;
					RemainingTime -= StepTime;
					this->CurrentValue = RunSubstep(LerpedTargetValue.Value, StepTime);
				}

				LastTargetValue = LerpedTargetValue;
			}
		}
		else
		{
			this->CurrentValue = RunSubstep((this->TargetValue).Value, DeltaTime);
		}

		return (this->CurrentValue).Value;
	}
	
protected:
	virtual void OnReset(ConstValueType OldCurrentValue,  ConstValueType OldTargetValue, ConstValueType NewCurrentValue, ConstValueType NewTargetValue) override
	{
		LastTargetValue = this->TargetValue;
	}

	ValueType RunSubstep(ValueType SubstepTargetValue, float DeltaTime)
	{
		return TIIRInterpolatorTraits<ValueType>::InterpTo((this->CurrentValue).Value, SubstepTargetValue, DeltaTime, Speed);
	}

private:
	static constexpr float MaxSubstepTime = 1.f / 120.f;

	const UComposableCameraIIRInterpolator* IIRInterpolator;
	float Speed = 1.f;
	bool bUseFixedStep = false;

	WrappedValueType LastTargetValue {};
};
