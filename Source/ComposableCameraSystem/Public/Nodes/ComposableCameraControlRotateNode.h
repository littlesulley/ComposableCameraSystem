// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Utils/ComposableCameraActorInputSource.h"
#include "ComposableCameraCameraNodeBase.h"
#include "ComposableCameraControlRotateNode.generated.h"

class UEnhancedInputComponent;
class UInputAction;

/**
 * Node for receiving user input and applying it to camera rotation. \n
 * @InputParameter RotateAction: Input action controlling camera rotation. You must use the Enhanced Input Component. \n
 * @InputParameter HorizontalSpeed: Camera horizontal rotation speed. \n
 * @InputParameter VerticalSpeed: Camera vertical rotation speed. \n
 * @InputParameter HorizontalDamping: Acceleration and deceleration time when changing yaw. First element is acceleration, second is deceleration. \n
 * @InputParameter VerticalDamping: Acceleration and deceleration time when changing pitch. First element is acceleration, second is deceleration. \n
 * @InputParameter InvertPitch: Whether to invert pitch. \n 
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (ToolTip = "Applies player input to control camera rotation with configurable speeds and damping."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraControlRotateNode : public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	UComposableCameraControlRotateNode() { PaletteCategory = TEXT("Rotation"); }

public:
	virtual void OnInitialize_Implementation() override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

protected:

public:
	// Selects whether camera rotation input is read from the controller's
	// controlled pawn or from RotationInputActor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraActorInputSource RotationInputActorSource { EComposableCameraActorInputSource::ExplicitActor };

	// Actor providing the EnhancedInputComponent for camera rotation input.
	// Typically driven at runtime via a context parameter (e.g. the player pawn),
	// but kept as a UPROPERTY so the Details panel renders a proper object picker
	// and an authored default is available when unwired.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "RotationInputActorSource == EComposableCameraActorInputSource::ExplicitActor", EditConditionHides))
	TObjectPtr<AActor> RotationInputActor;

	// Input action controlling camera rotation. You must use the Enhanced Input Component.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	TObjectPtr<class UInputAction> RotateAction;

	// Camera horizontal rotation speed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	float HorizontalSpeed { 1.f };

	// Camera vertical rotation speed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	float VerticalSpeed { 1.f };

	// Acceleration and deceleration time when changing yaw. First element is acceleration, second is deceleration.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	FVector2D HorizontalDamping { 0.5, 0.5 };

	// Acceleration and deceleration time when changing pitch. First element is acceleration, second is deceleration.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	FVector2D VerticalDamping { 0.5, 0.5 };

	// Whether to invert pitch.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	bool bInvertPitch { true };

private:
	void ApplyAcceleration(float DeltaTime, const FVector2D& Damping, double& ThisFrameRotationInput, const double& LastFrameRotationInput);

	// Make sure the (pin-driven) RotationInputActor's EnhancedInputComponent
	// has a value-binding registered for RotateAction. Called from OnTickNode
	// because RotationInputActor is an input pin and is auto-resolved per frame -	// resolving once in OnInitialize would (a) read the pre-pin-resolution
	// default value and (b) leave us bound to a stale actor if the pin is
	// later wired to a different one or the original actor is destroyed.
	//
	// Does NOT cache the FEnhancedInputActionValueBinding* returned by
	// BindActionValue. That pointer aliases an entry inside
	// UEnhancedInputComponent::EnhancedActionValueBindings. A
	// TArray<FEnhancedInputActionValueBinding> stored by-value, so any other
	// BindActionValue call on the same component (BP node, gameplay code,
	// another camera node sharing the actor) can reallocate the array and
	// dangle our pointer with no signal. Read the value at tick time via
	// EIC->GetBoundActionValue(RotateAction) instead. It does a linear
	// search that is safe across reallocations and cheap for the handful of
	// bindings a typical InputComponent holds.
	void EnsureInputBinding(AActor* EffectiveRotationInputActor);

	// Weak refs let us detect actor / component destruction across frames
	// without keeping the input owner alive ourselves, and let us notice
	// pin-driven actor / action swaps so we can rebind on the new owner.
	TWeakObjectPtr<UEnhancedInputComponent> CachedInputComponent;
	TWeakObjectPtr<AActor> LastBoundInputActor;
	TWeakObjectPtr<UInputAction> LastBoundAction;

	FVector2D LastFrameCameraRotationInput;
};
