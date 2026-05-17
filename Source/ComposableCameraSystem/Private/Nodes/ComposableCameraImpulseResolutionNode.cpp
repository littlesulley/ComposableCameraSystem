// Copyright 2026 Sulley. All Rights Reserved.

#include "Nodes/ComposableCameraImpulseResolutionNode.h"

#include "ComposableCameraSystemModule.h"
#include "Components/SphereComponent.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Utils/ComposableCameraImpulseShapeInterface.h"

void UComposableCameraImpulseResolutionNode::AddImpulseShape(AActor* Shape)
{
	if (!IsValid(Shape))
	{
		return;
	}
	if (!Shape->GetClass()->ImplementsInterface(UComposableCameraImpulseShapeInterface::StaticClass()))
	{
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("AddImpulseShape: actor '%s' does not implement IComposableCameraImpulseShapeInterface. Rejected."),
			*Shape->GetName());
		return;
	}
	// AddUnique on a TWeakObjectPtr<AActor> compares the underlying pointer,
	// so a re-entered overlap on the same actor stays a single entry.
	ImpulseShapeActors.AddUnique(Shape);
}

void UComposableCameraImpulseResolutionNode::RemoveImpulseShape(AActor* Shape)
{
	if (!Shape)
	{
		return;
	}
	ImpulseShapeActors.RemoveAll([Shape](const TWeakObjectPtr<AActor>& WeakActor)
	{
		// Drop both the matching actor AND any incidentally-stale entries
		// while we're walking. Keeps the array small over long sessions
		// where End-Overlap fires were missed.
		return !WeakActor.IsValid() || WeakActor.Get() == Shape;
	});
}

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

	// Iterate backwards so RemoveAtSwap-while-prune doesn't skip entries.
	// Resolve each weak in place: dead->drop (covers missed EndOverlap on
	// actor destruction / streaming-out); alive but interface gone->drop;
	// alive + interface ->call GetForce.
	for (int32 i = ImpulseShapeActors.Num() - 1; i >= 0; --i)
	{
		AActor* Actor = ImpulseShapeActors[i].Get();
		if (!IsValid(Actor))
		{
			ImpulseShapeActors.RemoveAtSwap(i, EAllowShrinking::No);
			continue;
		}
		IComposableCameraImpulseShapeInterface* Shape =
			Cast<IComposableCameraImpulseShapeInterface>(Actor);
		if (!Shape)
		{
			// Class no longer implements the interface (hot-reload / class
			// reinstancing edge case). Prune.
			ImpulseShapeActors.RemoveAtSwap(i, EAllowShrinking::No);
			continue;
		}
		CombinedForce += Shape->GetForce(CurrentCameraPose.Position);
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
