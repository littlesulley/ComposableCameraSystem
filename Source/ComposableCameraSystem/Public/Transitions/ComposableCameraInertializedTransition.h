// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraTransitionBase.h"
#include "Math/ComposableCameraMath.h"
#include "ComposableCameraInertializedTransition.generated.h"

class UComposableCameraSmoothTransition;

template <size_t Order, typename ElementType>
class ComposableCameraPolynomial
{
public:
	template <class... CoefficientTypes>
	ComposableCameraPolynomial(CoefficientTypes&&... coefficients)
	{
		Coefficients = TStaticArray<ElementType, Order + 1>(coefficients...);
	}

	ElementType Evaluate(float TimeStamp)
	{
		ElementType Result = ElementType { 0.0f };
		for (auto index = 0; index < Order + 1; ++index)
		{
			Result = Result * TimeStamp + Coefficients[index]; 
		}
		return Result;
	}
	
private:
	TStaticArray<ElementType, Order + 1> Coefficients;
};

template <typename DataType, typename ConcreteInertializerType>
	requires requires (ConcreteInertializerType I) {
		{ I.Evaluate( 0.0f, DataType{} ) } -> std::convertible_to<DataType>;
		{ I.Evaluate( 0.0f, DataType{}, 0.0f, 0.0f ) } -> std::convertible_to<DataType>;
		ConcreteInertializerType{ FComposableCameraPose{}, FComposableCameraPose{}, FComposableCameraPose{}, 0.f, 0.f };
	}
struct ComposableCameraInitializer
{
	ComposableCameraInitializer()
		: ConcreteInertializer{}
	{ }
	ComposableCameraInitializer(
		const FComposableCameraPose& LastSourceCameraPose,
		const FComposableCameraPose& ThisSourceCameraPose,
		const FComposableCameraPose& ThisTargetCameraPose,
		float BlendTime,
		float DeltaTime)
		: ConcreteInertializer(LastSourceCameraPose, ThisSourceCameraPose, ThisTargetCameraPose, BlendTime, DeltaTime)
	{ }

	DataType Evaluate(float TimeStamp, DataType TargetData)
	{
		return ConcreteInertializer.Evaluate(TimeStamp, TargetData);
	}

	DataType Evaluate(float TimeStamp, DataType TargetData, float BlendPct, UCurveFloat* Curve, float CurveWeight, float CurveShape)
	{
		auto [BlendCurveValue, BlendWeight] = GetBlendCurveValue(BlendPct, Curve, CurveWeight, CurveShape);
		return ConcreteInertializer.Evaluate(TimeStamp, TargetData, BlendCurveValue, BlendWeight);
	}

private:
	ConcreteInertializerType ConcreteInertializer;

	static std::pair<float, float> GetBlendCurveValue(float BlendPct, UCurveFloat* Curve, float CurveWeight, float CurveShape)
	{
		float CurveValue = Curve->GetFloatValue(BlendPct);
		float Weight;
		
		{
			if (BlendPct > 0.5f)
			{
				BlendPct = 1.0 -  BlendPct;
			}
			BlendPct *= 2;
			float BlendPoly = FMath::Pow(BlendPct, 4) * (35 - 84 * BlendPct + 70 * FMath::Pow(BlendPct, 2) - 20 * FMath::Pow(BlendPct, 3));
			Weight = CurveWeight * FMath::Loge(1 + BlendPoly * CurveShape) / FMath::Loge(1.0 + CurveShape); 
		}

		return { CurveValue, Weight };
	}
};

struct ComposableCameraPositionalInertializer
{
public:
	ComposableCameraPositionalInertializer() = default;
	ComposableCameraPositionalInertializer(const ComposableCameraPositionalInertializer&) = default;
	ComposableCameraPositionalInertializer(ComposableCameraPositionalInertializer&&) = default;
	ComposableCameraPositionalInertializer& operator=(const ComposableCameraPositionalInertializer&) = default;
	ComposableCameraPositionalInertializer& operator=(ComposableCameraPositionalInertializer&&) = default;
	
	ComposableCameraPositionalInertializer(
		const FComposableCameraPose& LastSourceCameraPose,
		const FComposableCameraPose& ThisSourceCameraPose,
		const FComposableCameraPose& ThisTargetCameraPose,
		float BlendTime,
		float DeltaTime)
	{
		FVector LastSourceCameraPosition = LastSourceCameraPose.Position;
		FVector ThisSourceCameraPosition = ThisSourceCameraPose.Position;
		FVector ThisTargetCameraPosition = ThisTargetCameraPose.Position;
		
		FVector InitialDirection = ThisSourceCameraPosition - ThisTargetCameraPosition;
		float InitialLength = InitialDirection.Length();
		InitialDirection.Normalize();

		FVector PreviousDirection = LastSourceCameraPosition - ThisTargetCameraPosition;
		float PreviousLength = PreviousDirection.Dot(InitialDirection);
		float InitialVelocity = (InitialLength - PreviousLength) / DeltaTime;

		_InitialLength = InitialLength;
		_InitialDirection = InitialDirection;
		
		float PositionInitialAcceleration = (-8.0 * InitialVelocity * BlendTime - 20.0 * InitialLength) / (BlendTime * BlendTime);
		float PositionA = -(PositionInitialAcceleration * BlendTime * BlendTime + 6.0 * InitialVelocity * BlendTime + 12.0 * InitialLength) / (2.0 * FMath::Pow(BlendTime, 5.0));
		float PositionB = (3.0 * PositionInitialAcceleration * BlendTime * BlendTime + 16.0 * InitialVelocity * BlendTime + 30.0 * InitialLength) / (2.0 * FMath::Pow(BlendTime, 4.0));
		float PositionC = -(3.0 * PositionInitialAcceleration * BlendTime * BlendTime + 12.0 * InitialVelocity * BlendTime + 20.0 * InitialLength) / (2.0 * FMath::Pow(BlendTime, 3.0));
		float PositionD = PositionInitialAcceleration / 2.0;
		float PositionE = InitialVelocity;
		float PositionF = InitialLength;
		Poly = ComposableCameraPolynomial<5, float>{PositionA, PositionB, PositionC, PositionD, PositionE, PositionF};
	}

	FVector Evaluate(float BlendDuration, FVector TargetLocation)
	{
		float NewLength = Poly.Evaluate(BlendDuration);
		return NewLength * _InitialDirection + TargetLocation;
	}

	FVector Evaluate(float BlendDuration, FVector TargetLocation, float BlendCurveValue, float BlendWeight)
	{
		float NewLength = Poly.Evaluate(BlendDuration);
		NewLength = (1.f - BlendWeight) * NewLength + BlendWeight * (BlendCurveValue * _InitialLength);
		return NewLength * _InitialDirection + TargetLocation;
	}

private:
	float _InitialLength;
	FVector _InitialDirection;
	ComposableCameraPolynomial<5, float> Poly;
};

struct ComposableCameraRotationalInertializer
{
public:
	ComposableCameraRotationalInertializer() = default;
	ComposableCameraRotationalInertializer(const ComposableCameraRotationalInertializer&) = default;
	ComposableCameraRotationalInertializer(ComposableCameraRotationalInertializer&&) = default;
	ComposableCameraRotationalInertializer& operator=(const ComposableCameraRotationalInertializer&) = default;
	ComposableCameraRotationalInertializer& operator=(ComposableCameraRotationalInertializer&&) = default;
	
	ComposableCameraRotationalInertializer(
		const FComposableCameraPose& LastSourceCameraPose,
		const FComposableCameraPose& ThisSourceCameraPose,
		const FComposableCameraPose& ThisTargetCameraPose,
		float BlendTime,
		float DeltaTime)
	{
		FRotator LastSourceCameraRotation = LastSourceCameraPose.Rotation;
		FRotator ThisSourceCameraRotation = ThisSourceCameraPose.Rotation;
		FRotator ThisTargetCameraRotation = ThisTargetCameraPose.Rotation;
		
		FVector VectorThisSourceCameraRotation = FVector(ThisSourceCameraRotation.Yaw, ThisSourceCameraRotation.Pitch, ThisSourceCameraRotation.Roll);
		FVector VectorLastSourceCameraRotation = FVector(LastSourceCameraRotation.Yaw, LastSourceCameraRotation.Pitch, LastSourceCameraRotation.Roll);
		FVector VectorThisTargetCameraRotation = FVector(ThisTargetCameraRotation.Yaw, ThisTargetCameraRotation.Pitch, ThisTargetCameraRotation.Roll);
		FVector InitialDirection = VectorThisSourceCameraRotation - VectorThisTargetCameraRotation;
		FVector InitialVelocity = VectorThisSourceCameraRotation - VectorLastSourceCameraRotation;
		
		InitialDirection = FVector(ComposableCameraSystem::NormalizeYaw(InitialDirection.X), InitialDirection.Y, InitialDirection.Z);
		InitialVelocity = FVector(ComposableCameraSystem::NormalizeYaw(InitialVelocity.X), InitialVelocity.Y, InitialVelocity.Z) / DeltaTime;

		_InitialDirection = InitialDirection;
		
		FVector RotInitialAcceleration = (-8.0 * InitialVelocity * BlendTime - 20.0 * InitialDirection) / (BlendTime * BlendTime);
		FVector RotA = -(RotInitialAcceleration * BlendTime * BlendTime + 6.0 * InitialVelocity * BlendTime + 12.0 * InitialDirection) / (2.0 * FMath::Pow(BlendTime, 5.0));
		FVector RotB = (3.0 * RotInitialAcceleration * BlendTime * BlendTime + 16.0 * InitialVelocity * BlendTime + 30.0 * InitialDirection) / (2.0 * FMath::Pow(BlendTime, 4.0));
		FVector RotC = -(3.0 * RotInitialAcceleration * BlendTime * BlendTime + 12.0 * InitialVelocity * BlendTime + 20.0 * InitialDirection) / (2.0 * FMath::Pow(BlendTime, 3.0));
		FVector RotD = RotInitialAcceleration / 2.0;
		FVector RotE = InitialVelocity;
		FVector RotF = InitialDirection;
		Poly = ComposableCameraPolynomial<5, FVector>{RotA, RotB, RotC, RotD, RotE, RotF};
	}

	FRotator Evaluate(float BlendDuration, FRotator TargetRotation)
	{
		FVector NewRot = Poly.Evaluate(BlendDuration) + FVector(TargetRotation.Yaw, TargetRotation.Pitch, TargetRotation.Roll);
		return FRotator(NewRot.Y, NewRot.X, NewRot.Z);
	}

	FRotator Evaluate(float BlendDuration, FRotator TargetRotation, float BlendCurveValue, float BlendWeight)
	{
		FVector NewRot = Poly.Evaluate(BlendDuration);
		NewRot = (1.f - BlendWeight) * NewRot + BlendWeight * (BlendCurveValue * _InitialDirection);
		NewRot += FVector(TargetRotation.Yaw, TargetRotation.Pitch, TargetRotation.Roll);
		return FRotator(NewRot.Y, NewRot.X, NewRot.Z);
	}

private:
	FVector _InitialDirection;
	ComposableCameraPolynomial<5, FVector> Poly;
};

/**
 * Inertialized transition.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraInertializedTransition : public UComposableCameraTransitionBase
{
	GENERATED_BODY()

public:
	virtual void OnBeginPlay_Implementation(float DeltaTime, const FComposableCameraPose& CurrentTargetPose) override;
	virtual FComposableCameraPose OnEvaluate_Implementation(float DeltaTime, const FComposableCameraPose& CurrentTargetPose) override;

public:
	// Whether to use automatic transition time. If true, will compute the transition time according to MaxAcceleration, else, will use TransitionTime.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bAutoTransitionTime { false };

	// Maximum acceleration during transition to determine the actual transition time.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bAutoTransitionTime == true"))
	float MaxAcceleration { 100.f };

	// Transition time if not using bAutoTransitionTime.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bAutoTransitionTime == false"))
	float BlendTime { 2.f };

	// Additive curve used to change the "shape" of transition. Must be normalized into [0,1] for both x-axis and y-axis and f(0)=1, f(1)=0. 
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UCurveFloat* AdditiveCurve;

	// Weight controlling the overall contribution of AdditiveCurve.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "AdditiveCurve != nullptr"))
	float AdditiveCurveWeight { 0.5f };

	// Factor controlling the contribution of AdditiveCurve to the transition shape.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "AdditiveCurve != nullptr"))
	float AdditiveCurveShape { 10.f };
	
private:
	ComposableCameraInitializer<FRotator, ComposableCameraRotationalInertializer> RotationalInertializer;
	ComposableCameraInitializer<FVector, ComposableCameraPositionalInertializer> PositionalInertializer;

	UComposableCameraSmoothTransition* BackupSmoothTransition;
	
	float GetActualBlendTime(float DeltaTime, const FComposableCameraPose& LastSourceCameraPose, const FComposableCameraPose& ThisSourceCameraPose, const FComposableCameraPose& CurrentTargetPose);
};
