// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actions/ComposableCameraActionBase.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "Transitions/ComposableCameraTransitionBase.h"
#include "ComposableCameraTestObjects.generated.h"

class AComposableCameraPlayerCameraManager;

/**
 * A controllable transition for testing. Allows tests to manually set finished state
 * and control the blend output.
 */
UCLASS(Hidden, MinimalAPI)
class UComposableCameraTestTransition : public UComposableCameraTransitionBase
{
	GENERATED_BODY()

public:
	/** Manually mark the transition as finished. */
	void SetFinished(bool bInFinished)
	{
		bFinished = bInFinished;
	}

	AComposableCameraPlayerCameraManager* GetCachedPlayerCameraManagerForTest() const
	{
		return GetOwningPlayerCameraManager();
	}

	/** The blend factor returned by this transition (0 = full source, 1 = full target). */
	float BlendFactor { 0.5f };

protected:
	virtual FComposableCameraPose OnEvaluate_Implementation(
		float DeltaTime,
		const FComposableCameraPose& CurrentSourcePose,
		const FComposableCameraPose& CurrentTargetPose) override
	{
		// Simple linear interpolation for predictable test output. Delegating to BlendBy
		// ensures we cover all pose fields (FOV, physical, projection) and respect the
		// "resolve FOV before blending" invariant.
		FComposableCameraPose Result = BlendPosesByLockedRotationPath(
			CurrentSourcePose,
			CurrentTargetPose,
			BlendFactor);
		return Result;
	}
};

/** Reflection fixture for Action asset Actor parameters. */
UCLASS(Hidden, MinimalAPI)
class UComposableCameraActionAssetTestAction : public UComposableCameraActionBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	TObjectPtr<AActor> TargetActor;
};

/** Authored class defaults for the class-based Action duration regression. */
UCLASS(Hidden, MinimalAPI)
class UComposableCameraTestZeroDurationAction : public UComposableCameraActionBase
{
	GENERATED_BODY()

public:
	UComposableCameraTestZeroDurationAction() { Duration = 0.f; }
};

UCLASS(Hidden, MinimalAPI)
class UComposableCameraTestNegativeDurationAction : public UComposableCameraActionBase
{
	GENERATED_BODY()

public:
	UComposableCameraTestNegativeDurationAction() { Duration = -1.f; }
};

UCLASS(Hidden, MinimalAPI)
class UComposableCameraTestConditionOnlyZeroDurationAction : public UComposableCameraActionBase
{
	GENERATED_BODY()

public:
	UComposableCameraTestConditionOnlyZeroDurationAction()
	{
		ExpirationType = static_cast<uint8>(EComposableCameraActionExpirationType::Condition);
		Duration = 0.f;
	}
};

/**
 * Reflected node used only by Modifier automation tests.
 *
 * It exposes every continuously blendable built-in type, one discrete type,
 * and one supported non-pin property. This keeps the transition matrix focused
 * on Modifier behavior instead of coupling each row to an unrelated production
 * node's tick logic.
 */
UCLASS(Hidden, MinimalAPI)
class UComposableCameraModifierTestNode : public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	virtual void GetPinDeclarations_Implementation(
		TArray<FComposableCameraNodePinDeclaration>& OutPins) const override
	{
		auto AddInputPin = [&OutPins](
			FName PinName,
			EComposableCameraPinType PinType)
		{
			FComposableCameraNodePinDeclaration& Pin =
				OutPins.AddDefaulted_GetRef();
			Pin.PinName = PinName;
			Pin.DisplayName = FText::FromName(PinName);
			Pin.Direction = EComposableCameraPinDirection::Input;
			Pin.PinType = PinType;
			Pin.bRequired = false;
		};

		AddInputPin(
			GET_MEMBER_NAME_CHECKED(
				UComposableCameraModifierTestNode, FloatValue),
			EComposableCameraPinType::Float);
		AddInputPin(
			GET_MEMBER_NAME_CHECKED(
				UComposableCameraModifierTestNode, DoubleValue),
			EComposableCameraPinType::Double);
		AddInputPin(
			GET_MEMBER_NAME_CHECKED(
				UComposableCameraModifierTestNode, Vector2DValue),
			EComposableCameraPinType::Vector2D);
		AddInputPin(
			GET_MEMBER_NAME_CHECKED(
				UComposableCameraModifierTestNode, Vector3DValue),
			EComposableCameraPinType::Vector3D);
		AddInputPin(
			GET_MEMBER_NAME_CHECKED(
				UComposableCameraModifierTestNode, Vector4Value),
			EComposableCameraPinType::Vector4);
		AddInputPin(
			GET_MEMBER_NAME_CHECKED(
				UComposableCameraModifierTestNode, RotatorValue),
			EComposableCameraPinType::Rotator);
		AddInputPin(
			GET_MEMBER_NAME_CHECKED(
				UComposableCameraModifierTestNode, TransformValue),
			EComposableCameraPinType::Transform);
		AddInputPin(
			GET_MEMBER_NAME_CHECKED(
				UComposableCameraModifierTestNode, DiscreteValue),
			EComposableCameraPinType::Bool);
	}

	virtual bool SupportsInPlaceModifierProperty(
		FName PropertyName) const override
	{
		return PropertyName == GET_MEMBER_NAME_CHECKED(
				UComposableCameraModifierTestNode, LinearColorValue)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(
				UComposableCameraModifierTestNode, NonPinFloatValue);
	}

	bool WasModifierPropertyNotified(FName PropertyName) const
	{
		return NotifiedProperties.Contains(PropertyName);
	}

	void ResetModifierNotifications()
	{
		NotifiedProperties.Reset();
	}

	UPROPERTY(EditAnywhere, Category = "Test")
	float FloatValue = 0.f;

	UPROPERTY(EditAnywhere, Category = "Test")
	double DoubleValue = 0.0;

	UPROPERTY(EditAnywhere, Category = "Test")
	FVector2D Vector2DValue = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Test")
	FVector Vector3DValue = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Test")
	FVector4 Vector4Value = FVector4(0.f, 0.f, 0.f, 0.f);

	UPROPERTY(EditAnywhere, Category = "Test")
	FRotator RotatorValue = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, Category = "Test")
	FTransform TransformValue = FTransform::Identity;

	/** LinearColor has no dedicated pin type; exercise the non-pin opt-in path. */
	UPROPERTY(EditAnywhere, Category = "Test")
	FLinearColor LinearColorValue = FLinearColor::Transparent;

	/** Extra continuous value for explicit non-pin notification assertions. */
	UPROPERTY(EditAnywhere, Category = "Test")
	float NonPinFloatValue = 0.f;

	UPROPERTY(EditAnywhere, Category = "Test")
	bool DiscreteValue = false;

protected:
	virtual void OnModifierPropertyChanged(FName PropertyName) override
	{
		NotifiedProperties.Add(PropertyName);
	}

private:
	TSet<FName> NotifiedProperties;
};
