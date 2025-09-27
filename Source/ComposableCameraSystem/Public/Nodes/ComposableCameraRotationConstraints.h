// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "ComposableCameraRotationConstraints.generated.h"

class UComposableCameraInterpolatorBase;

UENUM()
enum class EComposableCameraRotationConstrainType : uint8
{
    // Constrain rotation in world space.
	WorldSpace,
	
	// Constrain rotation in a given actor's space.
	ActorSpace,
	
	// Constrain rotation based on a given vector.
	VectorSpace
};

/**
 * Node for constraining rotation's yaw or pitch. \n
 *  @ InputParameter bConstrainYaw: Whether to enable yaw constraint. \n
 *  @ InputParameter ConstrainYawType: Constrain yaw type, choose between WorldSpace, ActorSpace and VectorSpace. \n
 *  @ InputParameter ActorForYawConstrain: Reference actor when ActorSpace is used. Its transform will be used as the reference frame. \n
 *  @ InputParameter VectorForYawConstrain: Reference vector when VectorSpace is used. It will serve as the forward vector of the reference frame. \n
 *  @ InputParameter YawRange: Yaw range in the reference frame. Use the world space, actor space or vector space as the reference frame. \n
 *  @ InputParameter bConstrainPitch: Whether to enable pitch constraint. \n
 *  @ InputParameter ConstrainPitchType: Constrain pitch type, choose between WorldSpace, ActorSpace and VectorSpace. \n
 *  @ InputParameter ActorForPitchConstrain: Reference actor when ActorSpace is used. Its transform will be used as the reference frame. \n
 *  @ InputParameter VectorForPitchConstrain: Reference vector when VectorSpace is used. It will serve as the forward vector of the reference frame. \n
 *  @ InputParameter PitchRange: Pitch range in the reference frame. Use the world space, actor space or vector space as the reference frame. 
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraRotationConstraints : public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;

public:
	// Whether to enable yaw constraint.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	bool bConstrainYaw { false };

	// Constrain yaw type, choose between WorldSpace, ActorSpace and VectorSpace.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraRotationConstrainType ConstrainYawType { EComposableCameraRotationConstrainType::WorldSpace };

	// Reference actor when ActorSpace is used. Its transform will be used as the reference frame.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "ConstrainYawType == EComposableCameraRotationConstrainType::ActorSpace"))
	TSoftObjectPtr<AActor> ActorForYawConstrain { nullptr };

	// Reference vector when VectorSpace is used. It will serve as the forward vector of the reference frame.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "ConstrainYawType == EComposableCameraRotationConstrainType::VectorSpace"))
	FVector VectorForYawConstrain { FVector::ForwardVector };

	// Yaw range in the reference frame. Use the world space, actor space or vector space as the reference frame.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	FVector2D YawRange { FVector2D {-180., 180.} };

	// Whether to enable pitch constraint.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	bool bConstrainPitch { true };

	// Constrain pitch type, choose between WorldSpace, ActorSpace and VectorSpace.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraRotationConstrainType ConstrainPitchType { EComposableCameraRotationConstrainType::WorldSpace };

	// Reference actor when ActorSpace is used. Its transform will be used as the reference frame.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "ConstrainPitchType == EComposableCameraRotationConstrainType::ActorSpace"))
	TSoftObjectPtr<AActor> ActorForPitchConstrain { nullptr };

	// Reference vector when VectorSpace is used. It will serve as the forward vector of the reference frame.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "ConstrainPitchType == EComposableCameraRotationConstrainType::VectorSpace"))
	FVector VectorForPitchConstrain { FVector::ForwardVector };

	// Pitch range in the reference frame. Use the world space, actor space or vector space as the reference frame.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	FVector2D PitchRange { FVector2D {-70., 70.} };

private:
	double FindTargetYawInRange(const double WorldCurrentYaw, const FVector2D& Vector2);
	double FindTargetPitchInRange(const double WorldCurrentPitch, const FVector2D& Vector2);
};
