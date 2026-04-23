// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraReceivePivotActorNode.h"

#include "Cameras/ComposableCameraCameraBase.h"

#if !UE_BUILD_SHIPPING
#include "Components/SkeletalMeshComponent.h"
#include "Debug/ComposableCameraViewportDebug.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"

namespace
{
	static TAutoConsoleVariable<int32> CVarShowReceivePivotActorGizmo(
		TEXT("CCS.Debug.Viewport.ReceivePivotActor"),
		0,
		TEXT("Show ReceivePivotActorNode gizmo (white sphere at the pivot actor / bone position).\n")
		TEXT("Requires `CCS.Debug.Viewport 1`."),
		ECVF_Default);
}
#endif

void UComposableCameraReceivePivotActorNode::OnInitialize_Implementation()
{
	Super::OnInitialize_Implementation();

	if (bUseBoneForPivot)
	{
		if (IsValid(PivotActor))
		{
			SkeletalMeshComponentForPivotActor = PivotActor->GetComponentByClass<USkeletalMeshComponent>();
		}
	}
}

void UComposableCameraReceivePivotActorNode::OnTickNode_Implementation(
	float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose,
	FComposableCameraPose& OutCameraPose)
{
	// PivotActor and bUseBoneForPivot are pin-matched UPROPERTYs — already resolved
	// by the base TickNode prologue. Read the member directly.
	AActor* InPivotActor = PivotActor.Get();
	FVector OutPivotPosition = FVector::ZeroVector;

	// Use IsValid() for Actor pointers — a destroyed actor may leave a dangling
	// pointer even via TObjectPtr for non-UPROPERTY copies.
	if (bUseBoneForPivot && IsValid(SkeletalMeshComponentForPivotActor))
	{
		OutPivotPosition = SkeletalMeshComponentForPivotActor->GetSocketLocation(BoneName);
	}
	else if (IsValid(InPivotActor))
	{
		OutPivotPosition = InPivotActor->GetActorLocation();
	}

	SetOutputPinValue<FVector>("PivotPosition", OutPivotPosition);
	SetOutputPinValue<AActor*>("PivotActor_Out", IsValid(InPivotActor) ? InPivotActor : nullptr);
}

#if !UE_BUILD_SHIPPING
void UComposableCameraReceivePivotActorNode::DrawNodeDebug(UWorld* World, bool /*bViewerIsOutsideCamera*/) const
{
	if (!World) { return; }
	if (CVarShowReceivePivotActorGizmo.GetValueOnGameThread() == 0
		&& !FComposableCameraViewportDebug::ShouldShowAllNodeGizmos()) { return; }

	// Resolve pivot position the same way OnTickNode does — sphere at the
	// bone socket if configured, otherwise at the actor origin. White sphere
	// so it's distinct from PivotOffset's yellow / PivotDamping's magenta.
	constexpr uint8 KForeground = 1;
	FVector PivotPos = FVector::ZeroVector;
	if (bUseBoneForPivot && IsValid(SkeletalMeshComponentForPivotActor))
	{
		PivotPos = SkeletalMeshComponentForPivotActor->GetSocketLocation(BoneName);
	}
	else if (IsValid(PivotActor.Get()))
	{
		PivotPos = PivotActor->GetActorLocation();
	}
	else
	{
		return; // no valid anchor to draw
	}
	FComposableCameraViewportDebug::DrawSolidDebugSphere(
		World, PivotPos, /*Radius=*/9.f, FColor::White,
		/*Alpha=*/100, /*Segments=*/12, KForeground);
}
#endif

void UComposableCameraReceivePivotActorNode::GetPinDeclarations_Implementation(
	TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	// Input: the actor to use as pivot.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "PivotActor";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "PivotActor", "Pivot Actor");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Actor;
		Pin.bRequired = true;
		Pin.DefaultValueString = FString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "PivotActorTooltip",
			"The actor whose position is used as the camera pivot point.");
		OutPins.Add(Pin);
	}

	// Input: toggle bone-based pivot resolution.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "bUseBoneForPivot";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "UseBoneForPivot", "Use Bone For Pivot");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Bool;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = bUseBoneForPivot ? TEXT("true") : TEXT("false");
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "UseBoneForPivotTooltip",
			"When true, use the named bone on the pivot actor's skeletal mesh as the pivot position.");
		OutPins.Add(Pin);
	}

	// Output: the computed pivot position.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "PivotPosition";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "PivotPosition", "Pivot Position");
		Pin.Direction = EComposableCameraPinDirection::Output;
		Pin.PinType = EComposableCameraPinType::Vector3D;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "PivotPositionTooltip",
			"The world-space position of the pivot actor (or bone if configured).");
		OutPins.Add(Pin);
	}

	// Output: pass-through the pivot actor for downstream nodes.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "PivotActor_Out";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "PivotActorOut", "Pivot Actor");
		Pin.Direction = EComposableCameraPinDirection::Output;
		Pin.PinType = EComposableCameraPinType::Actor;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "PivotActorOutTooltip",
			"Pass-through of the pivot actor for downstream nodes.");
		OutPins.Add(Pin);
	}
}
