// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraRelativeFixedPoseNode.h"

#include "Kismet/KismetMathLibrary.h"

void UComposableCameraRelativeFixedPoseNode::OnInitialize_Implementation()
{
	Super::OnInitialize_Implementation();

	if (Method == EComposableCameraRelativeFixedPoseMethod::RelativeToActor)
	{
		if (!RelativeActor)
		{
			return;
		}
		
		if (USkeletalMeshComponent* Comp = RelativeActor->GetComponentByClass<USkeletalMeshComponent>())
		{
			SkeletalMeshComponentForRelativeActor = Comp;
		}
	}
}

void UComposableCameraRelativeFixedPoseNode::OnTickNode_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	FTransform CurrentRelativeTransform = FTransform::Identity;
	
	if (Method == EComposableCameraRelativeFixedPoseMethod::RelativeToTransform)
	{
		CurrentRelativeTransform = RelativeTransform;	
	}
	else if (Method == EComposableCameraRelativeFixedPoseMethod::RelativeToActor)
	{
		if (SkeletalMeshComponentForRelativeActor && SkeletalMeshComponentForRelativeActor->DoesSocketExist(RelativeSocket))
		{
			CurrentRelativeTransform = SkeletalMeshComponentForRelativeActor->GetSocketTransform(RelativeSocket);
		}
		else if (RelativeActor)
		{
			CurrentRelativeTransform = RelativeActor->GetActorTransform();	
		}
	}
	
	FVector TargetLocation = UKismetMathLibrary::TransformLocation(CurrentRelativeTransform, TargetTransform.GetLocation());
	FRotator TargetRotation = UKismetMathLibrary::TransformRotation(CurrentRelativeTransform, TargetTransform.GetRotation().Rotator());

	OutCameraPose.Position = TargetLocation;
	OutCameraPose.Rotation = TargetRotation;
}

void UComposableCameraRelativeFixedPoseNode::GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("Method");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraRelativeFixedPoseNode", "Method", "Method");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Enum;
		PinDecl.EnumType = StaticEnum<EComposableCameraRelativeFixedPoseMethod>();
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = false;
		PinDecl.DefaultValueString = PinDecl.EnumType ? PinDecl.EnumType->GetNameStringByValue(static_cast<int64>(Method)) : FString();
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraRelativeFixedPoseNode", "MethodTip",
			"Selects whether the reference frame is RelativeTransform or RelativeActor.");
		OutPins.Add(PinDecl);
	}

	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("RelativeTransform");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraRelativeFixedPoseNode", "RelativeTransform", "Relative Transform");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Transform;
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = false;
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraRelativeFixedPoseNode", "RelativeTransformTip", "The base transform when Method is RelativeToTransform.");
		OutPins.Add(PinDecl);
	}

	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("RelativeActor");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraRelativeFixedPoseNode", "RelativeActor", "Relative Actor");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Actor;
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = false;
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraRelativeFixedPoseNode", "RelativeActorTip", "Reference actor when Method is RelativeToActor.");
		OutPins.Add(PinDecl);
	}

	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("RelativeSocket");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraRelativeFixedPoseNode", "RelativeSocket", "Relative Socket");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Name;
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = false;
		PinDecl.DefaultValueString = RelativeSocket.ToString();
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraRelativeFixedPoseNode", "RelativeSocketTip",
			"Skeletal-mesh socket on RelativeActor used as the reference frame. If unresolved, the actor's transform is used instead.");
		OutPins.Add(PinDecl);
	}

	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("TargetTransform");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraRelativeFixedPoseNode", "TargetTransform", "Target Transform");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Transform;
		PinDecl.bRequired = false;
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraRelativeFixedPoseNode", "TargetTransformTip", "The target transform applied in local space.");
		OutPins.Add(PinDecl);
	}
}

