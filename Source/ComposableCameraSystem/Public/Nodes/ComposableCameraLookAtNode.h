// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "ComposableCameraLookAtNode.generated.h"

class UComposableCameraInterpolatorBase;

UENUM()
enum class EComposableCameraLookAtType : uint8
{
	// Target position.
	ByPosition,

	// Target actor.
	ByActor
};

UENUM()
enum class EComposableCameraLookAtConstraintType : uint8
{
	// Hard look at: player cannot control camera.
	Hard,

	// Soft look at: player can control camera around the look at direction.
	Soft
};

/**
 * Node for looking at some target position.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraLookAtNode : public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	virtual void OnBeginPlayNode_Implementation(const FComposableCameraPose& CurrentCameraPose) override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;

protected:
	virtual void ReceiveInitializerNode(UComposableCameraCameraNodeBase* Initializer) override;
	
public:
	// Look at type, to look at a specified position or by an actor's position.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraLookAtType LookAtType;

	// Target look-at position.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "LookAtType == EComposableCameraLookAtType::ByPosition", EditConditionHides))
	FVector LookAtPosition;

	// Target look-at actor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "LookAtType == EComposableCameraLookAtType::ByActor", EditConditionHides))
	AActor* LookAtActor;

	// Target look-at socket when the actor has a SkeletalMeshComponent. If no such component exists, will use LookAtActor's position.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "LookAtType == EComposableCameraLookAtType::ByActor", EditConditionHides))
	FName LookAtSocket;

	// Look-at constraint type, hard look at or soft look at.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraLookAtConstraintType LookAtConstraintType;

	// Soft look at range in degrees.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "LookAtConstraintType == EComposableCameraLookAtConstraintType::Soft", EditConditionHides))
	float SoftLookAtRange { 20.f };
	
	// Soft look at weight. The larger it is, the harder the camera will look at the target.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "LookAtConstraintType == EComposableCameraLookAtConstraintType::Soft", EditConditionHides))
	float SoftLookAtWeight { 0.2f };

	// Soft look at interpolator when resuming to the look-at direction.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = InputParameters, meta = (EditCondition = "LookAtConstraintType == EComposableCameraLookAtConstraintType::Soft", EditConditionHides))
	UComposableCameraInterpolatorBase* SoftLookAtInterpolator { nullptr };

private:
	USkeletalMeshComponent* SkeletalMeshComponentForLookAtActor { nullptr };
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<FRotator>>> Interpolator_T;
};
