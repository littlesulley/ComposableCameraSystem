// Copyright 2026 Sulley. All Rights Reserved.

#include "Nodes/ComposableCameraComputeDistanceToActorNode.h"

#include "ComposableCameraSystemModule.h"

void UComposableCameraComputeDistanceToActorNode::ExecuteBeginPlay()
{
	AActor* ActorA = GetInputPinValue<AActor*>("ActorA");
	AActor* ActorB = GetInputPinValue<AActor*>("ActorB");

	float Distance = 0.f;
	FVector Direction = FVector::ForwardVector;

	// Use IsValid() instead of raw null check. Actor pointers read from the
	// RuntimeDataBlock are type-erased bytes invisible to GC. A destroyed
	// actor leaves a dangling pointer (not null), so (Actor != nullptr) would
	// pass and dereference garbage. IsValid() checks the weak object table.
	if (IsValid(ActorA) && IsValid(ActorB))
	{
		const FVector LocA = ActorA->GetActorLocation();
		const FVector LocB = ActorB->GetActorLocation();
		const FVector Delta = LocB - LocA;

		Distance = Delta.Size();
		Direction = Distance > UE_KINDA_SMALL_NUMBER
			? Delta / Distance
			: FVector::ForwardVector;
	}
	else
	{
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("ComputeDistanceToActor: one or both actors are null (A=%s, B=%s). Outputting defaults."),
			IsValid(ActorA) ? *ActorA->GetName() : TEXT("null/invalid"),
			IsValid(ActorB) ? *ActorB->GetName() : TEXT("null/invalid"));
	}

	SetOutputPinValue<float>("Distance", Distance);
	SetOutputPinValue<FVector>("Direction", Direction);

	UE_LOG(LogComposableCameraSystem, Verbose,
		TEXT("ComputeDistanceToActor: distance=%.1f, direction=(%s)"),
		Distance, *Direction.ToString());
}

void UComposableCameraComputeDistanceToActorNode::GetPinDeclarations_Implementation(
	TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	// Input: first actor.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "ActorA";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "ActorA", "Actor A");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Actor;
		Pin.bRequired = true;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "ActorATooltip",
			"First actor (e.g. the player pawn).");
		OutPins.Add(Pin);
	}

	// Input: second actor.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "ActorB";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "ActorB", "Actor B");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Actor;
		Pin.bRequired = true;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "ActorBTooltip",
			"Second actor (e.g. the look-at target).");
		OutPins.Add(Pin);
	}

	// Output: distance between the two actors.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "Distance";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Distance", "Distance");
		Pin.Direction = EComposableCameraPinDirection::Output;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "DistanceTooltip",
			"Euclidean distance between Actor A and Actor B at activation time.");
		OutPins.Add(Pin);
	}

	// Output: unit direction from A to B.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "Direction";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "Direction", "Direction");
		Pin.Direction = EComposableCameraPinDirection::Output;
		Pin.PinType = EComposableCameraPinType::Vector3D;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "DirectionTooltip",
			"Unit direction vector from Actor A toward Actor B.");
		OutPins.Add(Pin);
	}
}
