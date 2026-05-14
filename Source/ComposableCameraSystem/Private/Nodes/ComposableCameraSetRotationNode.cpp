// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraSetRotationNode.h"

#include "Cameras/ComposableCameraCameraBase.h"
#include "ComposableCameraSystemModule.h"
#include "GameFramework/Actor.h"
#include "Kismet/KismetMathLibrary.h"

namespace
{
	bool TryResolveSetRotation(
		const UComposableCameraCameraNodeBase* Node,
		EComposableCameraSetRotationSource RotationSource,
		EComposableCameraActorInputSource RotationActorSource,
		AActor* RotationActor,
		const FVector& RotationVector,
		const FRotator& Rotation,
		FRotator& OutRotation)
	{
		switch (RotationSource)
		{
		case EComposableCameraSetRotationSource::FromActor:
			{
				AActor* EffectiveRotationActor = ComposableCameraSystem::ResolveActorInput(
					RotationActorSource,
					RotationActor,
					Node ? Node->GetOwningPlayerCameraManager() : nullptr,
					Node);
				if (IsValid(EffectiveRotationActor))
				{
					OutRotation = UKismetMathLibrary::MakeRotFromX(EffectiveRotationActor->GetActorForwardVector());
					return true;
				}

				UE_LOG(LogComposableCameraSystem, Warning,
					TEXT("SetRotation: RotationSource=FromActor but resolved actor is null; preserving upstream rotation."));
				return false;
			}

		case EComposableCameraSetRotationSource::FromVector:
			if (!RotationVector.IsNearlyZero())
			{
				OutRotation = UKismetMathLibrary::MakeRotFromX(RotationVector);
				return true;
			}

			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("SetRotation: RotationSource=FromVector but RotationVector is zero; preserving upstream rotation."));
			return false;

		case EComposableCameraSetRotationSource::FromRotator:
			OutRotation = Rotation;
			return true;
		}

		return false;
	}

	void DeclareSetRotationPins(
		TArray<FComposableCameraNodePinDeclaration>& OutPins,
		EComposableCameraSetRotationSource RotationSource,
		EComposableCameraActorInputSource RotationActorSource,
		const FVector& RotationVector,
		const FRotator& Rotation)
	{
		{
			FComposableCameraNodePinDeclaration PinDecl;
			PinDecl.PinName = TEXT("RotationSource");
			PinDecl.DisplayName = NSLOCTEXT("UComposableCameraSetRotationNode", "RotationSource", "Rotation Source");
			PinDecl.Direction = EComposableCameraPinDirection::Input;
			PinDecl.PinType = EComposableCameraPinType::Enum;
			PinDecl.EnumType = StaticEnum<EComposableCameraSetRotationSource>();
			PinDecl.bRequired = false;
			PinDecl.bDefaultAsPin = false;
			PinDecl.DefaultValueString = PinDecl.EnumType ? PinDecl.EnumType->GetNameStringByValue(static_cast<int64>(RotationSource)) : FString();
			PinDecl.Tooltip = NSLOCTEXT("UComposableCameraSetRotationNode", "RotationSourceTip", "Selects whether camera rotation is set from an actor, vector, or rotator.");
			OutPins.Add(PinDecl);
		}

		{
			FComposableCameraNodePinDeclaration PinDecl;
			PinDecl.PinName = TEXT("RotationActorSource");
			PinDecl.DisplayName = NSLOCTEXT("UComposableCameraSetRotationNode", "RotationActorSource", "Rotation Actor Source");
			PinDecl.Direction = EComposableCameraPinDirection::Input;
			PinDecl.PinType = EComposableCameraPinType::Enum;
			PinDecl.EnumType = StaticEnum<EComposableCameraActorInputSource>();
			PinDecl.bRequired = false;
			PinDecl.bDefaultAsPin = false;
			PinDecl.DefaultValueString = PinDecl.EnumType ? PinDecl.EnumType->GetNameStringByValue(static_cast<int64>(RotationActorSource)) : FString();
			PinDecl.Tooltip = NSLOCTEXT("UComposableCameraSetRotationNode", "RotationActorSourceTip", "Selects whether RotationActor comes from an explicit actor or the controller's controlled pawn.");
			OutPins.Add(PinDecl);
		}

		{
			FComposableCameraNodePinDeclaration PinDecl;
			PinDecl.PinName = TEXT("RotationActor");
			PinDecl.DisplayName = NSLOCTEXT("UComposableCameraSetRotationNode", "RotationActor", "Rotation Actor");
			PinDecl.Direction = EComposableCameraPinDirection::Input;
			PinDecl.PinType = EComposableCameraPinType::Actor;
			PinDecl.bRequired = false;
			PinDecl.bDefaultAsPin = false;
			PinDecl.Tooltip = NSLOCTEXT("UComposableCameraSetRotationNode", "RotationActorTip", "Actor whose forward vector defines the replacement rotation.");
			OutPins.Add(PinDecl);
		}

		{
			FComposableCameraNodePinDeclaration PinDecl;
			PinDecl.PinName = TEXT("RotationVector");
			PinDecl.DisplayName = NSLOCTEXT("UComposableCameraSetRotationNode", "RotationVector", "Rotation Vector");
			PinDecl.Direction = EComposableCameraPinDirection::Input;
			PinDecl.PinType = EComposableCameraPinType::Vector3D;
			PinDecl.bRequired = false;
			PinDecl.bDefaultAsPin = false;
			PinDecl.DefaultValueString = RotationVector.ToString();
			PinDecl.Tooltip = NSLOCTEXT("UComposableCameraSetRotationNode", "RotationVectorTip", "Forward vector used to build the replacement rotation.");
			OutPins.Add(PinDecl);
		}

		{
			FComposableCameraNodePinDeclaration PinDecl;
			PinDecl.PinName = TEXT("Rotation");
			PinDecl.DisplayName = NSLOCTEXT("UComposableCameraSetRotationNode", "Rotation", "Rotation");
			PinDecl.Direction = EComposableCameraPinDirection::Input;
			PinDecl.PinType = EComposableCameraPinType::Rotator;
			PinDecl.bRequired = false;
			PinDecl.bDefaultAsPin = false;
			PinDecl.DefaultValueString = Rotation.ToString();
			PinDecl.Tooltip = NSLOCTEXT("UComposableCameraSetRotationNode", "RotationTip", "Literal replacement rotation.");
			OutPins.Add(PinDecl);
		}
	}
}

void UComposableCameraSetRotationNode::OnTickNode_Implementation(
	float /*DeltaTime*/, const FComposableCameraPose& /*CurrentCameraPose*/, FComposableCameraPose& OutCameraPose)
{
	FRotator TargetRotation;
	if (TryResolveSetRotation(
		this,
		RotationSource,
		RotationActorSource,
		RotationActor.Get(),
		RotationVector,
		Rotation,
		TargetRotation))
	{
		OutCameraPose.Rotation = TargetRotation;
	}
}

void UComposableCameraSetRotationNode::GetPinDeclarations_Implementation(
	TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	DeclareSetRotationPins(OutPins, RotationSource, RotationActorSource, RotationVector, Rotation);
}

void UComposableCameraBeginPlaySetRotationNode::ExecuteBeginPlay()
{
	ResolveAllInputPins();

	FRotator TargetRotation;
	if (!TryResolveSetRotation(
		this,
		RotationSource,
		RotationActorSource,
		RotationActor.Get(),
		RotationVector,
		Rotation,
		TargetRotation))
	{
		return;
	}

	AComposableCameraCameraBase* Camera = GetOwningCamera();
	if (!IsValid(Camera))
	{
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("SetRotation: BeginPlay node has no owning camera; cannot set initial rotation."));
		return;
	}

	Camera->CameraPose.Rotation = TargetRotation;
	Camera->LastFrameCameraPose.Rotation = TargetRotation;

	UE_LOG(LogComposableCameraSystem, Verbose,
		TEXT("SetRotation: BeginPlay wrote initial rotation %s on camera %s."),
		*TargetRotation.ToCompactString(),
		*Camera->GetName());
}

void UComposableCameraBeginPlaySetRotationNode::GetPinDeclarations_Implementation(
	TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	DeclareSetRotationPins(OutPins, RotationSource, RotationActorSource, RotationVector, Rotation);
}
