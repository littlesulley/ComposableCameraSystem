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
	// LookAtPosition — target position (used when LookAtType == ByPosition).
	FComposableCameraNodePinDeclaration LookAtPositionPin;
	LookAtPositionPin.PinName = TEXT("LookAtPosition");
	LookAtPositionPin.DisplayName = NSLOCTEXT("ComposableCameraLookAtNode", "LookAtPosition", "Look At Position");
	LookAtPositionPin.Direction = EComposableCameraPinDirection::Input;
	LookAtPositionPin.PinType = EComposableCameraPinType::Vector3D;
	LookAtPositionPin.bRequired = false;
	LookAtPositionPin.Tooltip = NSLOCTEXT("ComposableCameraLookAtNode", "LookAtPositionTip", "World position to look at (when LookAtType is ByPosition).");
	OutPins.Add(LookAtPositionPin);

	// LookAtActor — target actor (used when LookAtType == ByActor).
	FComposableCameraNodePinDeclaration LookAtActorPin;
	LookAtActorPin.PinName = TEXT("LookAtActor");
	LookAtActorPin.DisplayName = NSLOCTEXT("ComposableCameraLookAtNode", "LookAtActor", "Look At Actor");
	LookAtActorPin.Direction = EComposableCameraPinDirection::Input;
	LookAtActorPin.PinType = EComposableCameraPinType::Actor;
	LookAtActorPin.bRequired = false;
	LookAtActorPin.Tooltip = NSLOCTEXT("ComposableCameraLookAtNode", "LookAtActorTip", "Actor to look at (when LookAtType is ByActor).");
	OutPins.Add(LookAtActorPin);
}

