// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraPivotRotateNode.h"

#include "ComposableCameraSystemModule.h"
#include "GameFramework/Actor.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"

void UComposableCameraPivotRotateNode::OnInitialize_Implementation()
{
	Super::OnInitialize_Implementation();

	// Build the per-instance rotator interpolator once. Every subsequent tick
	// just calls Reset/Run on it, so the hot path stays allocation-free.
	Interpolator_T = IsValid(Interpolator) ? Interpolator->BuildRotatorInterpolator() : nullptr;
}

void UComposableCameraPivotRotateNode::OnTickNode_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	// Pin-resolved PivotActor is missing. Leave the upstream rotation
	// untouched so the camera doesn't snap to identity. Intentional null
	// wiring is common during early activation (context parameter not yet
	// pushed); spamming a per-frame log would be noisy. The editor's
	// required-pin diagnostics surface authoring mistakes instead.
	AActor* EffectivePivotActor = ComposableCameraSystem::ResolveActorInput(
		PivotActorSource, PivotActor.Get(), GetOwningPlayerCameraManager(), this);
	if (!IsValid(EffectivePivotActor))
	{
		return;
	}

	// Compose pivot rotation with the authored offset in the pivot's LOCAL
	// frame (quaternion multiply, same convention as USceneComponent's
	// RelativeRotation when attached to PivotActor's root). A raw FRotator
	// add would alias yaw / pitch / roll across the world frame and produce
	// gimbal artifacts when the pivot has non-trivial pitch or roll.
	const FQuat PivotQuat = EffectivePivotActor->GetActorQuat();
	const FQuat OffsetQuat = RotationOffset.Quaternion();
	const FRotator TargetRotation = (PivotQuat * OffsetQuat).Rotator();

	if (Interpolator_T)
	{
		// Standard interpolator idiom across this plugin (matches AutoRotateNode
		// and LookAtNode soft-mode): re-seed Current ->live camera rotation,
		// Target -> this frame's resolved target, then advance one DeltaTime
		// step. The interpolator's OnReset gets both old and new (Current,
		// Target) pairs so velocity-aware variants can adapt when the target
		// shifts mid-flight (e.g. PivotActor turning continuously).
		Interpolator_T->Reset(OutCameraPose.Rotation, TargetRotation);
		OutCameraPose.Rotation = Interpolator_T->Run(DeltaTime);
	}
	else
	{
		// No interpolator authored. Snap to the target.
		OutCameraPose.Rotation = TargetRotation;
	}
}

void UComposableCameraPivotRotateNode::GetPinDeclarations_Implementation(
	TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("PivotActorSource");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraPivotRotateNode", "PivotActorSource", "Pivot Actor Source");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Enum;
		Pin.EnumType = StaticEnum<EComposableCameraActorInputSource>();
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = Pin.EnumType ? Pin.EnumType->GetNameStringByValue(static_cast<int64>(PivotActorSource)) : FString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraPivotRotateNode", "PivotActorSourceTip",
			"Selects whether PivotActor comes from the controller's controlled pawn or an explicit actor.");
		OutPins.Add(Pin);
	}

	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("PivotActor");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraPivotRotateNode", "PivotActor", "Pivot Actor");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Actor;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraPivotRotateNode", "PivotActorTip",
			"Actor whose world rotation drives the camera's target rotation each frame.");
		OutPins.Add(Pin);
	}

	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("RotationOffset");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraPivotRotateNode", "RotationOffset", "Rotation Offset");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Rotator;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = RotationOffset.ToString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraPivotRotateNode", "RotationOffsetTip",
			"Rotation offset composed onto the pivot rotation in the pivot's LOCAL frame "
			"(quaternion multiply). Set to zero to copy the pivot rotation exactly.");
		OutPins.Add(Pin);
	}

	// Note: `Interpolator` is an Instanced subobject. The base class's
	// AutoDeclareSubobjectPins automatically surfaces its inner properties as
	// "Interpolator.<Field>" pins; no manual declaration needed here.
}
