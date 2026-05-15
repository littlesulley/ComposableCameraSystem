// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraPivotLookAheadNode.h"

#include "GameFramework/Actor.h"
#include "Utils/ComposableCameraActorInputSource.h"

#if !UE_BUILD_SHIPPING
#include "Debug/ComposableCameraViewportDebug.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"

namespace
{
	static TAutoConsoleVariable<int32> CVarShowPivotLookAheadGizmo(
		TEXT("CCS.Debug.Viewport.PivotLookAhead"),
		0,
		TEXT("Show PivotLookAheadNode gizmo (orange sphere at the predicted pivot).\n")
		TEXT("Requires `CCS.Debug.Viewport 1`."),
		ECVF_Default);
}
#endif

void UComposableCameraPivotLookAheadNode::OnInitialize_Implementation()
{
	Super::OnInitialize_Implementation();

	LastPivotPosition = FVector::ZeroVector;
	SmoothedVelocity = FVector::ZeroVector;
	VelocitySmoothingVelocity = FVector::ZeroVector;
	bHasLastPivotPosition = false;

#if !UE_BUILD_SHIPPING
	LastOutputPivotPosition = FVector::ZeroVector;
#endif
}

void UComposableCameraPivotLookAheadNode::OnFirstTickNode_Implementation()
{
	LastPivotPosition = PivotPosition;
	bHasLastPivotPosition = true;
}

void UComposableCameraPivotLookAheadNode::OnTickNode_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	FVector ResolvedVelocity = FVector::ZeroVector;
	const AActor* EffectiveVelocityActor = ComposableCameraSystem::ResolveActorInput(
		VelocityActorSource, VelocityActor.Get(), GetOwningPlayerCameraManager(), this);

	if (IsValid(EffectiveVelocityActor))
	{
		ResolvedVelocity = EffectiveVelocityActor->GetVelocity();
	}
	else if (bHasLastPivotPosition && DeltaTime > UE_SMALL_NUMBER)
	{
		ResolvedVelocity = (PivotPosition - LastPivotPosition) / DeltaTime;
	}

	if (VelocityDampingTime > UE_SMALL_NUMBER)
	{
		double TargetVelocity = 0.0;
		FMath::CriticallyDampedSmoothing(
			SmoothedVelocity.X, VelocitySmoothingVelocity.X,
			ResolvedVelocity.X, TargetVelocity,
			DeltaTime, VelocityDampingTime);
		TargetVelocity = 0.0;
		FMath::CriticallyDampedSmoothing(
			SmoothedVelocity.Y, VelocitySmoothingVelocity.Y,
			ResolvedVelocity.Y, TargetVelocity,
			DeltaTime, VelocityDampingTime);
		TargetVelocity = 0.0;
		FMath::CriticallyDampedSmoothing(
			SmoothedVelocity.Z, VelocitySmoothingVelocity.Z,
			ResolvedVelocity.Z, TargetVelocity,
			DeltaTime, VelocityDampingTime);
	}
	else
	{
		SmoothedVelocity = ResolvedVelocity;
		VelocitySmoothingVelocity = FVector::ZeroVector;
	}

	const FVector PredictedPivot = PivotPosition + SmoothedVelocity * FMath::Max(0.f, LookAheadTime);
	SetOutputPinValue<FVector>(TEXT("PivotPosition"), PredictedPivot);

	LastPivotPosition = PivotPosition;
	bHasLastPivotPosition = true;

#if !UE_BUILD_SHIPPING
	LastOutputPivotPosition = PredictedPivot;
#endif
}

#if !UE_BUILD_SHIPPING
void UComposableCameraPivotLookAheadNode::DrawNodeDebug(UWorld* World, bool /*bViewerIsOutsideCamera*/) const
{
	if (!World) { return; }
	if (CVarShowPivotLookAheadGizmo.GetValueOnGameThread() == 0
		&& !FComposableCameraViewportDebug::ShouldShowAllNodeGizmos()) { return; }

	constexpr uint8 KForeground = 1;
	FComposableCameraViewportDebug::DrawSolidDebugSphere(
		World, LastOutputPivotPosition, /*Radius=*/9.f, FColor(255, 128, 0),
		/*Alpha=*/105, /*Segments=*/12, KForeground);
}
#endif

void UComposableCameraPivotLookAheadNode::GetPinDeclarations_Implementation(
	TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("PivotPosition");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraPivotLookAheadNode", "PivotPositionIn", "Pivot Position");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Vector3D;
		Pin.bRequired = true;
		Pin.DefaultValueString = PivotPosition.ToString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraPivotLookAheadNode", "PivotPositionInTip",
			"Pivot position to project forward.");
		OutPins.Add(Pin);
	}

	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("VelocityActorSource");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraPivotLookAheadNode", "VelocityActorSource", "Velocity Actor Source");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Enum;
		Pin.EnumType = StaticEnum<EComposableCameraActorInputSource>();
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = Pin.EnumType ? Pin.EnumType->GetNameStringByValue(static_cast<int64>(VelocityActorSource)) : FString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraPivotLookAheadNode", "VelocityActorSourceTip",
			"Selects whether VelocityActor comes from the controller's controlled pawn or an explicit actor.");
		OutPins.Add(Pin);
	}

	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("VelocityActor");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraPivotLookAheadNode", "VelocityActor", "Velocity Actor");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Actor;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraPivotLookAheadNode", "VelocityActorTip",
			"Actor whose velocity drives look-ahead. If unresolved, pivot delta velocity is used.");
		OutPins.Add(Pin);
	}

	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("LookAheadTime");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraPivotLookAheadNode", "LookAheadTime", "Look Ahead Time");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = FString::SanitizeFloat(LookAheadTime);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraPivotLookAheadNode", "LookAheadTimeTip",
			"Seconds into the future to project the pivot using the resolved velocity.");
		OutPins.Add(Pin);
	}

	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("VelocityDampingTime");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraPivotLookAheadNode", "VelocityDampingTime", "Velocity Damping Time");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = FString::SanitizeFloat(VelocityDampingTime);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraPivotLookAheadNode", "VelocityDampingTimeTip",
			"Time used to smooth velocity changes. Zero applies the resolved velocity immediately.");
		OutPins.Add(Pin);
	}

	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("PivotPosition");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraPivotLookAheadNode", "PivotPositionOut", "Pivot Position");
		Pin.Direction = EComposableCameraPinDirection::Output;
		Pin.PinType = EComposableCameraPinType::Vector3D;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraPivotLookAheadNode", "PivotPositionOutTip",
			"Predicted pivot position after applying velocity look-ahead.");
		OutPins.Add(Pin);
	}
}
