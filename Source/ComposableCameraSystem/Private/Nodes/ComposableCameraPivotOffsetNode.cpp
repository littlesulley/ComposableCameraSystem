// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraPivotOffsetNode.h"

#include "Core/ComposableCameraPlayerCameraManager.h"
#include "GameFramework/Actor.h"
#include "Kismet/KismetMathLibrary.h"

#if !UE_BUILD_SHIPPING
#include "Debug/ComposableCameraViewportDebug.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"

namespace
{
	// Per-node opt-in toggle. The master `CCS.Debug.Viewport` CVar gates whether
	// *any* viewport debug draws at all; this CVar then controls whether this
	// specific node contributes its gizmo. Default off. Users opt in per node.
	static TAutoConsoleVariable<int32> CVarShowPivotOffsetGizmo(
		TEXT("CCS.Debug.Viewport.PivotOffset"),
		0,
		TEXT("Show PivotOffsetNode gizmo (yellow sphere at the post-offset pivot).\n")
		TEXT("Requires `CCS.Debug.Viewport 1`. Works in both possessed play and F8 eject."),
		ECVF_Default);
}
#endif

void UComposableCameraPivotOffsetNode::OnInitialize_Implementation()
{
	Super::OnInitialize_Implementation();

	// CurrentCameraPose at Initialize time is the outgoing camera's pose. The same
	// value AActor::BeginPlay used to pass into BeginPlayCamera. Read it from the PCM
	// so nodes that previously depended on the BeginPlayNode parameter still work.
	const FComposableCameraPose CurrentCameraPose = OwningPlayerCameraManager
		? OwningPlayerCameraManager->GetCurrentCameraPose()
		: FComposableCameraPose{};
	// Auto-resolve has not yet written into the pin-matched UPROPERTYs at Initialize
	// time, so read the pin explicitly via the fallback-aware GetInputPinValue path.
	UpdatePivotOffset(GetInputPinValue<FVector>("PivotPosition"), CurrentCameraPose);
}

void UComposableCameraPivotOffsetNode::OnTickNode_Implementation(float DeltaTime,
                                                                 const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	// PivotPosition and PivotOffset are pin-matched UPROPERTYs. Already resolved
	// by the base TickNode prologue. Read them directly.
	UpdatePivotOffset(PivotPosition, CurrentCameraPose);
}


void UComposableCameraPivotOffsetNode::UpdatePivotOffset(const FVector& InPivot, const FComposableCameraPose& CurrentCameraPose)
{
	FVector Pivot = InPivot;

	switch (PivotOffsetType)
	{
	case ECameraPivotOffset::ActorLocalSpace:
		if (AActor* EffectiveActorForLocalSpace = ComposableCameraSystem::ResolveActorInput(
			ActorForLocalSpaceSource, ActorForLocalSpace.Get(), GetOwningPlayerCameraManager(), this);
			IsValid(EffectiveActorForLocalSpace))
		{
			Pivot += EffectiveActorForLocalSpace->GetActorForwardVector() * PivotOffset[0];
			Pivot += EffectiveActorForLocalSpace->GetActorRightVector() * PivotOffset[1];
			Pivot += EffectiveActorForLocalSpace->GetActorUpVector() * PivotOffset[2];
		}
		else
		{
			Pivot += PivotOffset;
		}
		break;
	case ECameraPivotOffset::CameraSpace:
		{
			FRotator CameraRotation = CurrentCameraPose.Rotation;
			Pivot += UKismetMathLibrary::GetForwardVector(CameraRotation) * PivotOffset[0];
			Pivot += UKismetMathLibrary::GetRightVector(CameraRotation) * PivotOffset[1];
			Pivot += UKismetMathLibrary::GetUpVector(CameraRotation) * PivotOffset[2];
		}
		break;
	case ECameraPivotOffset::WorldSpace:
		Pivot += PivotOffset;
		break;
	}

	SetOutputPinValue<FVector>("PivotPosition", Pivot);

#if !UE_BUILD_SHIPPING
	LastComputedPivot = Pivot;
#endif
}

#if !UE_BUILD_SHIPPING
void UComposableCameraPivotOffsetNode::DrawNodeDebug(UWorld* World, bool /*bViewerIsOutsideCamera*/) const
{
	if (!World) { return; }
	if (CVarShowPivotOffsetGizmo.GetValueOnGameThread() == 0
		&& !FComposableCameraViewportDebug::ShouldShowAllNodeGizmos()) { return; }
	// Pivot is out at the character / world target. Never sits on top of
	// the camera, so the occlusion gate doesn't apply here.
	FComposableCameraViewportDebug::DrawSolidDebugSphere(
		World, LastComputedPivot, /*Radius=*/10.f, FColor::Yellow,
		/*Alpha=*/100, /*Segments=*/12, /*DepthPriority=*/0);
}
#endif

void UComposableCameraPivotOffsetNode::GetPinDeclarations_Implementation(
	TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	// Input: pivot position to offset.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "PivotPosition";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "PivotPos_In", "Pivot Position");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Vector3D;
		Pin.bRequired = true;
		Pin.DefaultValueString = PivotPosition.ToString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "PivotPosInTooltip",
			"The pivot position to apply offset to.");
		OutPins.Add(Pin);
	}

	// Input: the offset amount (interpreted per PivotOffsetType).
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "PivotOffset";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "PivotOffset_In", "Pivot Offset");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Vector3D;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = PivotOffset.ToString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "PivotOffset_InTooltip",
			"Offset applied to the pivot position. Interpreted as world / actor-local / camera space per PivotOffsetType.");
		OutPins.Add(Pin);
	}

	// Output: the offset pivot position.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "PivotPosition";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "PivotPos_Out", "Pivot Position");
		Pin.Direction = EComposableCameraPinDirection::Output;
		Pin.PinType = EComposableCameraPinType::Vector3D;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "PivotPosOutTooltip",
			"The pivot position after applying the offset.");
		OutPins.Add(Pin);
	}
}
