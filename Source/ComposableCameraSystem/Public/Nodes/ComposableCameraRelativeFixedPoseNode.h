// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "ComposableCameraRelativeFixedPoseNode.generated.h"

UENUM()
enum class EComposableCameraRelativeFixedPoseMethod : uint8
{
	// Fixed transform.
	RelativeToTransform,
	
	// Relative to an actor.
	RelativeToActor
};

/**
 * Node for maintaining a fixed pose relative to some given transform.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (ToolTip = "Maintains a fixed camera pose relative to a specified transform or actor."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraRelativeFixedPoseNode : public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	virtual void OnInitialize_Implementation() override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

protected:

public:
	// Method to use for fixed pose.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraRelativeFixedPoseMethod Method;

	// Relative transform when method is RelativeToTransform.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "Method == EComposableCameraRelativeFixedPoseMethod::RelativeToTransform", EditConditionHides))
	FTransform RelativeTransform;

	// Relative actor when method is RelativeToActor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "Method == EComposableCameraRelativeFixedPoseMethod::RelativeToActor", EditConditionHides))
	AActor* RelativeActor;

	// Relative socket when method is RelativeToActor and the actor has a SkeletalMeshComponent. If no such component exists, will use RelativeActor's transform.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "Method == EComposableCameraRelativeFixedPoseMethod::RelativeToActor", EditConditionHides))
	FName RelativeSocket;

	// The target transform that is applied to the local specified space.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	FTransform TargetTransform;

private:
	USkeletalMeshComponent* SkeletalMeshComponentForRelativeActor { nullptr };
};
