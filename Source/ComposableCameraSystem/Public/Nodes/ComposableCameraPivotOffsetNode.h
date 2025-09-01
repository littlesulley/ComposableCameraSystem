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
 * Camera node to adjust the pivot offset in world/camera space.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraPivotOffsetNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = PivotOffset)
	ECameraPivotOffset PivotOffsetType = ECameraPivotOffset::WorldSpace;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = PivotOffset)
	AActor* ActorForLocalSpace = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = PivotOffset)
	FVector PivotOffset = FVector::ZeroVector;
};
