// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraReceivePivotActorNode.h"

#include "Cameras/ComposableCameraCameraBase.h"

void UComposableCameraReceivePivotActorNode::OnInitialize_Implementation()
{
	Super::OnInitialize_Implementation();

	if (bUseBoneForPivot)
	{
		AActor* PivotActor = GetInputPinValue<AActor*>("PivotActor");
		if (IsValid(PivotActor))
		{
			SkeletalMeshComponentForPivotActor = PivotActor->GetComponentByClass<USkeletalMeshComponent>();
		}
	}
}

void UComposableCameraReceivePivotActorNode::OnTickNode_Implementation(
	float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose,
	FComposableCameraPose& OutCameraPose)
{
	AActor* PivotActor = GetInputPinValue<AActor*>("PivotActor");
	FVector PivotPosition = FVector::ZeroVector;

	// Use IsValid() for Actor pointers from the RuntimeDataBlock — they are
	// stored as type-erased bytes invisible to GC. A destroyed actor leaves
	// a dangling pointer, not null.
	if (bUseBoneForPivot && IsValid(SkeletalMeshComponentForPivotActor))
	{
		PivotPosition = SkeletalMeshComponentForPivotActor->GetSocketLocation(BoneName);
	}
	else if (IsValid(PivotActor))
	{
		PivotPosition = PivotActor->GetActorLocation();
	}

	SetOutputPinValue<FVector>("PivotPosition", PivotPosition);
	SetOutputPinValue<AActor*>("PivotActor_Out", IsValid(PivotActor) ? PivotActor : nullptr);
}

void UComposableCameraReceivePivotActorNode::GetPinDeclarations_Implementation(
	TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	// Input: the actor to use as pivot.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "PivotActor";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "PivotActor", "Pivot Actor");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Actor;
		Pin.bRequired = true;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "PivotActorTooltip",
			"The actor whose position is used as the camera pivot point.");
		OutPins.Add(Pin);
	}

	// Output: the computed pivot position.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "PivotPosition";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "PivotPosition", "Pivot Position");
		Pin.Direction = EComposableCameraPinDirection::Output;
		Pin.PinType = EComposableCameraPinType::Vector3D;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "PivotPositionTooltip",
			"The world-space position of the pivot actor (or bone if configured).");
		OutPins.Add(Pin);
	}

	// Output: pass-through the pivot actor for downstream nodes.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "PivotActor_Out";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "PivotActorOut", "Pivot Actor");
		Pin.Direction = EComposableCameraPinDirection::Output;
		Pin.PinType = EComposableCameraPinType::Actor;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "PivotActorOutTooltip",
			"Pass-through of the pivot actor for downstream nodes.");
		OutPins.Add(Pin);
	}
}
