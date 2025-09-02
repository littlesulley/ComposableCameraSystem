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
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, CollapseCategories)
class COMPOSABLECAMERASYSTEM_API UComposableCameraPivotOffsetNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	UComposableCameraPivotOffsetNode(const  FObjectInitializer& ObjectInitializer);
	virtual void OnBeginPlayNode_Implementation() override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	ECameraPivotOffset PivotOffsetType = ECameraPivotOffset::WorldSpace;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSoftObjectPtr<AActor> ActorForLocalSpace = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FVector PivotOffset = FVector::ZeroVector;

private:
	UComposableCameraPoseContextPivotOnly* PivotOnlyContext;
};
