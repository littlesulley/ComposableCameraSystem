// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraControlRotateNode.h"

#include "ComposableCameraSystemModule.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Math/ComposableCameraMath.h"

void UComposableCameraControlRotateNode::OnInitialize_Implementation()
{
	Super::OnInitialize_Implementation();

	LastFrameCameraRotationInput = FVector2D::ZeroVector;

	AActor* RotationInputActor = GetInputPinValue<AActor*>("RotationInputActor");
	if (IsValid(RotationInputActor))
	{
		InputComponent = Cast<UEnhancedInputComponent>(RotationInputActor->InputComponent);
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
	
	// Write rotation input to output pin for downstream nodes.
	SetOutputPinValue<FVector2D>("CameraRotationInput", CameraRotationInputForThisFrame);
	
	LastFrameCameraRotationInput = CameraRotationInputForThisFrame;
}

void UComposableCameraControlRotateNode::GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("RotationInputActor");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraControlRotateNode", "RotationInputActor", "Rotation Input Actor");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Actor;
		PinDecl.bRequired = false;
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraControlRotateNode", "RotationInputActorTip", "Actor providing EnhancedInputComponent for camera rotation input.");
		OutPins.Add(PinDecl);
	}

	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("CameraRotationInput");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraControlRotateNode", "CameraRotationInput", "Camera Rotation Input");
		PinDecl.Direction = EComposableCameraPinDirection::Output;
		PinDecl.PinType = EComposableCameraPinType::Vector2D;
		PinDecl.bRequired = false;
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraControlRotateNode", "CameraRotationInputTip", "Rotation input result for this frame.");
		OutPins.Add(PinDecl);
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
