// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraComputeRandomOffsetNode.h"

#include "ComposableCameraSystemModule.h"

void UComposableCameraComputeRandomOffsetNode::ExecuteBeginPlay()
{
	FVector Min = GetInputPinValue<FVector>("MinOffset");
	FVector Max = GetInputPinValue<FVector>("MaxOffset");

	// Fall back to UPROPERTY defaults if pins aren't connected.
	if (Min.IsZero() && Max.IsZero())
	{
		Min = MinOffset;
		Max = MaxOffset;
	}

	const FVector RandomOffset(
		FMath::FRandRange(Min.X, Max.X),
		FMath::FRandRange(Min.Y, Max.Y),
		FMath::FRandRange(Min.Z, Max.Z)
	);

	SetOutputPinValue<FVector>("RandomOffset", RandomOffset);

	UE_LOG(LogComposableCameraSystem, Verbose,
		TEXT("ComputeRandomOffset: generated (%s) from min (%s) max (%s)"),
		*RandomOffset.ToString(), *Min.ToString(), *Max.ToString());
}

void UComposableCameraComputeRandomOffsetNode::GetPinDeclarations_Implementation(
	TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	// Input: minimum bound.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "MinOffset";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "MinOffset", "Min Offset");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Vector3D;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "MinOffsetTooltip",
			"Minimum bound for each axis of the random offset.");
		OutPins.Add(Pin);
	}

	// Input: maximum bound.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "MaxOffset";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "MaxOffset", "Max Offset");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Vector3D;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "MaxOffsetTooltip",
			"Maximum bound for each axis of the random offset.");
		OutPins.Add(Pin);
	}

	// Output: the generated random offset.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "RandomOffset";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "RandomOffset", "Random Offset");
		Pin.Direction = EComposableCameraPinDirection::Output;
		Pin.PinType = EComposableCameraPinType::Vector3D;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "RandomOffsetTooltip",
			"The generated random offset vector, stable for the camera's lifetime.");
		OutPins.Add(Pin);
	}
}
