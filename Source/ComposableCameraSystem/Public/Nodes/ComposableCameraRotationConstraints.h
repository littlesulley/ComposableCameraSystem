// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
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
 * Node for constraining rotation's yaw or pitch.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraRotationConstraints : public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	virtual void OnBeginPlayNode_Implementation(const FComposableCameraPose& CurrentCameraPose) override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	bool bConstrainYaw { false };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraRotationConstrainType ConstrainYawType { EComposableCameraRotationConstrainType::WorldSpace };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "ConstrainYawType == EComposableCameraRotationConstrainType::ActorSpace"))
	AActor* ActorForYawConstrain { nullptr };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "ConstrainYawType == EComposableCameraRotationConstrainType::VectorSpace"))
	FVector VectorForYawConstrain;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	FVector2D YawRange { FVector2D {-180., 180.} };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	bool bConstrainPitch { true };
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraRotationConstrainType ConstrainPitchType { EComposableCameraRotationConstrainType::WorldSpace };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "ConstrainYawType == EComposableCameraRotationConstrainType::ActorSpace"))
	AActor* ActorForPitchConstrain { nullptr };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "ConstrainYawType == EComposableCameraRotationConstrainType::VectorSpace"))
	FVector VectorForPitchConstrain;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	FVector2D PitchRange { FVector2D {-70., 70.} };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = InputParameters)
	UComposableCameraInterpolatorBase* ResumeInterpolator;

};
