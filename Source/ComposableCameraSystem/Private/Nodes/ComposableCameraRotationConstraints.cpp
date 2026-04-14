// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraRotationConstraints.h"

#include "Kismet/KismetMathLibrary.h"
#include "Math/ComposableCameraMath.h"

void UComposableCameraRotationConstraints::OnTickNode_Implementation(float DeltaTime,
                                                                     const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	FRotator CurrentCameraRotation = CurrentCameraPose.Rotation;
	FVector2D WorldTargetYawRange { -180, 180 };
	FVector2D WorldTargetPitchRange { -90, 90 };
	
	if (bConstrainYaw)
	{
		double WorldPivotYaw = 0.;
		
		switch (ConstrainYawType)
		{
		case EComposableCameraRotationConstrainType::WorldSpace:
			WorldPivotYaw = 0.;
			break;
		case EComposableCameraRotationConstrainType::ActorSpace:
			if (ActorForYawConstrain.IsValid())
			{
				WorldPivotYaw = ActorForYawConstrain->GetActorRotation().Yaw;
			}
			else
			{
				WorldPivotYaw = 0.;
			}
			break;
		case EComposableCameraRotationConstrainType::VectorSpace:
			FRotator VectorSpaceRotation = UKismetMathLibrary::MakeRotFromX(VectorForYawConstrain);
			WorldPivotYaw = VectorSpaceRotation.Yaw;
			break;
		}
		
		double WorldSpaceYawLow =  WorldPivotYaw + YawRange[0];
		double WorldSpaceYawHigh =  WorldPivotYaw + YawRange[1];
		WorldTargetYawRange = { WorldSpaceYawLow, WorldSpaceYawHigh };
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
			if (ActorForPitchConstrain.IsValid())
			{
				WorldPivotPitch = ActorForPitchConstrain->GetActorRotation().Pitch;
			}
			else
			{
				WorldPivotPitch = 0.;
			}
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

	double WorldTargetYaw = FindTargetYawInRange(WorldCurrentYaw, WorldTargetYawRange);
	double WorldTargetPitch = FindTargetPitchInRange(WorldCurrentPitch, WorldTargetPitchRange);

	OutCameraPose.Rotation = FRotator(WorldTargetPitch, WorldTargetYaw, CurrentCameraRotation.Roll);
}

void UComposableCameraRotationConstraints::GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	FComposableCameraNodePinDeclaration PinDecl;

	// YawRange Input
	PinDecl.PinName = TEXT("YawRange");
	PinDecl.DisplayName = NSLOCTEXT("ComposableCameraRotationConstraints", "YawRange", "Yaw Range");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Vector2D;
	PinDecl.bRequired = false;
	PinDecl.Tooltip = NSLOCTEXT("ComposableCameraRotationConstraints", "YawRangeTip", "Yaw range in reference frame.");
	OutPins.Add(PinDecl);

	// PitchRange Input
	PinDecl.PinName = TEXT("PitchRange");
	PinDecl.DisplayName = NSLOCTEXT("ComposableCameraRotationConstraints", "PitchRange", "Pitch Range");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Vector2D;
	PinDecl.bRequired = false;
	PinDecl.Tooltip = NSLOCTEXT("ComposableCameraRotationConstraints", "PitchRangeTip", "Pitch range in reference frame.");
	OutPins.Add(PinDecl);

	// ActorForYawConstrain Input
	PinDecl.PinName = TEXT("ActorForYawConstrain");
	PinDecl.DisplayName = NSLOCTEXT("ComposableCameraRotationConstraints", "ActorForYawConstrain", "Actor For Yaw Constrain");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Actor;
	PinDecl.bRequired = false;
	PinDecl.Tooltip = NSLOCTEXT("ComposableCameraRotationConstraints", "ActorForYawConstrainTip", "Reference actor for yaw when ActorSpace.");
	OutPins.Add(PinDecl);

	// ActorForPitchConstrain Input
	PinDecl.PinName = TEXT("ActorForPitchConstrain");
	PinDecl.DisplayName = NSLOCTEXT("ComposableCameraRotationConstraints", "ActorForPitchConstrain", "Actor For Pitch Constrain");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Actor;
	PinDecl.bRequired = false;
	PinDecl.Tooltip = NSLOCTEXT("ComposableCameraRotationConstraints", "ActorForPitchConstrainTip", "Reference actor for pitch when ActorSpace.");
	OutPins.Add(PinDecl);
}


double UComposableCameraRotationConstraints::FindTargetYawInRange(const double WorldCurrentYaw,
                                                                  const FVector2D& WorldTargetYawRange)
{
	double NormalizedWorldCurrentYaw = ComposableCameraSystem::NormalizeYaw(WorldCurrentYaw);
	FVector2D NormalizedWorldTargetYawRange = {
		ComposableCameraSystem::NormalizeYaw(WorldTargetYawRange[0]),
		ComposableCameraSystem::NormalizeYaw(WorldTargetYawRange[1])
	};

	if (UKismetMathLibrary::InRange_FloatFloat(
		NormalizedWorldCurrentYaw, NormalizedWorldTargetYawRange[0], NormalizedWorldTargetYawRange[1]))
	{
		return NormalizedWorldCurrentYaw;
	}

	double DistanceToLow = FMath::Abs(ComposableCameraSystem::NormalizeYaw(NormalizedWorldCurrentYaw - NormalizedWorldTargetYawRange[0]));
	double DistanceToHigh = FMath::Abs(ComposableCameraSystem::NormalizeYaw(NormalizedWorldCurrentYaw - NormalizedWorldTargetYawRange[1]));

	return DistanceToLow < DistanceToHigh ? NormalizedWorldTargetYawRange[0] : NormalizedWorldTargetYawRange[1];
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