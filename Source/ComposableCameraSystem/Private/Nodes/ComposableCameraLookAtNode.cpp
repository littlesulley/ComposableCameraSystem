// Copyright 2026 Sulley. All Rights Reserved.

#include "Nodes/ComposableCameraLookAtNode.h"

#include "Cameras/ComposableCameraCameraBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/KismetMathLibrary.h"

#if !UE_BUILD_SHIPPING
#include "Debug/ComposableCameraViewportDebug.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"

namespace
{
	static TAutoConsoleVariable<int32> CVarShowLookAtGizmo(
		TEXT("CCS.Debug.Viewport.LookAt"),
		0,
		TEXT("Show LookAtNode gizmo (cyan sphere at the resolved look-at target).\n")
		TEXT("Requires `CCS.Debug.Viewport 1`."),
		ECVF_Default);
}
#endif

void UComposableCameraLookAtNode::OnInitialize_Implementation()
{
	Super::OnInitialize_Implementation();

	Interpolator_T = IsValid(SoftLookAtInterpolator) ? SoftLookAtInterpolator->BuildRotatorInterpolator() : nullptr;

	// Don't resolve the SkelMesh component here - LookAtActor can be driven
	// by an input pin and change every frame. Resolution happens lazily in
	// Tick when the active LookAtActor differs from `LastResolvedLookAtActor`.
	SkeletalMeshComponentForLookAtActor.Reset();
	LastResolvedLookAtActor.Reset();
}

namespace
{
	// Lazy-resolve the SkelMesh on `Actor` only when it differs from what the
	// cache last resolved against. Keeps Tick allocation-free in the common
	// case (LookAtActor stable across frames) while still picking up actor
	// swaps and component churn.
	static void ResolveSkelMeshForLookAtActor(
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

void UComposableCameraLookAtNode::OnTickNode_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	FVector CurrentLookAtPosition = FVector::ZeroVector;

	if (LookAtType == EComposableCameraLookAtType::ByPosition)
	{
		CurrentLookAtPosition = LookAtPosition;
	}
	else if (LookAtType == EComposableCameraLookAtType::ByActor)
	{
		AActor* EffectiveLookAtActor = ComposableCameraSystem::ResolveActorInput(
			LookAtActorSource, LookAtActor.Get(), GetOwningPlayerCameraManager(), this);

		// LookAtActor may have just been written by ResolveAllInputPins;
		// re-resolve the cached SkelMesh whenever the active actor changed.
		ResolveSkelMeshForLookAtActor(EffectiveLookAtActor, SkeletalMeshComponentForLookAtActor, LastResolvedLookAtActor);

		USkeletalMeshComponent* Comp = SkeletalMeshComponentForLookAtActor.Get();
		if (IsValid(Comp) && Comp->DoesSocketExist(LookAtSocket))
		{
			CurrentLookAtPosition = Comp->GetSocketLocation(LookAtSocket);
		}
		else if (IsValid(EffectiveLookAtActor))
		{
			CurrentLookAtPosition = EffectiveLookAtActor->GetActorLocation();
		}
	}

	if ((CurrentLookAtPosition - OutCameraPose.Position).SizeSquared() <= FMath::Square(KINDA_SMALL_NUMBER))
	{
		return;
	}

	FRotator ResultRotation = FRotator::ZeroRotator;
	
	if (LookAtConstraintType == EComposableCameraLookAtConstraintType::Hard)
	{
		ResultRotation = UKismetMathLibrary::FindLookAtRotation(OutCameraPose.Position, CurrentLookAtPosition);
	}
	else if (LookAtConstraintType == EComposableCameraLookAtConstraintType::Soft)
	{
		FRotator CurrentRotation = OutCameraPose.Rotation;
		FRotator TargetRotation = UKismetMathLibrary::FindLookAtRotation(OutCameraPose.Position, CurrentLookAtPosition);
		FRotator LookAtRotation = TargetRotation;
		FRotator DeltaRotation = (TargetRotation - CurrentRotation).GetNormalized();
		TargetRotation = (DeltaRotation * SoftLookAtWeight + CurrentRotation).GetNormalized();
		
		if (Interpolator_T)
		{
			Interpolator_T->Reset(CurrentRotation, TargetRotation);
			TargetRotation = Interpolator_T->Run(DeltaTime);
		}

		FVector TargetVector = TargetRotation.RotateVector(FVector::ForwardVector);
		FVector LookAtVector = LookAtRotation.RotateVector(FVector::ForwardVector);
		float Degree = UKismetMathLibrary::DegAcos(TargetVector.Dot(LookAtVector));
		
		if (Degree > SoftLookAtRange)
		{
			float RangeRatio = (Degree - SoftLookAtRange) / Degree;
			TargetRotation = ((LookAtRotation - TargetRotation).GetNormalized() * RangeRatio + TargetRotation).GetNormalized();
		}

		ResultRotation = TargetRotation;
	}

	OutCameraPose.Rotation = ResultRotation;
}

#if !UE_BUILD_SHIPPING
void UComposableCameraLookAtNode::DrawNodeDebug(UWorld* World, bool bViewerIsOutsideCamera) const
{
	if (!World) { return; }
	if (CVarShowLookAtGizmo.GetValueOnGameThread() == 0
		&& !FComposableCameraViewportDebug::ShouldShowAllNodeGizmos()) { return; }
	// Possessed play: target sphere only. A line from camera to target is
	// by definition along the view axis and all variants tried (thin line,
	// thick line, arrow, manual arrow, line+box) either vanish via view
	// alignment or get occluded by the mesh they pass through. The sphere
	// alone is the reliable gizmo in this mode.
	// F8 eject: draw a plain thin line from camera to target (viewer is
	// outside the camera, line is NOT view-aligned from this viewpoint, so
	// Thickness=0 + SDPG_Foreground reads fine).

	// Resolve current target position - mirrors OnTickNode's resolution chain.
	// Kept duplicated here so DrawNodeDebug stays side-effect-free and doesn't
	// need any cached state written by the tick path.
	FVector TargetPosition = FVector::ZeroVector;
	if (LookAtType == EComposableCameraLookAtType::ByPosition)
	{
		TargetPosition = LookAtPosition;
	}
	else if (LookAtType == EComposableCameraLookAtType::ByActor)
	{
		AActor* EffectiveLookAtActor = ComposableCameraSystem::ResolveActorInput(
			LookAtActorSource, LookAtActor.Get(), GetOwningPlayerCameraManager(), this);
		USkeletalMeshComponent* Comp = SkeletalMeshComponentForLookAtActor.Get();
		if (LastResolvedLookAtActor.Get() == EffectiveLookAtActor && IsValid(Comp) && Comp->DoesSocketExist(LookAtSocket))
		{
			TargetPosition = Comp->GetSocketLocation(LookAtSocket);
		}
		else if (IsValid(EffectiveLookAtActor))
		{
			TargetPosition = EffectiveLookAtActor->GetActorLocation();
		}
		else
		{
			return; // nothing resolvable to draw
		}
	}

	// DepthPriority=1 (SDPG_Foreground) draws above scene geometry - without
	// it, a target anchored on a bone socket would be occluded by the mesh.
	constexpr uint8 KForeground = 1;
	const FColor TargetColor = FComposableCameraViewportDebugColors::LookAt();
	FComposableCameraViewportDebug::DrawSolidDebugSphere(
		World, TargetPosition, /*Radius=*/7.5f, TargetColor,
		/*Alpha=*/110, /*Segments=*/12, KForeground, TEXT("LookAt"));

	if (bViewerIsOutsideCamera && OwningCamera)
	{
		DrawDebugLine(World, OwningCamera->GetCameraPose().Position, TargetPosition,
			TargetColor, /*bPersistentLines=*/false, /*LifeTime=*/-1.f, KForeground, /*Thickness=*/0.f);
	}
}
#endif

void UComposableCameraLookAtNode::GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	// LookAtType - selects whether the target is LookAtPosition or LookAtActor.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("LookAtType");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraLookAtNode", "LookAtType", "Look At Type");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Enum;
		Pin.EnumType = StaticEnum<EComposableCameraLookAtType>();
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = Pin.EnumType ? Pin.EnumType->GetNameStringByValue(static_cast<int64>(LookAtType)) : FString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraLookAtNode", "LookAtTypeTip",
			"Selects whether the camera looks at LookAtPosition or LookAtActor.");
		OutPins.Add(Pin);
	}

	// LookAtActorSource - only consumed when LookAtType == ByActor.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("LookAtActorSource");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraLookAtNode", "LookAtActorSource", "Look At Actor Source");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Enum;
		Pin.EnumType = StaticEnum<EComposableCameraActorInputSource>();
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = Pin.EnumType ? Pin.EnumType->GetNameStringByValue(static_cast<int64>(LookAtActorSource)) : FString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraLookAtNode", "LookAtActorSourceTip",
			"Selects whether LookAtActor comes from the controller's controlled pawn or an explicit actor.");
		OutPins.Add(Pin);
	}

	// LookAtPosition - target position (used when LookAtType == ByPosition).
	FComposableCameraNodePinDeclaration LookAtPositionPin;
	LookAtPositionPin.PinName = TEXT("LookAtPosition");
	LookAtPositionPin.DisplayName = NSLOCTEXT("ComposableCameraLookAtNode", "LookAtPosition", "Look At Position");
	LookAtPositionPin.Direction = EComposableCameraPinDirection::Input;
	LookAtPositionPin.PinType = EComposableCameraPinType::Vector3D;
	LookAtPositionPin.bRequired = false;
	LookAtPositionPin.bDefaultAsPin = false;
	LookAtPositionPin.Tooltip = NSLOCTEXT("ComposableCameraLookAtNode", "LookAtPositionTip", "World position to look at (when LookAtType is ByPosition).");
	OutPins.Add(LookAtPositionPin);

	// LookAtActor - target actor (used when LookAtType == ByActor).
	FComposableCameraNodePinDeclaration LookAtActorPin;
	LookAtActorPin.PinName = TEXT("LookAtActor");
	LookAtActorPin.DisplayName = NSLOCTEXT("ComposableCameraLookAtNode", "LookAtActor", "Look At Actor");
	LookAtActorPin.Direction = EComposableCameraPinDirection::Input;
	LookAtActorPin.PinType = EComposableCameraPinType::Actor;
	LookAtActorPin.bRequired = false;
	LookAtActorPin.bDefaultAsPin = false;
	LookAtActorPin.Tooltip = NSLOCTEXT("ComposableCameraLookAtNode", "LookAtActorTip", "Actor to look at (when LookAtType is ByActor).");
	OutPins.Add(LookAtActorPin);

	// LookAtSocket - skeletal-mesh socket on LookAtActor (optional).
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("LookAtSocket");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraLookAtNode", "LookAtSocket", "Look At Socket");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Name;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = LookAtSocket.ToString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraLookAtNode", "LookAtSocketTip",
			"Skeletal-mesh socket on LookAtActor used as the look-at target. If unresolved, the actor's location is used instead.");
		OutPins.Add(Pin);
	}

	// LookAtConstraintType - Hard vs Soft look-at.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("LookAtConstraintType");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraLookAtNode", "LookAtConstraintType", "Look At Constraint Type");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Enum;
		Pin.EnumType = StaticEnum<EComposableCameraLookAtConstraintType>();
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = Pin.EnumType ? Pin.EnumType->GetNameStringByValue(static_cast<int64>(LookAtConstraintType)) : FString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraLookAtNode", "LookAtConstraintTypeTip",
			"Hard locks the camera to the target; Soft allows player control around the look-at direction.");
		OutPins.Add(Pin);
	}

	// SoftLookAtRange - tolerance angle before pulling toward the target.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("SoftLookAtRange");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraLookAtNode", "SoftLookAtRange", "Soft Look At Range");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = FString::SanitizeFloat(SoftLookAtRange);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraLookAtNode", "SoftLookAtRangeTip",
			"Degrees of tolerance around the look-at direction before the camera is pulled back toward it (soft mode).");
		OutPins.Add(Pin);
	}

	// SoftLookAtWeight - how strongly the soft look-at pulls toward the target.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("SoftLookAtWeight");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraLookAtNode", "SoftLookAtWeight", "Soft Look At Weight");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = FString::SanitizeFloat(SoftLookAtWeight);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraLookAtNode", "SoftLookAtWeightTip",
			"The larger it is, the harder the camera will look at the target.");
		OutPins.Add(Pin);
	}
}
