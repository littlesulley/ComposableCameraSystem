// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "ComposableCameraPivotOffsetNode.generated.h"

UENUM(BlueprintType)
enum class ECameraPivotOffset : uint8
{
	WorldSpace,
	ActorLocalSpace,
	CameraSpace
};

/**
 * Node for adjusting the pivot position by applying an offset in world/camera/actor local space. \n
 * If using camera space, the CurrentCameraPose parameter in the Tick function will be used. \n
 * @ InputParameter PivotOffsetType: In which space you'd like to apply offset, can be world, camera, or actor local. \n
 * @ InputParameter ActorForLocalSpace: The actor determining the local space if you choose actor local space. \n
 * @ InputParameter PivotOffset: The offset. \n
 * @ ContextParameter ContextPivotPosition: The pivot location that is read from and written to after applying offset by this node. \n
 * This node runs every tick.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraPivotOffsetNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	UComposableCameraPivotOffsetNode(const  FObjectInitializer& ObjectInitializer);
	virtual void OnBeginPlayNode_Implementation(const FComposableCameraPose& CurrentCameraPose) override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	
public:
	// In which space you'd like to apply offset, can be world, camera, or actor local.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	ECameraPivotOffset PivotOffsetType = ECameraPivotOffset::WorldSpace;

	// The actor determining the local space if you choose actor local space.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	TSoftObjectPtr<AActor> ActorForLocalSpace = nullptr;

	// The offset.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	FVector PivotOffset = FVector::ZeroVector;

	// The pivot location that is read from and written to after applying offset by this node.
	UPROPERTY(EditAnywhere, Category = ContextParameters)
	FVector3dComposableCameraContextParameter ContextPivotPosition;

private:
	void UpdatePivotOffset(const FComposableCameraPose& CurrentCameraPose);
};
