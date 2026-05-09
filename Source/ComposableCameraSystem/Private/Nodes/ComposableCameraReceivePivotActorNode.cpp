// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraReceivePivotActorNode.h"

#include "Cameras/ComposableCameraCameraBase.h"
#include "Components/SkeletalMeshComponent.h"

#if !UE_BUILD_SHIPPING
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

namespace
{
	// Lazy-resolve the SkelMesh on `Actor` only when it differs from what the
	// cache last resolved against. PivotActor is an input pin so the active
	// actor can change every frame; resolving lazily avoids per-frame
	// `GetComponentByClass` walks while still picking up actor swaps and
	// component churn.
	static void ResolveSkelMeshForReceivePivotActor(
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
			return; // cache hit
		}
		InOutLastResolvedActor = Actor;
		InOutSkelMesh = Actor->GetComponentByClass<USkeletalMeshComponent>();
	}
}

void UComposableCameraReceivePivotActorNode::OnInitialize_Implementation()
{
	Super::OnInitialize_Implementation();

	// Don't resolve the SkelMesh component here — PivotActor can be driven
	// by an input pin and change every frame. Resolution happens lazily in
	// Tick when the active PivotActor differs from `LastResolvedPivotActor`.
	SkeletalMeshComponentForPivotActor.Reset();
	LastResolvedPivotActor.Reset();
	LastEffectivePivotActor.Reset();
}

void UComposableCameraReceivePivotActorNode::OnTickNode_Implementation(
	float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose,
	FComposableCameraPose& OutCameraPose)
{
	// PivotActor and bUseBoneForPivot are pin-matched UPROPERTYs — already
	// resolved by the base TickNode prologue. Refresh the SkelMesh cache
	// against the just-written PivotActor before reading either branch.
	AActor* InPivotActor = ComposableCameraSystem::ResolveActorInput(
		PivotActorSource, PivotActor.Get(), GetOwningPlayerCameraManager());
	LastEffectivePivotActor = InPivotActor;
	ResolveSkelMeshForReceivePivotActor(InPivotActor, SkeletalMeshComponentForPivotActor, LastResolvedPivotActor);

	FVector OutPivotPosition = FVector::ZeroVector;

	USkeletalMeshComponent* PivotSkelMesh = SkeletalMeshComponentForPivotActor.Get();
	if (bUseBoneForPivot && IsValid(PivotSkelMesh))
	{
		OutPivotPosition = PivotSkelMesh->GetSocketLocation(BoneName);
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
	// DrawNodeDebug is `const`; it can read the cached weak ptr from the
	// last Tick but cannot refresh it (no mutating side effects). Stale-
	// cache windows are bounded by one tick — fine for a debug gizmo.
	constexpr uint8 KForeground = 1;
	FVector PivotPos = FVector::ZeroVector;
	USkeletalMeshComponent* PivotSkelMesh = SkeletalMeshComponentForPivotActor.Get();
	if (bUseBoneForPivot && IsValid(PivotSkelMesh))
	{
		PivotPos = PivotSkelMesh->GetSocketLocation(BoneName);
	}
	else if (IsValid(LastEffectivePivotActor.Get()))
	{
		PivotPos = LastEffectivePivotActor->GetActorLocation();
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
	// Input: how to resolve the pivot actor.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "PivotActorSource";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "PivotActorSource", "Pivot Actor Source");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Enum;
		Pin.EnumType = StaticEnum<EComposableCameraActorInputSource>();
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = Pin.EnumType ? Pin.EnumType->GetNameStringByValue(static_cast<int64>(PivotActorSource)) : FString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "PivotActorSourceTooltip",
			"Selects whether the pivot actor comes from the controller's controlled pawn or an explicit actor.");
		OutPins.Add(Pin);
	}

	// Input: the actor to use as pivot.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "PivotActor";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "PivotActor", "Pivot Actor");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Actor;
		Pin.bRequired = false;
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
