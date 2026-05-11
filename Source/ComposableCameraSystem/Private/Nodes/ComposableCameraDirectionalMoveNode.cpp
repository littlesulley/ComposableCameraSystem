// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraDirectionalMoveNode.h"

void UComposableCameraDirectionalMoveNode::OnInitialize_Implementation()
{
	Super::OnInitialize_Implementation();

	ElapsedTime = 0.f;
}

void UComposableCameraDirectionalMoveNode::OnTickNode_Implementation(
	float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose,
	FComposableCameraPose& OutCameraPose)
{
	ElapsedTime += DeltaTime;

	const FVector LocalDirection = Direction.GetSafeNormal();
	const FVector WorldDirection = InitialTransform.TransformVectorNoScale(LocalDirection).GetSafeNormal();

	OutCameraPose.Position = InitialTransform.GetLocation() + WorldDirection * Speed * ElapsedTime;
	OutCameraPose.Rotation = InitialTransform.GetRotation().Rotator();
}

void UComposableCameraDirectionalMoveNode::GetPinDeclarations_Implementation(
	TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("Direction");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "DirectionalMove_Direction", "Direction");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Vector3D;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = Direction.ToString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "DirectionalMove_Direction_Tip",
			"Direction in camera-local space. X=forward, Y=right, Z=up. The value is normalized at runtime.");
		OutPins.Add(Pin);
	}

	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("InitialTransform");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "DirectionalMove_InitialTransform", "Initial Transform");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Transform;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "DirectionalMove_InitialTransform_Tip",
			"Starting transform. Its rotation converts Direction from camera space to world space.");
		OutPins.Add(Pin);
	}

	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("Speed");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "DirectionalMove_Speed", "Speed");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = FString::SanitizeFloat(Speed);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "DirectionalMove_Speed_Tip",
			"Movement speed in centimeters per second.");
		OutPins.Add(Pin);
	}
}
