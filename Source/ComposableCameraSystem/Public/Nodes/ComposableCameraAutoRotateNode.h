// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
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

protected:
	virtual void ReceiveInitializerNode(UComposableCameraCameraNodeBase* Initializer) override;
	
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraAutoRotateType RotateType { EComposableCameraAutoRotateType::ActorVelocity };
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	float RotateSpeed { 1.0f };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	AActor* PivotActor { nullptr }; 
};
