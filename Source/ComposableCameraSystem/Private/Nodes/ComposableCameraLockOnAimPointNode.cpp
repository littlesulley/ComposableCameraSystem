// Copyright 2026 Sulley. All Rights Reserved.

#include "Nodes/ComposableCameraLockOnAimPointNode.h"

#include "GameFramework/Actor.h"

#if !UE_BUILD_SHIPPING
#include "Debug/ComposableCameraDebugDrawSink.h"
#include "Debug/ComposableCameraViewportDebug.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"

namespace
{
	static TAutoConsoleVariable<int32> CVarShowLockOnAimPointGizmo(
		TEXT("CCS.Debug.Viewport.LockOnAimPoint"),
		0,
		TEXT("Show LockOnAimPointNode gizmo (blue sphere at the stable virtual aim point).\n")
		TEXT("Requires `CCS.Debug.Viewport 1`."),
		ECVF_Default);
}
#endif

namespace
{
	FVector ResolveLockOnAimPointSource(
		EComposableCameraLockOnAimPointSource Source,
		const FVector& WorldPosition,
		EComposableCameraActorInputSource ActorSource,
		AActor* ExplicitActor,
		float WorldUpOffset,
		const AComposableCameraPlayerCameraManager* PlayerCameraManager,
		const UObject* WorldContextObject)
	{
		if (Source == EComposableCameraLockOnAimPointSource::ActorPosition)
		{
			AActor* Actor = ComposableCameraSystem::ResolveActorInput(
				ActorSource, ExplicitActor, PlayerCameraManager, WorldContextObject);
			if (IsValid(Actor))
			{
				return Actor->GetActorLocation() + FVector::UpVector * WorldUpOffset;
			}
		}

		return WorldPosition;
	}
}

void UComposableCameraLockOnAimPointNode::OnInitialize_Implementation()
{
	Super::OnInitialize_Implementation();
	AimPointState = {};

#if !UE_BUILD_SHIPPING
	LastRawAimPosition = AimWorldPosition;
	LastOutputPivotPosition = AimWorldPosition;
	bLastAppliedCorrection = false;
#endif
}

void UComposableCameraLockOnAimPointNode::OnTickNode_Implementation(
	float DeltaTime,
	const FComposableCameraPose& /*CurrentCameraPose*/,
	FComposableCameraPose& OutCameraPose)
{
	const FVector ResolvedFollowPosition = ResolveLockOnAimPointSource(
		FollowSource,
		FollowWorldPosition,
		FollowActorSource,
		FollowActor.Get(),
		FollowWorldUpOffset,
		GetOwningPlayerCameraManager(),
		this);

	const FVector ResolvedAimPosition = ResolveLockOnAimPointSource(
		AimSource,
		AimWorldPosition,
		AimActorSource,
		AimActor.Get(),
		AimWorldUpOffset,
		GetOwningPlayerCameraManager(),
		this);

	const FVector StableAimPosition = ComposableCameraSystem::ComputeLockOnAimPoint(
		ResolvedFollowPosition,
		ResolvedAimPosition,
		OutCameraPose.Position,
		OutCameraPose.Rotation.Vector(),
		Radius,
		Weights,
		PitchRange,
		AimPointState,
		DeltaTime,
		BlendOutTime);

	SetOutputPinValue<FVector>(TEXT("PivotPosition"), StableAimPosition);

#if !UE_BUILD_SHIPPING
	LastRawAimPosition = ResolvedAimPosition;
	LastOutputPivotPosition = StableAimPosition;
	bLastAppliedCorrection = !StableAimPosition.Equals(ResolvedAimPosition, KINDA_SMALL_NUMBER);
#endif
}

void UComposableCameraLockOnAimPointNode::GetPinDeclarations_Implementation(
	TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("FollowSource");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraLockOnAimPointNode", "FollowSource", "Follow Source");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Enum;
		Pin.EnumType = StaticEnum<EComposableCameraLockOnAimPointSource>();
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = Pin.EnumType ? Pin.EnumType->GetNameStringByValue(static_cast<int64>(FollowSource)) : FString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraLockOnAimPointNode", "FollowSourceTip",
			"Selects whether the follow point comes from FollowWorldPosition or FollowActor.");
		OutPins.Add(Pin);
	}

	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("FollowActorSource");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraLockOnAimPointNode", "FollowActorSource", "Follow Actor Source");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Enum;
		Pin.EnumType = StaticEnum<EComposableCameraActorInputSource>();
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = Pin.EnumType ? Pin.EnumType->GetNameStringByValue(static_cast<int64>(FollowActorSource)) : FString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraLockOnAimPointNode", "FollowActorSourceTip",
			"Selects whether FollowActor comes from the controller's controlled pawn or an explicit actor.");
		OutPins.Add(Pin);
	}

	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("FollowWorldPosition");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraLockOnAimPointNode", "FollowWorldPosition", "Follow World Position");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Vector3D;
		Pin.bRequired = false;
		Pin.DefaultValueString = FollowWorldPosition.ToString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraLockOnAimPointNode", "FollowWorldPositionTip",
			"Follow point in world space. Used when FollowSource == WorldPosition.");
		OutPins.Add(Pin);
	}

	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("FollowActor");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraLockOnAimPointNode", "FollowActor", "Follow Actor");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Actor;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraLockOnAimPointNode", "FollowActorTip",
			"Actor whose world location supplies the follow point. Used when FollowSource == ActorPosition.");
		OutPins.Add(Pin);
	}

	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("FollowWorldUpOffset");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraLockOnAimPointNode", "FollowWorldUpOffset", "Follow World Up Offset");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = FString::SanitizeFloat(FollowWorldUpOffset);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraLockOnAimPointNode", "FollowWorldUpOffsetTip",
			"World-up offset added to FollowActor->GetActorLocation(). Used when FollowSource == ActorPosition.");
		OutPins.Add(Pin);
	}

	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("AimSource");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraLockOnAimPointNode", "AimSource", "Aim Source");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Enum;
		Pin.EnumType = StaticEnum<EComposableCameraLockOnAimPointSource>();
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = Pin.EnumType ? Pin.EnumType->GetNameStringByValue(static_cast<int64>(AimSource)) : FString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraLockOnAimPointNode", "AimSourceTip",
			"Selects whether the aim point comes from AimWorldPosition or AimActor.");
		OutPins.Add(Pin);
	}

	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("AimActorSource");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraLockOnAimPointNode", "AimActorSource", "Aim Actor Source");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Enum;
		Pin.EnumType = StaticEnum<EComposableCameraActorInputSource>();
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = Pin.EnumType ? Pin.EnumType->GetNameStringByValue(static_cast<int64>(AimActorSource)) : FString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraLockOnAimPointNode", "AimActorSourceTip",
			"Selects whether AimActor comes from the controller's controlled pawn or an explicit actor.");
		OutPins.Add(Pin);
	}

	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("AimWorldPosition");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraLockOnAimPointNode", "AimWorldPosition", "Aim World Position");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Vector3D;
		Pin.bRequired = false;
		Pin.DefaultValueString = AimWorldPosition.ToString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraLockOnAimPointNode", "AimWorldPositionTip",
			"Raw aim point in world space. Used when AimSource == WorldPosition.");
		OutPins.Add(Pin);
	}

	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("AimActor");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraLockOnAimPointNode", "AimActor", "Aim Actor");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Actor;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraLockOnAimPointNode", "AimActorTip",
			"Actor whose world location supplies the raw aim point. Used when AimSource == ActorPosition.");
		OutPins.Add(Pin);
	}

	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("AimWorldUpOffset");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraLockOnAimPointNode", "AimWorldUpOffset", "Aim World Up Offset");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = FString::SanitizeFloat(AimWorldUpOffset);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraLockOnAimPointNode", "AimWorldUpOffsetTip",
			"World-up offset added to AimActor->GetActorLocation(). Used when AimSource == ActorPosition.");
		OutPins.Add(Pin);
	}

	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("Radius");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraLockOnAimPointNode", "Radius", "Radius");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = FString::SanitizeFloat(Radius);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraLockOnAimPointNode", "RadiusTip",
			"Minimum horizontal projected distance used before the stable aim point correction activates.");
		OutPins.Add(Pin);
	}

	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("PitchRange");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraLockOnAimPointNode", "PitchRange", "Pitch Range");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Vector2D;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = PitchRange.ToString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraLockOnAimPointNode", "PitchRangeTip",
			"Min/max pitch in degrees used by the pitch-preserving term while inside Radius. Current pitch inside this range is used directly; outside it is clamped to the nearest boundary.");
		OutPins.Add(Pin);
	}

	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("BlendOutTime");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraLockOnAimPointNode", "BlendOutTime", "Blend Out Time");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = FString::SanitizeFloat(BlendOutTime);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraLockOnAimPointNode", "BlendOutTimeTip",
			"Seconds used to fade the correction offset back to zero after leaving Radius.");
		OutPins.Add(Pin);
	}

	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("Weights");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraLockOnAimPointNode", "Weights", "Weights");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Vector3D;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = Weights.ToString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraLockOnAimPointNode", "WeightsTip",
			"Blend weights for PitchAddition, CameraToAimAddition, and CameraForwardAddition.");
		OutPins.Add(Pin);
	}

	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("PivotPosition");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraLockOnAimPointNode", "PivotPosition", "Pivot Position");
		Pin.Direction = EComposableCameraPinDirection::Output;
		Pin.PinType = EComposableCameraPinType::Vector3D;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraLockOnAimPointNode", "PivotPositionTip",
			"Stable virtual aim point to feed into ScreenSpacePivot.PivotWorldPosition.");
		OutPins.Add(Pin);
	}
}

#if !UE_BUILD_SHIPPING
void UComposableCameraLockOnAimPointNode::DrawNodeDebug(FComposableCameraDebugDrawSink& Draw, bool bViewerIsOutsideCamera) const
{
	if (CVarShowLockOnAimPointGizmo.GetValueOnGameThread() == 0
		&& !FComposableCameraViewportDebug::ShouldShowAllNodeGizmos()
		&& !Draw.ShouldForceDrawAllNodeGizmos()) { return; }

	constexpr uint8 KForeground = 1;
	const FColor AimPointColor = FComposableCameraViewportDebugColors::LockOnAimPoint();
	Draw.DrawSphere(LastOutputPivotPosition, /*Radius=*/8.f, AimPointColor,
		/*Alpha=*/100, KForeground, /*bSolid=*/true);

	if (bViewerIsOutsideCamera && bLastAppliedCorrection)
	{
		Draw.DrawLine(LastRawAimPosition, LastOutputPivotPosition, AimPointColor, /*Thickness=*/0.f, KForeground);
	}
}
#endif
