// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraImpulseResolutionNode.h"

#include "Components/SphereComponent.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Utils/ComposableCameraImpulseShapeInterface.h"

void UComposableCameraImpulseResolutionNode::OnInitialize_Implementation()
{
	Super::OnInitialize_Implementation();

	Interpolator_T = Interpolator ? Interpolator->BuildVector3dInterpolator() : nullptr;
	
	Sphere = NewObject<USphereComponent>(GetOwningCamera());
	Sphere->RegisterComponent();
	Sphere->InitSphereRadius(10.0f);
	Sphere->SetGenerateOverlapEvents(true);
	Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Sphere->SetCollisionResponseToAllChannels(ECR_Overlap);
	Sphere->AttachToComponent(GetOwningCamera()->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
}

void UComposableCameraImpulseResolutionNode::OnTickNode_Implementation(float DeltaTime,
                                                                       const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	FVector CombinedForce = FVector::ZeroVector;

	for (auto Shape : ImpulseShapes)
	{
		FVector Force = Shape->GetForce(CurrentCameraPose.Position);
		CombinedForce += Force;
	}

	FVector Impulse = CombinedForce * DeltaTime;
	FVector NewVelocity = OldVelocity + Impulse;
	NewVelocity *= FMath::Clamp(1.f - VelocityDamping * DeltaTime, 0.f, 1.f);
	
	if (Interpolator_T)
	{
		Interpolator_T->Reset(OldVelocity, NewVelocity);
		NewVelocity = Interpolator_T->Run(DeltaTime);
	}
	
	FVector AdditivePosition = NewVelocity * DeltaTime;
	OutCameraPose.Position += AdditivePosition;
	OldVelocity = NewVelocity;
}

void UComposableCameraImpulseResolutionNode::GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	FComposableCameraNodePinDeclaration PinDecl;

	// Input: VelocityDamping
	PinDecl.PinName = TEXT("VelocityDamping");
	PinDecl.DisplayName = NSLOCTEXT("UComposableCameraImpulseResolutionNode", "VelocityDamping", "Velocity Damping");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Float;
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.Tooltip = NSLOCTEXT("UComposableCameraImpulseResolutionNode", "VelocityDampingTip", "Controls camera movement speed (default from UPROPERTY).");
	OutPins.Add(PinDecl);
}

void UComposableCameraImpulseResolutionNode::BeginDestroy()
{
	Super::BeginDestroy();

	if (Sphere)
	{
		Sphere->DestroyComponent();
	}
}
