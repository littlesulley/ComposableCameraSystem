// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "ComposableCameraReceivePivotActorNode.generated.h"

/**
 * Reads a pivot actor's location and publishes it as the pivot position for downstream nodes.
 * This node runs every tick.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (ToolTip = "Reads a pivot actor's location and publishes it as the pivot position for downstream nodes."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraReceivePivotActorNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()
	
public:
	virtual void OnInitialize_Implementation() override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

public:
	// Whether to use a bone as the target pivot point.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	bool bUseBoneForPivot { false };

	// If use bone, specify the bone name. If we cannot find such a bone, will use the pivot actor's root position.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters, meta = (EditCondition = "bUseBoneForPivot == true"))
	FName BoneName;

private:
	USkeletalMeshComponent* SkeletalMeshComponentForPivotActor { nullptr };
};
