// Copyright 2026 Sulley. All Rights Reserved.

#include "Nodes/ComposableCameraComputePositionBetweenActorsNode.h"

#include "ComposableCameraSystemModule.h"
#include "GameFramework/Actor.h"

void UComposableCameraComputePositionBetweenActorsNode::ExecuteBeginPlay()
{
	ResolveAllInputPins();

	AActor* ResolvedFirstActor = ComposableCameraSystem::ResolveActorInput(
		FirstActorSource,
		FirstActor.Get(),
		GetOwningPlayerCameraManager(),
		this);
	AActor* ResolvedSecondActor = ComposableCameraSystem::ResolveActorInput(
		SecondActorSource,
		SecondActor.Get(),
		GetOwningPlayerCameraManager(),
		this);

	FVector Position = FVector::ZeroVector;
	if (IsValid(ResolvedFirstActor) && IsValid(ResolvedSecondActor))
	{
		const float ClampedAlpha = FMath::Clamp(Alpha, 0.f, 1.f);
		Position = FMath::Lerp(
			ResolvedFirstActor->GetActorLocation(),
			ResolvedSecondActor->GetActorLocation(),
			ClampedAlpha);
		Position.Z += HeightOffset;
	}
	else
	{
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("ComputePositionBetweenActors: one or both actors are null (First=%s, Second=%s). Outputting zero position."),
			*GetNameSafe(ResolvedFirstActor),
			*GetNameSafe(ResolvedSecondActor));
	}

	SetOutputPinValue<FVector>(TEXT("Position"), Position);
}

void UComposableCameraComputePositionBetweenActorsNode::GetPinDeclarations_Implementation(
	TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("FirstActorSource");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "FirstActorSource", "First Actor Source");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Enum;
		Pin.EnumType = StaticEnum<EComposableCameraActorInputSource>();
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = Pin.EnumType ? Pin.EnumType->GetNameStringByValue(static_cast<int64>(FirstActorSource)) : FString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "FirstActorSourceTooltip",
			"Selects whether the first actor comes from the controller's controlled pawn or an explicit actor.");
		OutPins.Add(Pin);
	}

	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("FirstActor");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "FirstActor", "First Actor");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Actor;
		Pin.bRequired = false;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "FirstActorTooltip",
			"Explicit actor used as the first interpolation endpoint.");
		OutPins.Add(Pin);
	}

	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("SecondActorSource");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "SecondActorSource", "Second Actor Source");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Enum;
		Pin.EnumType = StaticEnum<EComposableCameraActorInputSource>();
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = Pin.EnumType ? Pin.EnumType->GetNameStringByValue(static_cast<int64>(SecondActorSource)) : FString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "SecondActorSourceTooltip",
			"Selects whether the second actor comes from the controller's controlled pawn or an explicit actor.");
		OutPins.Add(Pin);
	}

	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("SecondActor");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "SecondActor", "Second Actor");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Actor;
		Pin.bRequired = false;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "SecondActorTooltip",
			"Explicit actor used as the second interpolation endpoint.");
		OutPins.Add(Pin);
	}

	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("Alpha");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "BetweenActorsAlpha", "Alpha");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = FString::SanitizeFloat(Alpha);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "BetweenActorsAlphaTooltip",
			"Normalized position between first actor (0) and second actor (1).");
		OutPins.Add(Pin);
	}

	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("HeightOffset");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "HeightOffset", "Height Offset");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = FString::SanitizeFloat(HeightOffset);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "HeightOffsetTooltip",
			"World-Z offset added after actor-position interpolation.");
		OutPins.Add(Pin);
	}

	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("Position");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "BetweenActorsPosition", "Position");
		Pin.Direction = EComposableCameraPinDirection::Output;
		Pin.PinType = EComposableCameraPinType::Vector3D;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "BetweenActorsPositionTooltip",
			"Computed world position between the two actors with Height Offset applied on world Z.");
		OutPins.Add(Pin);
	}
}
