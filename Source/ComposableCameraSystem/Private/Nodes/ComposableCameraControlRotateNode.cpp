// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraControlRotateNode.h"

#include "ComposableCameraSystemModule.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Math/ComposableCameraMath.h"

void UComposableCameraControlRotateNode::OnInitialize_Implementation()
{
	Super::OnInitialize_Implementation();

	LastFrameCameraRotationInput = FVector2D::ZeroVector;

	// Do NOT resolve RotationInputActor / RotateAction here — input pin values
	// are not yet applied at OnInitialize time (see UComposableCameraCameraNodeBase::OnInitialize
	// note). The binding is registered lazily in EnsureInputBinding(), called from OnTickNode.
	CachedInputComponent.Reset();
	LastBoundInputActor.Reset();
	LastBoundAction.Reset();
}

void UComposableCameraControlRotateNode::EnsureInputBinding(AActor* EffectiveRotationInputActor)
{
	const bool bActorChanged  = LastBoundInputActor.Get() != EffectiveRotationInputActor;
	const bool bActionChanged = LastBoundAction.Get()     != RotateAction;
	const bool bComponentDead = !CachedInputComponent.IsValid();

	if (!bActorChanged && !bActionChanged && !bComponentDead)
	{
		return;
	}

	CachedInputComponent.Reset();
	LastBoundInputActor = EffectiveRotationInputActor;
	LastBoundAction     = RotateAction;

	if (!IsValid(EffectiveRotationInputActor) || !IsValid(RotateAction))
	{
		return;
	}

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(EffectiveRotationInputActor->InputComponent);
	if (!EIC)
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT(
			"ControlRotate: actor '%s' has no UEnhancedInputComponent."), *EffectiveRotationInputActor->GetName());
		return;
	}

	CachedInputComponent = EIC;

	// Register the binding so the EnhancedInput system tracks the action value
	// for this component. The returned reference is intentionally discarded —
	// the underlying TArray<FEnhancedInputActionValueBinding> can reallocate
	// when other code calls BindActionValue, which would dangle any cached
	// pointer. Tick reads the live value via GetBoundActionValue (linear
	// search, safe across reallocations).
	EIC->BindActionValue(RotateAction);
}

void UComposableCameraControlRotateNode::OnTickNode_Implementation(
	float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	AActor* EffectiveRotationInputActor = ComposableCameraSystem::ResolveActorInput(
		RotationInputActorSource, RotationInputActor.Get(), GetOwningPlayerCameraManager());
	EnsureInputBinding(EffectiveRotationInputActor);

	FVector2D CameraRotationInputForThisFrame {};

	if (UEnhancedInputComponent* EIC = CachedInputComponent.Get();
		EIC && IsValid(RotateAction))
	{
		CameraRotationInputForThisFrame = EIC->GetBoundActionValue(RotateAction).Get<FVector2D>();

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
		PinDecl.PinName = TEXT("RotationInputActorSource");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraControlRotateNode", "RotationInputActorSource", "Rotation Input Actor Source");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Enum;
		PinDecl.EnumType = StaticEnum<EComposableCameraActorInputSource>();
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = false;
		PinDecl.DefaultValueString = PinDecl.EnumType ? PinDecl.EnumType->GetNameStringByValue(static_cast<int64>(RotationInputActorSource)) : FString();
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraControlRotateNode", "RotationInputActorSourceTip", "Selects whether rotation input comes from the controller's controlled pawn or an explicit actor.");
		OutPins.Add(PinDecl);
	}

	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("RotationInputActor");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraControlRotateNode", "RotationInputActor", "Rotation Input Actor");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Actor;
		PinDecl.bRequired = false;
		PinDecl.DefaultValueString = FString();
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraControlRotateNode", "RotationInputActorTip", "Actor providing EnhancedInputComponent for camera rotation input.");
		OutPins.Add(PinDecl);
	}

	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("HorizontalSpeed");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraControlRotateNode", "HorizontalSpeed", "Horizontal Speed");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Float;
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = false;
		PinDecl.DefaultValueString = FString::SanitizeFloat(HorizontalSpeed);
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraControlRotateNode", "HorizontalSpeedTip", "Yaw input speed multiplier.");
		OutPins.Add(PinDecl);
	}

	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("VerticalSpeed");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraControlRotateNode", "VerticalSpeed", "Vertical Speed");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Float;
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = false;
		PinDecl.DefaultValueString = FString::SanitizeFloat(VerticalSpeed);
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraControlRotateNode", "VerticalSpeedTip", "Pitch input speed multiplier.");
		OutPins.Add(PinDecl);
	}

	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("HorizontalDamping");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraControlRotateNode", "HorizontalDamping", "Horizontal Damping");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Vector2D;
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = false;
		PinDecl.DefaultValueString = HorizontalDamping.ToString();
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraControlRotateNode", "HorizontalDampingTip",
			"Accel (X) / decel (Y) time for yaw input in seconds.");
		OutPins.Add(PinDecl);
	}

	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("VerticalDamping");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraControlRotateNode", "VerticalDamping", "Vertical Damping");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Vector2D;
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = false;
		PinDecl.DefaultValueString = VerticalDamping.ToString();
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraControlRotateNode", "VerticalDampingTip",
			"Accel (X) / decel (Y) time for pitch input in seconds.");
		OutPins.Add(PinDecl);
	}

	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("bInvertPitch");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraControlRotateNode", "bInvertPitch", "Invert Pitch");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Bool;
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = false;
		PinDecl.DefaultValueString = bInvertPitch ? TEXT("true") : TEXT("false");
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraControlRotateNode", "bInvertPitchTip", "Invert the pitch axis direction.");
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


void UComposableCameraControlRotateNode::ApplyAcceleration(float DeltaTime, const FVector2D& Damping, double& ThisFrameRotationInput,
                                                           const double& LastFrameRotationInput)
{
	const double DampTime = FMath::Abs(ThisFrameRotationInput) > FMath::Abs(LastFrameRotationInput)
			? Damping.X
			: Damping.Y;

	double Increment = ComposableCameraSystem::SimpleExpDamp(DeltaTime, static_cast<float>(DampTime), ThisFrameRotationInput - LastFrameRotationInput);
	ThisFrameRotationInput = LastFrameRotationInput + Increment;
}
