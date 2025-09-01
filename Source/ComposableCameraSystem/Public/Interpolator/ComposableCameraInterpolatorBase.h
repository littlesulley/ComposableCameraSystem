// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ComposableCameraInterpolatorBase.generated.h"

class UComposableCameraInterpolatorBase;

template <typename ValueType>
struct TValueTypeWrapper
{
	TValueTypeWrapper()
		: Value{} {}
	TValueTypeWrapper(const ValueType& Value)
		: Value(Value) { }
	
	TValueTypeWrapper& operator-= (const TValueTypeWrapper& RHS)
	{
		Value -= RHS.Value;
		return *this;
	}
	TValueTypeWrapper& operator+= (const TValueTypeWrapper& RHS)
	{
		Value += RHS.Value;
		return *this;
	}
	TValueTypeWrapper& operator*= (double Multiplier)
	{
		Value *= Multiplier;
		return *this;
	}
	friend TValueTypeWrapper operator- (TValueTypeWrapper LHS, const TValueTypeWrapper& RHS)
	{
		LHS -= RHS;
		return LHS;
	}
	friend TValueTypeWrapper operator+ (TValueTypeWrapper LHS, const TValueTypeWrapper& RHS)
	{
		LHS += RHS;
		return LHS;
	}
	TValueTypeWrapper operator* (double Multiplier) const
	{
		return Value * Multiplier;
	}

	ValueType Value {};
};

template <>
struct TValueTypeWrapper<FRotator>
{
	TValueTypeWrapper()
		: Value{} {}
	TValueTypeWrapper(const FRotator& Value)
		: Value(Value) { }
	
	TValueTypeWrapper& operator-= (const TValueTypeWrapper& RHS)
	{
		Value = (Value - RHS.Value).GetNormalized();
		return *this;
	}
	TValueTypeWrapper& operator+= (const TValueTypeWrapper& RHS)
	{
		Value = (Value + RHS.Value).GetNormalized();
		return *this;
	}
	TValueTypeWrapper& operator*= (double Multiplier)
	{
		Value *= Multiplier;
		return *this;
	}
	friend TValueTypeWrapper operator- (TValueTypeWrapper LHS, const TValueTypeWrapper& RHS)
	{
		LHS -= RHS;
		return LHS;
	}
	friend TValueTypeWrapper operator+ (TValueTypeWrapper LHS, const TValueTypeWrapper& RHS)
	{
		LHS += RHS;
		return LHS;
	}
	TValueTypeWrapper operator* (double Multiplier) const
	{
		return Value * Multiplier;
	}
	
	FRotator Value {};
};

template <>
struct TValueTypeWrapper<FQuat>
{
	TValueTypeWrapper()
		: Value{} {}
	TValueTypeWrapper(const FQuat& Value)
		: Value(Value) { }
	
	TValueTypeWrapper& operator-= (const TValueTypeWrapper& RHS)
	{
		Value = (Value * RHS.Value.Inverse());
		return *this;
	}
	TValueTypeWrapper& operator+= (const TValueTypeWrapper& RHS)
	{
		Value = (Value * RHS.Value);
		return *this;
	}
	TValueTypeWrapper& operator*= (double Multiplier)
	{
		Value = FQuat{ Value.GetRotationAxis(), Value.GetAngle() * Multiplier };
		return *this;
	}
	friend TValueTypeWrapper operator- (TValueTypeWrapper LHS, const TValueTypeWrapper& RHS)
	{
		LHS -= RHS;
		return LHS;
	}
	friend TValueTypeWrapper operator+ (TValueTypeWrapper LHS, const TValueTypeWrapper& RHS)
	{
		LHS += RHS;
		return LHS;
	}

	TValueTypeWrapper operator* (double Multiplier) const
	{
		return FQuat{ Value.GetRotationAxis(), Value.GetAngle() * Multiplier };
	}
	
	FQuat Value {};
};

template <typename WrapperType>
class TCameraInterpolator;

template <typename ValueType>
class TCameraInterpolator<TValueTypeWrapper<ValueType>>
{
public:
	using ConstValueType = const ValueType;
	using WrappedValueType = TValueTypeWrapper<ValueType>;

	TCameraInterpolator(const UComposableCameraInterpolatorBase* Interpolator)
		: Interpolator(Interpolator)
		, CurrentValue(ValueType{})
		, TargetValue(ValueType{})
		, bFinished(false)
	{}

	virtual ~TCameraInterpolator() {}

	ValueType GetCurrentValue() const { return CurrentValue.Value; }
	ValueType GetTargetValue() const { return TargetValue.Value; }
	bool IsFinished() const { return bFinished; }

	void Reset(ConstValueType NewCurrentValue,  ConstValueType NewTargetValue)
	{
		ValueType OldCurrentValue = CurrentValue.Value;
		ValueType OldTargetValue = TargetValue.Value;
		CurrentValue = NewCurrentValue;
		TargetValue = NewTargetValue;
		OnReset(OldCurrentValue, OldTargetValue, NewCurrentValue, NewTargetValue);
	}
	
	virtual ValueType Run(const float DeltaTime) = 0;

protected:
	virtual void OnReset(ConstValueType OldCurrentValue,  ConstValueType OldTargetValue, ConstValueType NewCurrentValue, ConstValueType NewTargetValue) = 0;

protected:
	const UComposableCameraInterpolatorBase* Interpolator;
	WrappedValueType CurrentValue;
	WrappedValueType TargetValue;
	bool bFinished;
};

/**
 * Base interpolator.
 */
UCLASS(Abstract, DefaultToInstanced, EditInlineNew)
class COMPOSABLECAMERASYSTEM_API UComposableCameraInterpolatorBase
	: public UObject
{
	GENERATED_BODY()

protected:
	virtual TUniquePtr<TCameraInterpolator<TValueTypeWrapper<double>>> BuildDoubleInterpolator() const { return nullptr; }
	virtual TUniquePtr<TCameraInterpolator<TValueTypeWrapper<FVector2d>>> BuildVector2dInterpolator() const { return nullptr; }
	virtual TUniquePtr<TCameraInterpolator<TValueTypeWrapper<FVector3d>>> BuildVector3dInterpolator() const { return nullptr; }
	virtual TUniquePtr<TCameraInterpolator<TValueTypeWrapper<FQuat>>> BuildQuatInterpolator() const { return nullptr; }
	virtual TUniquePtr<TCameraInterpolator<TValueTypeWrapper<FRotator>>> BuildRotatorInterpolator() const { return nullptr; }
};

#define COMPOSABLECAMERASYSTEM_DECLARE_CAMERA_INTERPOLATOR() \
		protected: \
			virtual TUniquePtr<TCameraInterpolator<TValueTypeWrapper<double>>> BuildDoubleInterpolator() const override; \
			virtual TUniquePtr<TCameraInterpolator<TValueTypeWrapper<FVector2d>>> BuildVector2dInterpolator() const override; \
			virtual TUniquePtr<TCameraInterpolator<TValueTypeWrapper<FVector3d>>> BuildVector3dInterpolator() const override; \
			virtual TUniquePtr<TCameraInterpolator<TValueTypeWrapper<FQuat>>> BuildQuatInterpolator() const override; \
			virtual TUniquePtr<TCameraInterpolator<TValueTypeWrapper<FRotator>>> BuildRotatorInterpolator() const override; 

#define COMPOSABLECAMERASYSTEM_DEFINE_CAMERA_INTERPOLATOR(ThisClass, TInterpolatorClass) \
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<double>>> ThisClass::BuildDoubleInterpolator() const \
	{ \
		return MakeUnique<TInterpolatorClass<double>>(this); \
	} \
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<FVector2d>>> ThisClass::BuildVector2dInterpolator() const \
	{ \
		return MakeUnique<TInterpolatorClass<FVector2d>>(this); \
	} \
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<FVector3d>>> ThisClass::BuildVector3dInterpolator() const \
	{ \
		return MakeUnique<TInterpolatorClass<FVector3d>>(this); \
	} \
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<FQuat>>> ThisClass::BuildQuatInterpolator() const \
	{ \
		return MakeUnique<TInterpolatorClass<FQuat>>(this); \
	} \
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<FRotator>>> ThisClass::BuildRotatorInterpolator() const \
	{ \
		return MakeUnique<TInterpolatorClass<FRotator>>(this); \
	} 