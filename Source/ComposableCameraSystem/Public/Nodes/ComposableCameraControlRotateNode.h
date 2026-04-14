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
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (ToolTip = "Applies player input to control camera rotation with configurable speeds and damping."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraControlRotateNode : public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	virtual void OnInitialize_Implementation() override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

protected:

public:
	// Input action controlling camera rotation. You must use the Enhanced Input Component.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	class UInputAction* RotateAction;

	// Camera horizontal rotation speed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	float HorizontalSpeed { 1.f };

	// Camera vertical rotation speed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	float VerticalSpeed { 1.f };

	// Acceleration and deceleration time when changing yaw. First element is acceleration, second is deceleration.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	FVector2f HorizontalDamping { .5f };
	
	// Acceleration and deceleration time when changing pitch. First element is acceleration, second is deceleration.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	FVector2f VerticalDamping { .5f };

	// Whether to invert pitch.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	bool bInvertPitch { true };

private:
	void ApplyAcceleration(float DeltaTime, FVector2f Damping, double& ThisFrameRotationInput, const double& LastFrameRotationInput);
	
	UEnhancedInputComponent* InputComponent;
	FEnhancedInputActionValueBinding* InputBinding;
	FVector2D LastFrameCameraRotationInput;
};


