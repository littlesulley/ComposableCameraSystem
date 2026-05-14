// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "Utils/ComposableCameraActorInputSource.h"
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
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (ToolTip = "Constrains camera yaw and pitch rotation within specified ranges."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraRotationConstraints : public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	UComposableCameraRotationConstraints() { PaletteCategory = TEXT("Rotation"); }

public:
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

protected:
	
public:
	// Whether to enable yaw constraint.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	bool bConstrainYaw { false };

	// Constrain yaw type, choose between WorldSpace, ActorSpace and VectorSpace.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraRotationConstrainType ConstrainYawType { EComposableCameraRotationConstrainType::WorldSpace };

	// Selects whether the yaw reference actor comes from the controller's controlled pawn or an explicit actor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "ConstrainYawType == EComposableCameraRotationConstrainType::ActorSpace", EditConditionHides))
	EComposableCameraActorInputSource ActorForYawConstrainSource { EComposableCameraActorInputSource::ExplicitActor };

	// Reference actor when ActorSpace is used. Its transform will be used as the reference frame.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "ConstrainYawType == EComposableCameraRotationConstrainType::ActorSpace && ActorForYawConstrainSource == EComposableCameraActorInputSource::ExplicitActor", EditConditionHides))
	TObjectPtr<AActor> ActorForYawConstrain { nullptr };

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

	// Selects whether the pitch reference actor comes from the controller's controlled pawn or an explicit actor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "ConstrainPitchType == EComposableCameraRotationConstrainType::ActorSpace", EditConditionHides))
	EComposableCameraActorInputSource ActorForPitchConstrainSource { EComposableCameraActorInputSource::ExplicitActor };

	// Reference actor when ActorSpace is used. Its transform will be used as the reference frame.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "ConstrainPitchType == EComposableCameraRotationConstrainType::ActorSpace && ActorForPitchConstrainSource == EComposableCameraActorInputSource::ExplicitActor", EditConditionHides))
	TObjectPtr<AActor> ActorForPitchConstrain { nullptr };

	// Reference vector when VectorSpace is used. It will serve as the forward vector of the reference frame.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "ConstrainPitchType == EComposableCameraRotationConstrainType::VectorSpace"))
	FVector VectorForPitchConstrain { FVector::ForwardVector };

	// Pitch range in the reference frame. Use the world space, actor space or vector space as the reference frame.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	FVector2D PitchRange { FVector2D {-70., 70.} };

private:
	double FindTargetYawInRange(double WorldCurrentYaw, double WorldPivotYaw, const FVector2D& PivotSpaceYawRange);
	double FindTargetPitchInRange(const double WorldCurrentPitch, const FVector2D& Vector2);
};
