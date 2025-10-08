// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraLookAtNode.h"

#include "Kismet/KismetMathLibrary.h"

void UComposableCameraLookAtNode::OnBeginPlayNode_Implementation(const FComposableCameraPose& CurrentCameraPose)
{
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

void UComposableCameraLookAtNode::ReceiveInitializerNode(UComposableCameraCameraNodeBase* Initializer)
{
	if (UComposableCameraLookAtNode* CastedInitializer = Cast<UComposableCameraLookAtNode>(Initializer))
	{
		LookAtType = CastedInitializer->LookAtType;
		LookAtPosition = CastedInitializer->LookAtPosition;
		LookAtActor = CastedInitializer->LookAtActor;
		LookAtSocket = CastedInitializer->LookAtSocket;
		LookAtConstraintType = CastedInitializer->LookAtConstraintType;
		SoftLookAtRange = CastedInitializer->SoftLookAtRange;
		SoftLookAtWeight = CastedInitializer->SoftLookAtWeight;
		SoftLookAtInterpolator = CastedInitializer->SoftLookAtInterpolator;
	}
}
