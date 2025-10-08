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

void UComposableCameraRotationConstraints::ReceiveInitializerNode(UComposableCameraCameraNodeBase* Initializer)
{
	if (UComposableCameraRotationConstraints* CastedInitializer = Cast<UComposableCameraRotationConstraints>(Initializer))
	{
		bConstrainYaw = CastedInitializer->bConstrainYaw;
		YawRange = CastedInitializer->YawRange;
		ConstrainYawType = CastedInitializer->ConstrainYawType;
		ActorForYawConstrain = CastedInitializer->ActorForYawConstrain;
		VectorForYawConstrain = CastedInitializer->VectorForYawConstrain;
		bConstrainPitch = CastedInitializer->bConstrainPitch;
		PitchRange = CastedInitializer->PitchRange;
		ConstrainPitchType = CastedInitializer->ConstrainPitchType;
		ActorForPitchConstrain = CastedInitializer->ActorForPitchConstrain;
		VectorForPitchConstrain = CastedInitializer->VectorForPitchConstrain;
	}
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