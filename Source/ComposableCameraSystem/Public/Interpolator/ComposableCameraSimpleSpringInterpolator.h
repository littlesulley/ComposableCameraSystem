// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraInterpolatorBase.h"
#include "Math/ComposableCameraMath.h"
#include "ComposableCameraSimpleSpringInterpolator.generated.h"

template <typename ValueType>
struct TSimpleSpringInterpolatorTraits {};

template <>
struct TSimpleSpringInterpolatorTraits<double>
{
	static double Damp(double CurrentValue, double TargetValue, float DeltaTime, float DampTime)
	{
		return CurrentValue + ComposableCameraSystem::SimpleExpDamp(DeltaTime, DampTime, TargetValue - CurrentValue);
	}
};

template <>
struct TSimpleSpringInterpolatorTraits<FVector2d>
{
	static FVector2d Damp(const FVector2d& CurrentValue, const FVector2d& TargetValue,  float DeltaTime, float DampTime)
	{
		double NewValue_0 = TSimpleSpringInterpolatorTraits<double>::Damp(CurrentValue[0], TargetValue[0], DeltaTime, DampTime);
		double NewValue_1 = TSimpleSpringInterpolatorTraits<double>::Damp(CurrentValue[1], TargetValue[1], DeltaTime, DampTime);
		return { NewValue_0, NewValue_1 };
	}
};

template <>
struct TSimpleSpringInterpolatorTraits<FVector3d>
{
	static FVector3d Damp(const FVector3d& CurrentValue, const FVector3d& TargetValue,  float DeltaTime, float DampTime)
	{
		double NewValue_0 = TSimpleSpringInterpolatorTraits<double>::Damp(CurrentValue[0], TargetValue[0], DeltaTime, DampTime);
		double NewValue_1 = TSimpleSpringInterpolatorTraits<double>::Damp(CurrentValue[1], TargetValue[1], DeltaTime, DampTime);
		double NewValue_2 = TSimpleSpringInterpolatorTraits<double>::Damp(CurrentValue[2], TargetValue[2], DeltaTime, DampTime);
		return { NewValue_0, NewValue_1, NewValue_2 };
	}
};

template <>
struct TSimpleSpringInterpolatorTraits<FRotator>
{
	static FRotator Damp(const FRotator& CurrentValue, const FRotator& TargetValue,  float DeltaTime, float DampTime)
	{
		const FRotator Delta = (TargetValue - CurrentValue).GetNormalized();
		const double Scale = ComposableCameraSystem::SimpleExpDamp(DeltaTime, DampTime, 1.);
		return (CurrentValue + Delta * Scale).GetNormalized();
	}
};

template <>
struct TSimpleSpringInterpolatorTraits<FQuat>
{
	static FQuat Damp(const FQuat& CurrentValue, const FQuat& TargetValue,  float DeltaTime, float DampTime)
	{
		const double Progress = ComposableCameraSystem::SimpleExpDamp(DeltaTime, DampTime, 1.);
		return FQuat::Slerp(CurrentValue, TargetValue, Progress);
	}
};

/**
 * Simple exact spring interpolator.
 */
UCLASS(BlueprintType, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraSimpleSpringInterpolator : public UComposableCameraInterpolatorBase
{
	GENERATED_BODY()

	COMPOSABLECAMERASYSTEM_DECLARE_CAMERA_INTERPOLATOR();

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly,  Category = "Interpolator")
	float DampTime { 1.f };
};

template <typename ValueType>
class TSimpleSpringInterpolator : public TCameraInterpolator<TValueTypeWrapper<ValueType>>
{
public:
	TSimpleSpringInterpolator(const UComposableCameraSimpleSpringInterpolator* Interpolator)
		: TCameraInterpolator<TValueTypeWrapper<ValueType>>(Interpolator)
		, SimpleSpringInterpolator(Interpolator)
		, DampTime(Interpolator->DampTime)
	{}

	TSimpleSpringInterpolator(const float DampTime)
		: TCameraInterpolator<TValueTypeWrapper<ValueType>>(nullptr)
		, SimpleSpringInterpolator(nullptr)
		, DampTime(DampTime)
	{}

	using ConstValueType = typename TCameraInterpolator<TValueTypeWrapper<ValueType>>::ConstValueType;
	using WrappedValueType = typename TCameraInterpolator<TValueTypeWrapper<ValueType>>::WrappedValueType;

	virtual ValueType Run(const float DeltaTime) override
	{
		if (SimpleSpringInterpolator)
		{
			DampTime = SimpleSpringInterpolator->DampTime;
		}

		return TSimpleSpringInterpolatorTraits<ValueType>::Damp(
			this->CurrentValue.Value, this->TargetValue.Value, DeltaTime, DampTime);
	}

protected:
	virtual void OnReset(ConstValueType OldCurrentValue,  ConstValueType OldTargetValue, ConstValueType NewCurrentValue, ConstValueType NewTargetValue) override
	{ }

private:
	const UComposableCameraSimpleSpringInterpolator* SimpleSpringInterpolator;
	float DampTime = 1.f;
};
