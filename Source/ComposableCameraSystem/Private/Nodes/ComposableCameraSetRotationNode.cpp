// Copyright 2026 Sulley. All Rights Reserved.

#include "Nodes/ComposableCameraSetRotationNode.h"

#include "Cameras/ComposableCameraCameraBase.h"
#include "ComposableCameraSystemModule.h"
#include "GameFramework/Actor.h"
#include "Kismet/KismetMathLibrary.h"

namespace
{
	FRotator ApplySetRotationOffset(const FRotator& BaseRotation, const FRotator& RotationOffset)
	{
		const FQuat WorldYawQuat = FRotator(0.f, RotationOffset.Yaw, 0.f).Quaternion();
		const FQuat BaseQuat = BaseRotation.Quaternion();
		const FQuat LocalPitchRollQuat = FRotator(RotationOffset.Pitch, 0.f, RotationOffset.Roll).Quaternion();
		return (WorldYawQuat * BaseQuat * LocalPitchRollQuat).GetNormalized().Rotator();
	}

	bool TryResolveSetRotation(
		const UComposableCameraCameraNodeBase* Node,
		EComposableCameraSetRotationSource RotationSource,
		EComposableCameraActorInputSource RotationActorSource,
		AActor* RotationActor,
		EComposableCameraActorInputSource FirstActorSource,
		AActor* FirstActor,
		EComposableCameraActorInputSource SecondActorSource,
		AActor* SecondActor,
		const FVector& RotationVector,
		const FRotator& Rotation,
		const FRotator& RotationOffset,
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
					break;
				}

				UE_LOG(LogComposableCameraSystem, Warning,
					TEXT("SetRotation: RotationSource=FromActor but resolved actor is null; preserving upstream rotation."));
				return false;
			}

		case EComposableCameraSetRotationSource::FromTwoActors:
			{
				AActor* EffectiveFirstActor = ComposableCameraSystem::ResolveActorInput(
					FirstActorSource,
					FirstActor,
					Node ? Node->GetOwningPlayerCameraManager() : nullptr,
					Node);
				AActor* EffectiveSecondActor = ComposableCameraSystem::ResolveActorInput(
					SecondActorSource,
					SecondActor,
					Node ? Node->GetOwningPlayerCameraManager() : nullptr,
					Node);
				if (!IsValid(EffectiveFirstActor) || !IsValid(EffectiveSecondActor))
				{
					UE_LOG(LogComposableCameraSystem, Warning,
						TEXT("SetRotation: RotationSource=FromTwoActors but one or both actors are null; preserving upstream rotation."));
					return false;
				}

				const FVector Direction = EffectiveSecondActor->GetActorLocation() - EffectiveFirstActor->GetActorLocation();
				if (!Direction.IsNearlyZero())
				{
					OutRotation = UKismetMathLibrary::MakeRotFromX(Direction);
					break;
				}

				UE_LOG(LogComposableCameraSystem, Warning,
					TEXT("SetRotation: RotationSource=FromTwoActors but actor positions are identical; preserving upstream rotation."));
				return false;
			}

		case EComposableCameraSetRotationSource::FromVector:
			if (!RotationVector.IsNearlyZero())
			{
				OutRotation = UKismetMathLibrary::MakeRotFromX(RotationVector);
				break;
			}

			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("SetRotation: RotationSource=FromVector but RotationVector is zero; preserving upstream rotation."));
			return false;

		case EComposableCameraSetRotationSource::FromRotator:
			OutRotation = Rotation;
			break;

		default:
			return false;
		}

		OutRotation = ApplySetRotationOffset(OutRotation, RotationOffset);
		return true;
	}

	void DeclareSetRotationPins(
		TArray<FComposableCameraNodePinDeclaration>& OutPins,
		EComposableCameraSetRotationSource RotationSource,
		EComposableCameraActorInputSource RotationActorSource,
		EComposableCameraActorInputSource FirstActorSource,
		EComposableCameraActorInputSource SecondActorSource,
		const FVector& RotationVector,
		const FRotator& Rotation,
		const FRotator& RotationOffset)
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
			PinDecl.Tooltip = NSLOCTEXT("UComposableCameraSetRotationNode", "RotationSourceTip", "Selects whether camera rotation is set from an actor, two actors, vector, or rotator.");
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
			PinDecl.PinName = TEXT("FirstActorSource");
			PinDecl.DisplayName = NSLOCTEXT("UComposableCameraSetRotationNode", "FirstActorSource", "First Actor Source");
			PinDecl.Direction = EComposableCameraPinDirection::Input;
			PinDecl.PinType = EComposableCameraPinType::Enum;
			PinDecl.EnumType = StaticEnum<EComposableCameraActorInputSource>();
			PinDecl.bRequired = false;
			PinDecl.bDefaultAsPin = false;
			PinDecl.DefaultValueString = PinDecl.EnumType ? PinDecl.EnumType->GetNameStringByValue(static_cast<int64>(FirstActorSource)) : FString();
			PinDecl.Tooltip = NSLOCTEXT("UComposableCameraSetRotationNode", "FirstActorSourceTip", "Selects whether FirstActor comes from an explicit actor or the controller's controlled pawn.");
			OutPins.Add(PinDecl);
		}

		{
			FComposableCameraNodePinDeclaration PinDecl;
			PinDecl.PinName = TEXT("FirstActor");
			PinDecl.DisplayName = NSLOCTEXT("UComposableCameraSetRotationNode", "FirstActor", "First Actor");
			PinDecl.Direction = EComposableCameraPinDirection::Input;
			PinDecl.PinType = EComposableCameraPinType::Actor;
			PinDecl.bRequired = false;
			PinDecl.bDefaultAsPin = false;
			PinDecl.Tooltip = NSLOCTEXT("UComposableCameraSetRotationNode", "FirstActorTip", "First endpoint for FromTwoActors.");
			OutPins.Add(PinDecl);
		}

		{
			FComposableCameraNodePinDeclaration PinDecl;
			PinDecl.PinName = TEXT("SecondActorSource");
			PinDecl.DisplayName = NSLOCTEXT("UComposableCameraSetRotationNode", "SecondActorSource", "Second Actor Source");
			PinDecl.Direction = EComposableCameraPinDirection::Input;
			PinDecl.PinType = EComposableCameraPinType::Enum;
			PinDecl.EnumType = StaticEnum<EComposableCameraActorInputSource>();
			PinDecl.bRequired = false;
			PinDecl.bDefaultAsPin = false;
			PinDecl.DefaultValueString = PinDecl.EnumType ? PinDecl.EnumType->GetNameStringByValue(static_cast<int64>(SecondActorSource)) : FString();
			PinDecl.Tooltip = NSLOCTEXT("UComposableCameraSetRotationNode", "SecondActorSourceTip", "Selects whether SecondActor comes from an explicit actor or the controller's controlled pawn.");
			OutPins.Add(PinDecl);
		}

		{
			FComposableCameraNodePinDeclaration PinDecl;
			PinDecl.PinName = TEXT("SecondActor");
			PinDecl.DisplayName = NSLOCTEXT("UComposableCameraSetRotationNode", "SecondActor", "Second Actor");
			PinDecl.Direction = EComposableCameraPinDirection::Input;
			PinDecl.PinType = EComposableCameraPinType::Actor;
			PinDecl.bRequired = false;
			PinDecl.bDefaultAsPin = false;
			PinDecl.Tooltip = NSLOCTEXT("UComposableCameraSetRotationNode", "SecondActorTip", "Second endpoint for FromTwoActors.");
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

		{
			FComposableCameraNodePinDeclaration PinDecl;
			PinDecl.PinName = TEXT("RotationOffset");
			PinDecl.DisplayName = NSLOCTEXT("UComposableCameraSetRotationNode", "RotationOffset", "Rotation Offset");
			PinDecl.Direction = EComposableCameraPinDirection::Input;
			PinDecl.PinType = EComposableCameraPinType::Rotator;
			PinDecl.bRequired = false;
			PinDecl.bDefaultAsPin = false;
			PinDecl.DefaultValueString = RotationOffset.ToString();
			PinDecl.Tooltip = NSLOCTEXT("UComposableCameraSetRotationNode", "RotationOffsetTip", "Additional rotation applied after the base rotation is resolved. Yaw uses world Z; pitch and roll use the resolved local rotation.");
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
		FirstActorSource,
		FirstActor.Get(),
		SecondActorSource,
		SecondActor.Get(),
		RotationVector,
		Rotation,
		RotationOffset,
		TargetRotation))
	{
		OutCameraPose.Rotation = TargetRotation;
	}
}

void UComposableCameraSetRotationNode::GetPinDeclarations_Implementation(
	TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	DeclareSetRotationPins(
		OutPins,
		RotationSource,
		RotationActorSource,
		FirstActorSource,
		SecondActorSource,
		RotationVector,
		Rotation,
		RotationOffset);
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
		FirstActorSource,
		FirstActor.Get(),
		SecondActorSource,
		SecondActor.Get(),
		RotationVector,
		Rotation,
		RotationOffset,
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
	DeclareSetRotationPins(
		OutPins,
		RotationSource,
		RotationActorSource,
		FirstActorSource,
		SecondActorSource,
		RotationVector,
		Rotation,
		RotationOffset);
}
