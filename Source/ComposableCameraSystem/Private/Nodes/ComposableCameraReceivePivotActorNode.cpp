// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraReceivePivotActorNode.h"

#include "Cameras/ComposableCameraCameraBase.h"

void UComposableCameraReceivePivotActorNode::OnInitialize_Implementation()
{
	Super::OnInitialize_Implementation();

	if (bUseBoneForPivot)
	{
		AActor* InPivotActor = GetInputPinValue<AActor*>("PivotActor");
		if (IsValid(InPivotActor))
		{
			SkeletalMeshComponentForPivotActor = InPivotActor->GetComponentByClass<USkeletalMeshComponent>();
		}
	}
}

void UComposableCameraReceivePivotActorNode::OnTickNode_Implementation(
	float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose,
	FComposableCameraPose& OutCameraPose)
{
	// PivotActor and bUseBoneForPivot are pin-matched UPROPERTYs — already resolved
	// by the base TickNode prologue. Read the member directly.
	AActor* InPivotActor = PivotActor.Get();
	FVector OutPivotPosition = FVector::ZeroVector;

	// Use IsValid() for Actor pointers — a destroyed actor may leave a dangling
	// pointer even via TObjectPtr for non-UPROPERTY copies.
	if (bUseBoneForPivot && IsValid(SkeletalMeshComponentForPivotActor))
	{
		OutPivotPosition = SkeletalMeshComponentForPivotActor->GetSocketLocation(BoneName);
	}
	else if (IsValid(InPivotActor))
	{
		OutPivotPosition = InPivotActor->GetActorLocation();
	}

	SetOutputPinValue<FVector>("PivotPosition", OutPivotPosition);
	SetOutputPinValue<AActor*>("PivotActor_Out", IsValid(InPivotActor) ? InPivotActor : nullptr);
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
		Pin.DefaultValueString = FString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "PivotActorTooltip",
			"The actor whose position is used as the camera pivot point.");
		OutPins.Add(Pin);
	}

	// Input: toggle bone-based pivot resolution.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "bUseBoneForPivot";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "UseBoneForPivot", "Use Bone For Pivot");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Bool;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = bUseBoneForPivot ? TEXT("true") : TEXT("false");
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "UseBoneForPivotTooltip",
			"When true, use the named bone on the pivot actor's skeletal mesh as the pivot position.");
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
