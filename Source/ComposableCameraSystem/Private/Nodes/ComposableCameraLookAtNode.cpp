// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraLookAtNode.h"

#include "Kismet/KismetMathLibrary.h"

void UComposableCameraLookAtNode::OnInitialize_Implementation()
{
	Super::OnInitialize_Implementation();

	Interpolator_T = IsValid(SoftLookAtInterpolator) ? SoftLookAtInterpolator->BuildRotatorInterpolator() : nullptr;

	if (LookAtType == EComposableCameraLookAtType::ByActor)
	{
		if (!LookAtActor)
		{
			return;
		}
		
		if (USkeletalMeshComponent* Comp = LookAtActor->GetComponentByClass<USkeletalMeshComponent>())
		{
			SkeletalMeshComponentForLookAtActor = Comp;
		}
	}
}

void UComposableCameraLookAtNode::OnTickNode_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	FVector CurrentLookAtPosition = FVector::ZeroVector;
	
	if (LookAtType == EComposableCameraLookAtType::ByPosition)
	{
		CurrentLookAtPosition = LookAtPosition;
	}
	else if (LookAtType == EComposableCameraLookAtType::ByActor)
	{
		if (SkeletalMeshComponentForLookAtActor && SkeletalMeshComponentForLookAtActor->DoesSocketExist(LookAtSocket))
		{
			CurrentLookAtPosition = SkeletalMeshComponentForLookAtActor->GetSocketLocation(LookAtSocket);
		}
		else if (LookAtActor)
		{
			CurrentLookAtPosition = LookAtActor->GetActorLocation();
		}
	}

	FRotator ResultRotation = FRotator::ZeroRotator;
	
	if (LookAtConstraintType == EComposableCameraLookAtConstraintType::Hard)
	{
		ResultRotation = UKismetMathLibrary::FindLookAtRotation(OutCameraPose.Position, CurrentLookAtPosition);
	}
	else if (LookAtConstraintType == EComposableCameraLookAtConstraintType::Soft)
	{
		FRotator CurrentRotation = OutCameraPose.Rotation;
		FRotator TargetRotation = UKismetMathLibrary::FindLookAtRotation(OutCameraPose.Position, CurrentLookAtPosition);
		FRotator LookAtRotation = TargetRotation;
		FRotator DeltaRotation = (TargetRotation - CurrentRotation).GetNormalized();
		TargetRotation = (DeltaRotation * SoftLookAtWeight + CurrentRotation).GetNormalized();
		
		if (Interpolator_T)
		{
			Interpolator_T->Reset(CurrentRotation, TargetRotation);
			TargetRotation = Interpolator_T->Run(DeltaTime);
		}

		FVector TargetVector = TargetRotation.RotateVector(FVector::ForwardVector);
		FVector LookAtVector = LookAtRotation.RotateVector(FVector::ForwardVector);
		float Degree = UKismetMathLibrary::DegAcos(TargetVector.Dot(LookAtVector));
		
		if (Degree > SoftLookAtRange)
		{
			float RangeRatio = (Degree - SoftLookAtRange) / Degree;
			TargetRotation = ((LookAtRotation - TargetRotation).GetNormalized() * RangeRatio + TargetRotation).GetNormalized();
		}

		ResultRotation = TargetRotation;
	}

	OutCameraPose.Rotation = ResultRotation;
}

void UComposableCameraLookAtNode::GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	// LookAtType — selects whether the target is LookAtPosition or LookAtActor.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("LookAtType");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraLookAtNode", "LookAtType", "Look At Type");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Enum;
		Pin.EnumType = StaticEnum<EComposableCameraLookAtType>();
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = Pin.EnumType ? Pin.EnumType->GetNameStringByValue(static_cast<int64>(LookAtType)) : FString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraLookAtNode", "LookAtTypeTip",
			"Selects whether the camera looks at LookAtPosition or LookAtActor.");
		OutPins.Add(Pin);
	}

	// LookAtPosition — target position (used when LookAtType == ByPosition).
	FComposableCameraNodePinDeclaration LookAtPositionPin;
	LookAtPositionPin.PinName = TEXT("LookAtPosition");
	LookAtPositionPin.DisplayName = NSLOCTEXT("ComposableCameraLookAtNode", "LookAtPosition", "Look At Position");
	LookAtPositionPin.Direction = EComposableCameraPinDirection::Input;
	LookAtPositionPin.PinType = EComposableCameraPinType::Vector3D;
	LookAtPositionPin.bRequired = false;
	LookAtPositionPin.bDefaultAsPin = false;
	LookAtPositionPin.Tooltip = NSLOCTEXT("ComposableCameraLookAtNode", "LookAtPositionTip", "World position to look at (when LookAtType is ByPosition).");
	OutPins.Add(LookAtPositionPin);

	// LookAtActor — target actor (used when LookAtType == ByActor).
	FComposableCameraNodePinDeclaration LookAtActorPin;
	LookAtActorPin.PinName = TEXT("LookAtActor");
	LookAtActorPin.DisplayName = NSLOCTEXT("ComposableCameraLookAtNode", "LookAtActor", "Look At Actor");
	LookAtActorPin.Direction = EComposableCameraPinDirection::Input;
	LookAtActorPin.PinType = EComposableCameraPinType::Actor;
	LookAtActorPin.bRequired = false;
	LookAtActorPin.bDefaultAsPin = false;
	LookAtActorPin.Tooltip = NSLOCTEXT("ComposableCameraLookAtNode", "LookAtActorTip", "Actor to look at (when LookAtType is ByActor).");
	OutPins.Add(LookAtActorPin);

	// LookAtSocket — skeletal-mesh socket on LookAtActor (optional).
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("LookAtSocket");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraLookAtNode", "LookAtSocket", "Look At Socket");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Name;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = LookAtSocket.ToString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraLookAtNode", "LookAtSocketTip",
			"Skeletal-mesh socket on LookAtActor used as the look-at target. If unresolved, the actor's location is used instead.");
		OutPins.Add(Pin);
	}

	// LookAtConstraintType — Hard vs Soft look-at.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("LookAtConstraintType");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraLookAtNode", "LookAtConstraintType", "Look At Constraint Type");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Enum;
		Pin.EnumType = StaticEnum<EComposableCameraLookAtConstraintType>();
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = Pin.EnumType ? Pin.EnumType->GetNameStringByValue(static_cast<int64>(LookAtConstraintType)) : FString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraLookAtNode", "LookAtConstraintTypeTip",
			"Hard locks the camera to the target; Soft allows player control around the look-at direction.");
		OutPins.Add(Pin);
	}

	// SoftLookAtRange — tolerance angle before pulling toward the target.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("SoftLookAtRange");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraLookAtNode", "SoftLookAtRange", "Soft Look At Range");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = FString::SanitizeFloat(SoftLookAtRange);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraLookAtNode", "SoftLookAtRangeTip",
			"Degrees of tolerance around the look-at direction before the camera is pulled back toward it (soft mode).");
		OutPins.Add(Pin);
	}

	// SoftLookAtWeight — how strongly the soft look-at pulls toward the target.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("SoftLookAtWeight");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraLookAtNode", "SoftLookAtWeight", "Soft Look At Weight");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = FString::SanitizeFloat(SoftLookAtWeight);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraLookAtNode", "SoftLookAtWeightTip",
			"The larger it is, the harder the camera will look at the target.");
		OutPins.Add(Pin);
	}
}
