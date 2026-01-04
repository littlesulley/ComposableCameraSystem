// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraControlRotateNode.h"

#include "ComposableCameraSystemModule.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Core/ComposableCameraPlayerCamaraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Math/ComposableCameraMath.h"

void UComposableCameraControlRotateNode::OnBeginPlayNode_Implementation(const FComposableCameraPose& CurrentCameraPose)
{
	LastFrameCameraRotationInput = FVector2D::ZeroVector;

	if (ContextRotationInputActor.Variable && ContextRotationInputActor.Variable->RuntimeValue)
	{
		InputComponent = Cast<UEnhancedInputComponent>(ContextRotationInputActor.Variable->RuntimeValue->InputComponent);
	}
	else if (ContextRotationInputActor.Value)
	{
		InputComponent = Cast<UEnhancedInputComponent>(ContextRotationInputActor.Value->InputComponent);
	}

	if (InputComponent)
	{
		InputBinding = &InputComponent->BindActionValue(RotateAction);
	}
	else
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT(
			"Cannot find an input component of type UEnhancedInputComponent in ControlRotate node."));
	}
}

void UComposableCameraControlRotateNode::OnTickNode_Implementation(
	float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	FVector2D CameraRotationInputForThisFrame {};

	// Read camera rotation input from IA.
	if (InputBinding)
	{
		CameraRotationInputForThisFrame = InputBinding->GetValue().Get<FVector2D>();

		if (bInvertPitch)
		{
			CameraRotationInputForThisFrame.Y = -CameraRotationInputForThisFrame.Y;
		}
	}

	// Apply speed.
	CameraRotationInputForThisFrame.X = CameraRotationInputForThisFrame.X * HorizontalSpeed;
	CameraRotationInputForThisFrame.Y = CameraRotationInputForThisFrame.Y * VerticalSpeed;

	// Apply acceleration and deceleration.
	ApplyAcceleration(DeltaTime, HorizontalDamping, CameraRotationInputForThisFrame.X, LastFrameCameraRotationInput.X);
	ApplyAcceleration(DeltaTime, VerticalDamping, CameraRotationInputForThisFrame.Y, LastFrameCameraRotationInput.Y);

	// Write into OutCameraPose
	FQuat CurrentCameraRotation = CurrentCameraPose.Rotation.Quaternion();
	FQuat LocalRotationPitch = FRotator(CameraRotationInputForThisFrame.Y, 0, 0).Quaternion();
	FQuat WorldRotationYaw = FRotator(0,  CameraRotationInputForThisFrame.X, 0).Quaternion();
	
	FQuat NewCameraRotation = WorldRotationYaw * CurrentCameraRotation * LocalRotationPitch;
	OutCameraPose.Rotation = NewCameraRotation.GetNormalized().Rotator();
	
	// Write into context.
	if (ContextCameraRotationInput.Variable)
	{
		ContextCameraRotationInput.Variable->RuntimeValue = CameraRotationInputForThisFrame;
	}
	else
	{
		ContextCameraRotationInput.Value = CameraRotationInputForThisFrame;	
	}
	
	LastFrameCameraRotationInput = CameraRotationInputForThisFrame;
}

void UComposableCameraControlRotateNode::ReceiveInitializerNode(UComposableCameraCameraNodeBase* Initializer)
{
	if (UComposableCameraControlRotateNode* CastedInitializer = Cast<UComposableCameraControlRotateNode>(Initializer))
	{
		RotateAction = CastedInitializer->RotateAction;
		HorizontalSpeed = CastedInitializer->HorizontalSpeed;
		VerticalSpeed = CastedInitializer->VerticalSpeed;
		HorizontalDamping = CastedInitializer->HorizontalDamping;
		VerticalDamping = CastedInitializer->VerticalDamping;
		bInvertPitch = CastedInitializer->bInvertPitch;
	}
}

void UComposableCameraControlRotateNode::ApplyAcceleration(float DeltaTime, FVector2f Damping, double& ThisFrameRotationInput,
                                                           const double& LastFrameRotationInput)
{
	float DampTime = FMath::Abs(ThisFrameRotationInput) > FMath::Abs(LastFrameRotationInput)
			? Damping.X
			: Damping.Y;

	double Increment = ComposableCameraSystem::SimpleExpDamp(DeltaTime, DampTime, ThisFrameRotationInput - LastFrameRotationInput);
	ThisFrameRotationInput = LastFrameRotationInput + Increment;
}
