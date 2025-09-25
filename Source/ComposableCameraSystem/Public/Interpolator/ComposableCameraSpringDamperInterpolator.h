// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraInterpolatorBase.h"
#include "ComposableCameraSpringDamperInterpolator.generated.h"

template <typename ValueType>
struct TSpringDamperInterpolatorTraits
{
	using IntermediateValueType = ValueType;
	
	static TValueTypeWrapper<ValueType>& ConvertTo(TValueTypeWrapper<ValueType>& WrappedValue)
	{
		return WrappedValue;
	}

	static TValueTypeWrapper<ValueType>& ConvertFrom(TValueTypeWrapper<ValueType>& WrappedValue)
	{
		return WrappedValue;
	}
};

template <>
struct TSpringDamperInterpolatorTraits<FRotator>
{
	using IntermediateValueType = FVector;
	
	static TValueTypeWrapper<FVector> ConvertTo(TValueTypeWrapper<FRotator>& WrappedValue)
	{
		FVector IntermediateValue { WrappedValue.Value.Pitch, WrappedValue.Value.Yaw, WrappedValue.Value.Roll };
		return IntermediateValue;
	}

	static TValueTypeWrapper<FRotator> ConvertFrom(TValueTypeWrapper<FVector>& WrappedValue)
	{
		FRotator IntermediateValue { WrappedValue.Value[0], WrappedValue.Value[1], WrappedValue.Value[2] };
		return IntermediateValue;
	}
};

template <>
struct TSpringDamperInterpolatorTraits<FQuat>
{
	using IntermediateValueType = FVector;
	
	static TValueTypeWrapper<FVector> ConvertTo(TValueTypeWrapper<FQuat>& WrappedValue)
	{
		FRotator Rotation = WrappedValue.Value.Rotator().GetDenormalized();
		FVector IntermediateValue { Rotation.Pitch, Rotation.Yaw, Rotation.Roll };
		return IntermediateValue;
	}

	static TValueTypeWrapper<FQuat> ConvertFrom(TValueTypeWrapper<FVector>& WrappedValue)
	{
		FRotator IntermediateValue { WrappedValue.Value[0], WrappedValue.Value[1], WrappedValue.Value[2] };
		return IntermediateValue.GetDenormalized().Quaternion();
	}
};

/**
 * Spring damper interpolator. 
 */
UCLASS(BlueprintType, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraSpringDamperInterpolator
	: public UComposableCameraInterpolatorBase
{
	GENERATED_BODY()

	COMPOSABLECAMERASYSTEM_DECLARE_CAMERA_INTERPOLATOR()

public:
	/** Controls the frequency of oscillation and the speed of decay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interpolator", Meta = (ClampMin = "0.0001"))
	float Frequency{ 3.1415926 };

	/** Controls whether the spring is undamped (=0), underdamped (<1), critically damped (=1), or overdamped (>1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interpolator", Meta = (ClampMin = "0"))
	float DampRatio{ 1.0 };
};

template <typename ValueType>
class TSpringDamperInterpolator
	: public TCameraInterpolator<TValueTypeWrapper<ValueType>>
{
public:
	TSpringDamperInterpolator(const UComposableCameraSpringDamperInterpolator* Interpolator)
		: TCameraInterpolator<TValueTypeWrapper<ValueType>>(Interpolator)
		, SpringDamperInterpolator(Interpolator)
		, Frequency(Interpolator->Frequency)
		, DampRatio(Interpolator->DampRatio)
	{}

	TSpringDamperInterpolator(const float InFrequency, const float InDampRatio)
		: TCameraInterpolator<TValueTypeWrapper<ValueType>>(nullptr)
		, SpringDamperInterpolator(nullptr)
		, Frequency(InFrequency)
		, DampRatio(InDampRatio)
	{}

	using ConstValueType = typename TCameraInterpolator<TValueTypeWrapper<ValueType>>::ConstValueType;
	using WrappedValueType = typename TCameraInterpolator<TValueTypeWrapper<ValueType>>::WrappedValueType;
	using IntermediateValueType = typename TSpringDamperInterpolatorTraits<ValueType>::IntermediateValueType;
	using WrappedIntermediateValueType = TValueTypeWrapper<IntermediateValueType>;
	
	virtual ValueType Run(const float DeltaTime) override
	{
		if (SpringDamperInterpolator)
		{
			Frequency = SpringDamperInterpolator->Frequency;
			DampRatio = SpringDamperInterpolator->DampRatio;
		}

		WrappedValueType StartValue = this->TargetValue - this->CurrentValue;
		WrappedIntermediateValueType Value = TSpringDamperInterpolatorTraits<ValueType>::ConvertTo(StartValue);
		WrappedIntermediateValueType Output;
		
		// Underdamped
		if (DampRatio < 1 && DampRatio > 0)
		{
			float SqrtOfOneMinusSquare = FMath::Sqrt(1.0 - DampRatio * DampRatio);
			float OmegaDotSqrt = Frequency * SqrtOfOneMinusSquare;
			float Inner = OmegaDotSqrt * DeltaTime;
			float OmegaDotZeta = Frequency * DampRatio;
			float Cosine = FMath::Cos(Inner);
			float Sine = FMath::Sin(Inner);

			float Decay = FMath::Exp(-OmegaDotZeta * DeltaTime);
			WrappedIntermediateValueType C1 = Value;
			WrappedIntermediateValueType C2 = (Velocity + OmegaDotZeta * Value) / (OmegaDotSqrt + 1e-5f);
			WrappedIntermediateValueType X = C1 * Decay * Cosine + C2 * Decay * Sine;
			Velocity = -OmegaDotZeta * X + (Velocity + OmegaDotZeta * Value) * Decay * Cosine - Value * OmegaDotSqrt * Decay * Sine;
			Output = Value.Value - X.Value;
		}
		// Critically damped
		else if (DampRatio == 1)
		{
			float Exp = FMath::Exp(-Frequency * DeltaTime);
			WrappedIntermediateValueType Inner = Velocity + Frequency * Value;
			WrappedIntermediateValueType XCoefficient = Value + Inner * DeltaTime;
			WrappedIntermediateValueType X = XCoefficient * Exp;
			WrappedIntermediateValueType VCoefficient = Velocity - Inner * Frequency * DeltaTime;
			Velocity = VCoefficient * Exp;
			Output = Value.Value - X.Value;
		}
		// Overdamped
		else if (DampRatio > 1)
		{
			float SqrtOfSquareMinusOne = FMath::Sqrt(DampRatio * DampRatio - 1.0);
			float ZetaPlusSqrt = DampRatio + SqrtOfSquareMinusOne;
			float ZetaMinusSqrt = DampRatio - SqrtOfSquareMinusOne;
			float NegOmegaDotPlus = -Frequency * ZetaPlusSqrt;
			float NegOmegaDotMinus = -Frequency * ZetaMinusSqrt;

			WrappedIntermediateValueType C1 = (-Velocity / Frequency - ZetaMinusSqrt * Value) / (2.0 * SqrtOfSquareMinusOne + 1e-5f);
			WrappedIntermediateValueType C2 = Value - C1;
			WrappedIntermediateValueType T1 = C1 * FMath::Exp(DeltaTime * NegOmegaDotPlus);
			WrappedIntermediateValueType T2 = C2 * FMath::Exp(DeltaTime * NegOmegaDotMinus);
			WrappedIntermediateValueType X = T1 + T2;
			Velocity = NegOmegaDotPlus * T1 + NegOmegaDotMinus * T2;
			Output = Value.Value - X.Value;
		}
		// Undamped
		else
		{
			float Cosine = FMath::Cos(Frequency * DeltaTime);
			float Sine = FMath::Sin(Frequency * DeltaTime);
			WrappedIntermediateValueType X = Velocity / Frequency * Sine + Value * Cosine;
			Velocity = Velocity * Cosine - Frequency * Value * Sine;
			Output = Value.Value - X.Value;
		}

		return TSpringDamperInterpolatorTraits<ValueType>::ConvertFrom(Output).Value;
	}

protected:
	virtual void OnReset(ConstValueType OldCurrentValue,  ConstValueType OldTargetValue, ConstValueType NewCurrentValue, ConstValueType NewTargetValue) override
	{}
	
private:
	const UComposableCameraSpringDamperInterpolator* SpringDamperInterpolator;
	float Frequency;
	float DampRatio;

	WrappedIntermediateValueType Velocity {}; 
};