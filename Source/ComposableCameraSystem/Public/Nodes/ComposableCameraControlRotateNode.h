// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "ComposableCameraControlRotateNode.generated.h"

struct FEnhancedInputActionValueBinding;

/**
 * Node for receiving user input and applying it to camera rotation. \n
 * @InputParameter RotateAction: Input action controlling camera rotation. You must use the Enhanced Input Component. \n
 * @InputParameter HorizontalSpeed: Camera horizontal rotation speed. \n
 * @InputParameter VerticalSpeed: Camera vertical rotation speed. \n
 * @InputParameter HorizontalDamping: Acceleration and deceleration time when changing yaw. First element is acceleration, second is deceleration. \n
 * @InputParameter VerticalDamping: Acceleration and deceleration time when changing pitch. First element is acceleration, second is deceleration. \n
 * @InputParameter InvertPitch: Whether to invert pitch. \n 
 * @ContextParameter ContextCameraRotationInput: Camera rotation input for this frame (not the final rotation). This is where the result camera rotation input will be written to. \n
 * @ContextParameter RotationInputActor: The actor where you retrieve the InputComponent used to read input.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraControlRotateNode : public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	virtual void OnBeginPlayNode_Implementation(const FComposableCameraPose& CurrentCameraPose) override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;

public:
	// Input action controlling camera rotation. You must use the Enhanced Input Component.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = InputParameters)
	class UInputAction* RotateAction;

	// Camera horizontal rotation speed.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = InputParameters)
	float HorizontalSpeed { 1.f };

	// Camera vertical rotation speed.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = InputParameters)
	float VerticalSpeed { 1.f };

	// Acceleration and deceleration time when changing yaw. First element is acceleration, second is deceleration.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = InputParameters)
	FVector2f HorizontalDamping { 1.f };
	
	// Acceleration and deceleration time when changing pitch. First element is acceleration, second is deceleration.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = InputParameters)
	FVector2f VerticalDamping { 1.f };

	// Whether to invert pitch.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = InputParameters)
	bool bInvertPitch { false };

	// Camera rotation input for this frame (not the final rotation). This is where the result camera rotation input will be written to.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = ContextParameters)
	FVector2dComposableCameraContextParameter ContextCameraRotationInput;

	// The actor where you retrieve the InputComponent used to read input.
	UPROPERTY(EditDefaultsOnly, Category = ContextParameters)
	FActorComposableCameraContextParameter ContextRotationInputActor;

private:
	void ApplyAcceleration(float DeltaTime, FVector2f Damping, double& ThisFrameRotationInput, const double& LastFrameRotationInput);
	
	UEnhancedInputComponent* InputComponent;
	FEnhancedInputActionValueBinding* InputBinding;
	FVector2D LastFrameCameraRotationInput;
};


