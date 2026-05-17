// Copyright 2026 Sulley. All Rights Reserved.

#include "Nodes/ComposableCameraRelativeFixedPoseNode.h"

#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Kismet/KismetMathLibrary.h"

#if !UE_BUILD_SHIPPING
#include "Debug/ComposableCameraViewportDebug.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"

namespace
{
	static TAutoConsoleVariable<int32> CVarShowRelativeFixedPoseGizmo(
		TEXT("CCS.Debug.Viewport.RelativeFixedPose"),
		0,
		TEXT("Show RelativeFixedPoseNode gizmo (orange sphere at the reference transform / actor origin).\n")
		TEXT("Requires `CCS.Debug.Viewport 1`."),
		ECVF_Default);
}
#endif

void UComposableCameraRelativeFixedPoseNode::OnInitialize_Implementation()
{
	Super::OnInitialize_Implementation();

	SkeletalMeshComponentForRelativeActor.Reset();
	LastResolvedRelativeActor.Reset();
}

namespace
{
	static void ResolveSkelMeshForRelativeActor(
		AActor* Actor,
		TWeakObjectPtr<USkeletalMeshComponent>& InOutSkelMesh,
		TWeakObjectPtr<AActor>& InOutLastResolvedActor)
	{
		if (!IsValid(Actor))
		{
			InOutSkelMesh.Reset();
			InOutLastResolvedActor.Reset();
			return;
		}
		if (InOutLastResolvedActor.Get() == Actor && InOutSkelMesh.IsValid())
		{
			return;
		}
		InOutLastResolvedActor = Actor;
		InOutSkelMesh = Actor->GetComponentByClass<USkeletalMeshComponent>();
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
		AActor* EffectiveRelativeActor = ComposableCameraSystem::ResolveActorInput(
			RelativeActorSource, RelativeActor.Get(), GetOwningPlayerCameraManager(), this);
		ResolveSkelMeshForRelativeActor(EffectiveRelativeActor, SkeletalMeshComponentForRelativeActor, LastResolvedRelativeActor);

		USkeletalMeshComponent* Comp = SkeletalMeshComponentForRelativeActor.Get();
		if (IsValid(Comp) && Comp->DoesSocketExist(RelativeSocket))
		{
			CurrentRelativeTransform = Comp->GetSocketTransform(RelativeSocket);
		}
		else if (IsValid(EffectiveRelativeActor))
		{
			CurrentRelativeTransform = EffectiveRelativeActor->GetActorTransform();
		}
	}
	
	FVector TargetLocation = UKismetMathLibrary::TransformLocation(CurrentRelativeTransform, TargetTransform.GetLocation());
	FRotator TargetRotation = UKismetMathLibrary::TransformRotation(CurrentRelativeTransform, TargetTransform.GetRotation().Rotator());

	OutCameraPose.Position = TargetLocation;
	OutCameraPose.Rotation = TargetRotation;
}

#if !UE_BUILD_SHIPPING
void UComposableCameraRelativeFixedPoseNode::DrawNodeDebug(UWorld* World, bool /*bViewerIsOutsideCamera*/) const
{
	if (!World) { return; }
	if (CVarShowRelativeFixedPoseGizmo.GetValueOnGameThread() == 0
		&& !FComposableCameraViewportDebug::ShouldShowAllNodeGizmos()) { return; }

	// Resolve the reference transform origin the same way OnTickNode does -
	// a sphere at that origin is the "what am I relative TO?" marker.
	// The output pose itself (target camera pose) is already visible via
	// the frustum when in F8 eject, so we don't double up there.
	constexpr uint8 KForeground = 1;
	FVector OriginPos = FVector::ZeroVector;
	bool bHasOrigin = false;

	if (Method == EComposableCameraRelativeFixedPoseMethod::RelativeToTransform)
	{
		OriginPos = RelativeTransform.GetLocation();
		bHasOrigin = true;
	}
	else if (Method == EComposableCameraRelativeFixedPoseMethod::RelativeToActor)
	{
		AActor* EffectiveRelativeActor = ComposableCameraSystem::ResolveActorInput(
			RelativeActorSource, RelativeActor.Get(), GetOwningPlayerCameraManager(), this);
		USkeletalMeshComponent* Comp = SkeletalMeshComponentForRelativeActor.Get();
		if (LastResolvedRelativeActor.Get() == EffectiveRelativeActor && IsValid(Comp) && Comp->DoesSocketExist(RelativeSocket))
		{
			OriginPos = Comp->GetSocketLocation(RelativeSocket);
			bHasOrigin = true;
		}
		else if (IsValid(EffectiveRelativeActor))
		{
			OriginPos = EffectiveRelativeActor->GetActorLocation();
			bHasOrigin = true;
		}
	}

	if (bHasOrigin)
	{
		FComposableCameraViewportDebug::DrawSolidDebugSphere(
			World, OriginPos, /*Radius=*/8.f, FColor(255, 140, 0),
			/*Alpha=*/100, /*Segments=*/12, KForeground);
	}
}
#endif

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
		PinDecl.PinName = TEXT("RelativeActorSource");
		PinDecl.DisplayName = NSLOCTEXT("UComposableCameraRelativeFixedPoseNode", "RelativeActorSource", "Relative Actor Source");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Enum;
		PinDecl.EnumType = StaticEnum<EComposableCameraActorInputSource>();
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = false;
		PinDecl.DefaultValueString = PinDecl.EnumType ? PinDecl.EnumType->GetNameStringByValue(static_cast<int64>(RelativeActorSource)) : FString();
		PinDecl.Tooltip = NSLOCTEXT("UComposableCameraRelativeFixedPoseNode", "RelativeActorSourceTip",
			"Selects whether RelativeActor comes from the controller's controlled pawn or an explicit actor.");
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
