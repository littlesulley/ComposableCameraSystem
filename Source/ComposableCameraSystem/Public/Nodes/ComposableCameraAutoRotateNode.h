// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "StructUtils/InstancedStruct.h"
#include "ComposableCameraAutoRotateNode.generated.h"

UENUM()
enum class EComposableCameraAutoRotateType
{
	// Camera will rotate following a given actor's speed.
	ActorVelocity,

	// Camera will rotate around a given actor's transform.
	ActorTransform,
	
	// Camera will rotate to a given transform.
	FixedTransform
};

USTRUCT(NotBlueprintable)
struct FComposableCameraAutoRotateNodeParameters : public FComposableCameraNodeParamaters
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EComposableCameraAutoRotateType RotateType { EComposableCameraAutoRotateType::ActorVelocity };
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float RotateSpeed { 1.0f };

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	AActor* PivotActor { nullptr };
};

/**
 * Node for auto-rotating
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, CollapseCategories)
class COMPOSABLECAMERASYSTEM_API UComposableCameraAutoRotateNode : public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	virtual void OnBeginPlayNode_Implementation(const FComposableCameraPose& CurrentCameraPose) override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FComposableCameraAutoRotateNodeParameters InputParameters;
};
