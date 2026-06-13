// Copyright 2026 Sulley. All Rights Reserved.

#include "Nodes/ComposableCameraRotationConstraints.h"

#include "GameFramework/Actor.h"
#include "Kismet/KismetMathLibrary.h"
#include "Math/ComposableCameraMath.h"

namespace
{
	FRotator ReferenceRotationFromActor(AActor* Actor)
	{
		return IsValid(Actor)
			? UKismetMathLibrary::MakeRotFromX(Actor->GetActorForwardVector())
			: FRotator::ZeroRotator;
	}
}

void UComposableCameraRotationConstraints::OnTickNode_Implementation(float DeltaTime,
                                                                     const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	FRotator CurrentCameraRotation = CurrentCameraPose.Rotation;
	double WorldPivotYaw = 0.;
	FVector2D WorldTargetPitchRange { -90, 90 };
	
	if (bConstrainYaw)
	{
		switch (ConstrainYawType)
		{
		case EComposableCameraRotationConstrainType::WorldSpace:
			WorldPivotYaw = 0.;
			break;
		case EComposableCameraRotationConstrainType::ActorSpace:
			WorldPivotYaw = ReferenceRotationFromActor(ComposableCameraSystem::ResolveActorInput(
				ActorForYawConstrainSource,
				ActorForYawConstrain.Get(),
				GetOwningPlayerCameraManager(),
				this)).Yaw;
			break;
		case EComposableCameraRotationConstrainType::VectorSpace:
			FRotator VectorSpaceRotation = UKismetMathLibrary::MakeRotFromX(VectorForYawConstrain);
			WorldPivotYaw = VectorSpaceRotation.Yaw;
			break;
		}
	}

	if (bConstrainPitch)
	{
		double WorldPivotPitch = 0.;

		switch (ConstrainPitchType)
		{
		case EComposableCameraRotationConstrainType::WorldSpace:
			WorldPivotPitch = 0.;
			break;
		case EComposableCameraRotationConstrainType::ActorSpace:
			WorldPivotPitch = ReferenceRotationFromActor(ComposableCameraSystem::ResolveActorInput(
				ActorForPitchConstrainSource,
				ActorForPitchConstrain.Get(),
				GetOwningPlayerCameraManager(),
				this)).Pitch;
			break;
		case EComposableCameraRotationConstrainType::VectorSpace:
			FRotator VectorSpaceRotation = UKismetMathLibrary::MakeRotFromX(VectorForPitchConstrain);
			WorldPivotPitch = VectorSpaceRotation.Pitch;
			break;
		}

		double WorldSpacePitchLow =  WorldPivotPitch + PitchRange[0];
		double WorldSpacePitchHigh =  WorldPivotPitch + PitchRange[1];
		WorldTargetPitchRange = { WorldSpacePitchLow, WorldSpacePitchHigh };
	}

	double WorldCurrentYaw = CurrentCameraRotation.Yaw;
	double WorldCurrentPitch = CurrentCameraRotation.Pitch;

	double WorldTargetYaw = bConstrainYaw
		? FindTargetYawInRange(WorldCurrentYaw, WorldPivotYaw, YawRange)
		: WorldCurrentYaw;
	double WorldTargetPitch = bConstrainPitch
		? FindTargetPitchInRange(WorldCurrentPitch, WorldTargetPitchRange)
		: WorldCurrentPitch;

	OutCameraPose.Rotation = FRotator(WorldTargetPitch, WorldTargetYaw, CurrentCameraRotation.Roll);
}

void UComposableCameraRotationConstraints::GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	FComposableCameraNodePinDeclaration PinDecl;

	// bConstrainYaw Input
	PinDecl = {};
	PinDecl.PinName = TEXT("bConstrainYaw");
	PinDecl.DisplayName = NSLOCTEXT("ComposableCameraRotationConstraints", "bConstrainYaw", "Constrain Yaw");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Bool;
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.DefaultValueString = bConstrainYaw ? TEXT("true") : TEXT("false");
	PinDecl.Tooltip = NSLOCTEXT("ComposableCameraRotationConstraints", "bConstrainYawTip", "Enable yaw constraint.");
	OutPins.Add(PinDecl);

	// ConstrainYawType Input
	PinDecl = {};
	PinDecl.PinName = TEXT("ConstrainYawType");
	PinDecl.DisplayName = NSLOCTEXT("ComposableCameraRotationConstraints", "ConstrainYawType", "Constrain Yaw Type");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Enum;
	PinDecl.EnumType = StaticEnum<EComposableCameraRotationConstrainType>();
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.DefaultValueString = PinDecl.EnumType ? PinDecl.EnumType->GetNameStringByValue(static_cast<int64>(ConstrainYawType)) : FString();
	PinDecl.Tooltip = NSLOCTEXT("ComposableCameraRotationConstraints", "ConstrainYawTypeTip",
		"Selects the reference frame for the yaw constraint - WorldSpace, ActorSpace, or VectorSpace.");
	OutPins.Add(PinDecl);

	// YawRange Input
	PinDecl = {};
	PinDecl.PinName = TEXT("YawRange");
	PinDecl.DisplayName = NSLOCTEXT("ComposableCameraRotationConstraints", "YawRange", "Yaw Range");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Vector2D;
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.DefaultValueString = YawRange.ToString();
	PinDecl.Tooltip = NSLOCTEXT("ComposableCameraRotationConstraints", "YawRangeTip", "Yaw range in reference frame.");
	OutPins.Add(PinDecl);

	// VectorForYawConstrain Input
	PinDecl = {};
	PinDecl.PinName = TEXT("VectorForYawConstrain");
	PinDecl.DisplayName = NSLOCTEXT("ComposableCameraRotationConstraints", "VectorForYawConstrain", "Vector For Yaw Constrain");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Vector3D;
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.DefaultValueString = VectorForYawConstrain.ToString();
	PinDecl.Tooltip = NSLOCTEXT("ComposableCameraRotationConstraints", "VectorForYawConstrainTip", "Forward vector for the yaw reference frame when ConstrainYawType is VectorSpace.");
	OutPins.Add(PinDecl);

	// ActorForYawConstrainSource Input
	PinDecl = {};
	PinDecl.PinName = TEXT("ActorForYawConstrainSource");
	PinDecl.DisplayName = NSLOCTEXT("ComposableCameraRotationConstraints", "ActorForYawConstrainSource", "Actor For Yaw Constrain Source");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Enum;
	PinDecl.EnumType = StaticEnum<EComposableCameraActorInputSource>();
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.DefaultValueString = PinDecl.EnumType ? PinDecl.EnumType->GetNameStringByValue(static_cast<int64>(ActorForYawConstrainSource)) : FString();
	PinDecl.Tooltip = NSLOCTEXT("ComposableCameraRotationConstraints", "ActorForYawConstrainSourceTip",
		"Selects whether the yaw reference actor comes from the controller's controlled pawn or an explicit actor.");
	OutPins.Add(PinDecl);

	// ActorForYawConstrain Input
	PinDecl = {};
	PinDecl.PinName = TEXT("ActorForYawConstrain");
	PinDecl.DisplayName = NSLOCTEXT("ComposableCameraRotationConstraints", "ActorForYawConstrain", "Actor For Yaw Constrain");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Actor;
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.Tooltip = NSLOCTEXT("ComposableCameraRotationConstraints", "ActorForYawConstrainTip", "Reference actor for yaw when ActorSpace.");
	OutPins.Add(PinDecl);

	// bConstrainPitch Input
	PinDecl = {};
	PinDecl.PinName = TEXT("bConstrainPitch");
	PinDecl.DisplayName = NSLOCTEXT("ComposableCameraRotationConstraints", "bConstrainPitch", "Constrain Pitch");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Bool;
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.DefaultValueString = bConstrainPitch ? TEXT("true") : TEXT("false");
	PinDecl.Tooltip = NSLOCTEXT("ComposableCameraRotationConstraints", "bConstrainPitchTip", "Enable pitch constraint.");
	OutPins.Add(PinDecl);

	// ConstrainPitchType Input
	PinDecl = {};
	PinDecl.PinName = TEXT("ConstrainPitchType");
	PinDecl.DisplayName = NSLOCTEXT("ComposableCameraRotationConstraints", "ConstrainPitchType", "Constrain Pitch Type");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Enum;
	PinDecl.EnumType = StaticEnum<EComposableCameraRotationConstrainType>();
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.DefaultValueString = PinDecl.EnumType ? PinDecl.EnumType->GetNameStringByValue(static_cast<int64>(ConstrainPitchType)) : FString();
	PinDecl.Tooltip = NSLOCTEXT("ComposableCameraRotationConstraints", "ConstrainPitchTypeTip",
		"Selects the reference frame for the pitch constraint - WorldSpace, ActorSpace, or VectorSpace.");
	OutPins.Add(PinDecl);

	// PitchRange Input
	PinDecl = {};
	PinDecl.PinName = TEXT("PitchRange");
	PinDecl.DisplayName = NSLOCTEXT("ComposableCameraRotationConstraints", "PitchRange", "Pitch Range");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Vector2D;
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.DefaultValueString = PitchRange.ToString();
	PinDecl.Tooltip = NSLOCTEXT("ComposableCameraRotationConstraints", "PitchRangeTip", "Pitch range in reference frame.");
	OutPins.Add(PinDecl);

	// VectorForPitchConstrain Input
	PinDecl = {};
	PinDecl.PinName = TEXT("VectorForPitchConstrain");
	PinDecl.DisplayName = NSLOCTEXT("ComposableCameraRotationConstraints", "VectorForPitchConstrain", "Vector For Pitch Constrain");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Vector3D;
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.DefaultValueString = VectorForPitchConstrain.ToString();
	PinDecl.Tooltip = NSLOCTEXT("ComposableCameraRotationConstraints", "VectorForPitchConstrainTip", "Forward vector for the pitch reference frame when ConstrainPitchType is VectorSpace.");
	OutPins.Add(PinDecl);

	// ActorForPitchConstrainSource Input
	PinDecl = {};
	PinDecl.PinName = TEXT("ActorForPitchConstrainSource");
	PinDecl.DisplayName = NSLOCTEXT("ComposableCameraRotationConstraints", "ActorForPitchConstrainSource", "Actor For Pitch Constrain Source");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Enum;
	PinDecl.EnumType = StaticEnum<EComposableCameraActorInputSource>();
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.DefaultValueString = PinDecl.EnumType ? PinDecl.EnumType->GetNameStringByValue(static_cast<int64>(ActorForPitchConstrainSource)) : FString();
	PinDecl.Tooltip = NSLOCTEXT("ComposableCameraRotationConstraints", "ActorForPitchConstrainSourceTip",
		"Selects whether the pitch reference actor comes from the controller's controlled pawn or an explicit actor.");
	OutPins.Add(PinDecl);

	// ActorForPitchConstrain Input
	PinDecl = {};
	PinDecl.PinName = TEXT("ActorForPitchConstrain");
	PinDecl.DisplayName = NSLOCTEXT("ComposableCameraRotationConstraints", "ActorForPitchConstrain", "Actor For Pitch Constrain");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Actor;
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.Tooltip = NSLOCTEXT("ComposableCameraRotationConstraints", "ActorForPitchConstrainTip", "Reference actor for pitch when ActorSpace.");
	OutPins.Add(PinDecl);
}


double UComposableCameraRotationConstraints::FindTargetYawInRange(const double WorldCurrentYaw,
                                                                  const double WorldPivotYaw,
                                                                  const FVector2D& PivotSpaceYawRange)
{
	const double RangeLow = PivotSpaceYawRange[0];
	const double RangeHigh = PivotSpaceYawRange[1];
	if (RangeHigh - RangeLow >= 360. - UE_DOUBLE_KINDA_SMALL_NUMBER)
	{
		return ComposableCameraSystem::NormalizeYaw(WorldCurrentYaw);
	}

	const double PivotSpaceCurrentYaw = FMath::FindDeltaAngleDegrees(WorldPivotYaw, WorldCurrentYaw);
	if (UKismetMathLibrary::InRange_FloatFloat(PivotSpaceCurrentYaw, RangeLow, RangeHigh))
	{
		return ComposableCameraSystem::NormalizeYaw(WorldCurrentYaw);
	}

	return ComposableCameraSystem::GetClosestAngleDegree(
		static_cast<float>(WorldCurrentYaw),
		static_cast<float>(WorldPivotYaw + RangeLow),
		static_cast<float>(WorldPivotYaw + RangeHigh));
}

double UComposableCameraRotationConstraints::FindTargetPitchInRange(const double WorldCurrentPitch,
	const FVector2D& WorldTargetPitchRange)
{
	if (UKismetMathLibrary::InRange_FloatFloat(
		WorldCurrentPitch, WorldTargetPitchRange[0], WorldTargetPitchRange[1]))
	{
		return WorldCurrentPitch;
	}

	return WorldCurrentPitch > WorldTargetPitchRange[1] ?  WorldTargetPitchRange[1] : WorldTargetPitchRange[0];
}
