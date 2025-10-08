// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraPivotDampingNode.h"

#include "Core/ComposableCameraPlayerCamaraManager.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"

void UComposableCameraPivotDampingNode::OnBeginPlayNode_Implementation(const FComposableCameraPose& CurrentCameraPose)
{
	UpwardInterpolator_T    = IsValid(UpwardInterpolator)    ? UpwardInterpolator->BuildDoubleInterpolator()    : nullptr;
	DownwardInterpolator_T  = IsValid(DownwardInterpolator)  ? DownwardInterpolator->BuildDoubleInterpolator()  : nullptr;
	LeftwardInterpolator_T  = IsValid(LeftwardInterpolator)  ? LeftwardInterpolator->BuildDoubleInterpolator()  : nullptr;
	RightwardInterpolator_T = IsValid(RightwardInterpolator) ? RightwardInterpolator->BuildDoubleInterpolator() : nullptr;
	ForwardInterpolator_T   = IsValid(ForwardInterpolator)   ? ForwardInterpolator->BuildDoubleInterpolator()   : nullptr;
	BackwardInterpolator_T  = IsValid(BackwardInterpolator)  ? BackwardInterpolator->BuildDoubleInterpolator()  : nullptr;

	LastPivotPosition = ContextPivotPosition.Variable ? ContextPivotPosition.Variable->RuntimeValue : ContextPivotPosition.Value;
}

void UComposableCameraPivotDampingNode::OnTickNode_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	FVector Pivot = ContextPivotPosition.Value;
	if (ContextPivotPosition.Variable)
	{
		Pivot = ContextPivotPosition.Variable->RuntimeValue;
	}

	FRotator CameraRotation = OutCameraPose.Rotation;
	
	if (bMaintainCameraSpacePivotPosition)
	{
		FRotator LastCameraRotation = OwningPlayerCameraManager->GetCameraRotation();
		FVector FakeCameraSpacePivotDirection = UKismetMathLibrary::LessLess_VectorRotator(
			Pivot - LastPivotPosition, LastCameraRotation);
		LastPivotPosition = Pivot - UKismetMathLibrary::GreaterGreater_VectorRotator(FakeCameraSpacePivotDirection, CameraRotation);
	}

	CameraRotation.Pitch = 0;
	
	FVector WorldSpacePivotDirection = Pivot - LastPivotPosition;
	FVector CameraSpacePivotDirection =
		UKismetMathLibrary::LessLess_VectorRotator(WorldSpacePivotDirection, CameraRotation);
	
	FVector CameraSpaceDampedPivotDirection = CameraSpacePivotDirection;
	
	if (CameraSpacePivotDirection.X > 0)
	{
		double NewX = CameraSpacePivotDirection.X;
		if (ForwardInterpolator_T)
		{
			ForwardInterpolator_T->Reset(0, CameraSpacePivotDirection.X);
			NewX = ForwardInterpolator_T->Run(DeltaTime);
		}
		CameraSpaceDampedPivotDirection.X = NewX;
	}
	else
	{
		double NewX = CameraSpacePivotDirection.X;
		if (BackwardInterpolator_T)
		{
			BackwardInterpolator_T->Reset(0, CameraSpacePivotDirection.X);
			NewX = BackwardInterpolator_T->Run(DeltaTime);
		}
		CameraSpaceDampedPivotDirection.X = NewX;
	}

	if (CameraSpacePivotDirection.Y > 0)
	{
		double NewY = CameraSpacePivotDirection.Y;
		if (RightwardInterpolator_T)
		{
			RightwardInterpolator_T->Reset(0, CameraSpacePivotDirection.Y);
			NewY = RightwardInterpolator_T->Run(DeltaTime);
		}
		CameraSpaceDampedPivotDirection.Y = NewY;
	}
	else
	{
		double NewY = CameraSpacePivotDirection.Y;
		if (LeftwardInterpolator_T)
		{
			LeftwardInterpolator_T->Reset(0, CameraSpacePivotDirection.Y);
			NewY = LeftwardInterpolator_T->Run(DeltaTime);
		}
		CameraSpaceDampedPivotDirection.Y = NewY;
	}

	if (CameraSpacePivotDirection.Z > 0)
	{
		double NewZ = CameraSpacePivotDirection.Z;
		if (UpwardInterpolator_T)
		{
			UpwardInterpolator_T->Reset(0, CameraSpacePivotDirection.Z);
			NewZ= UpwardInterpolator_T->Run(DeltaTime);
		}
		CameraSpaceDampedPivotDirection.Z = NewZ;
	}
	else
	{
		double NewZ = CameraSpacePivotDirection.Z;
		if (DownwardInterpolator_T)
		{
			DownwardInterpolator_T->Reset(0, CameraSpacePivotDirection.Z);
			NewZ= DownwardInterpolator_T->Run(DeltaTime);
		}
		CameraSpaceDampedPivotDirection.Z = NewZ;
	}
	
	FVector WorldSpaceDampedPivotPosition = UKismetMathLibrary::GreaterGreater_VectorRotator(
		CameraSpaceDampedPivotDirection, CameraRotation) + LastPivotPosition;
	
	if (ContextPivotPosition.Variable)
	{
		ContextPivotPosition.Variable->RuntimeValue = WorldSpaceDampedPivotPosition;
	}
	else
	{
		ContextPivotPosition.Value = WorldSpaceDampedPivotPosition;
	}

	LastPivotPosition = WorldSpaceDampedPivotPosition;

	if (OwningPlayerCameraManager && OwningPlayerCameraManager->bDrawDebugInformation)
	{
		UKismetSystemLibrary::DrawDebugSphere(this, WorldSpaceDampedPivotPosition, 20, 12, FLinearColor::Green, 0, 1);
	}
}

void UComposableCameraPivotDampingNode::ReceiveInitializerNode(UComposableCameraCameraNodeBase* Initializer)
{
	if (UComposableCameraPivotDampingNode* CastedInitializer = Cast<UComposableCameraPivotDampingNode>(Initializer))
	{
		bMaintainCameraSpacePivotPosition = CastedInitializer->bMaintainCameraSpacePivotPosition;
		UpwardInterpolator = CastedInitializer->UpwardInterpolator;
		DownwardInterpolator = CastedInitializer->DownwardInterpolator;
		LeftwardInterpolator = CastedInitializer->LeftwardInterpolator;
		RightwardInterpolator = CastedInitializer->RightwardInterpolator;
		ForwardInterpolator = CastedInitializer->ForwardInterpolator;
		BackwardInterpolator = CastedInitializer->BackwardInterpolator;
	}
}
