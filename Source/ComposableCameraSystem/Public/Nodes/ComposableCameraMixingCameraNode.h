// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "ComposableCameraMixingCameraNode.generated.h"

/**
 * Parameters when activating a new persistent camera.
 */
USTRUCT(BlueprintType)
struct FComposableCameraPersistentActivateParams
{
	GENERATED_BODY()

	FComposableCameraPersistentActivateParams() = default;
	FComposableCameraPersistentActivateParams(const FTransform& InInitialTransform) : InitialTransform(InInitialTransform)
	{}

public:
	// Whether to preserve current camera pose when activating a new camera.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bPreserveCameraPose { true };
	
	// Initial transform to spawn the camera if bPreserveCameraPose is false.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FTransform InitialTransform;
	
	// Data asset for node initializers. If not set, no initializer will be applied.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UComposableCameraNodeInitializerDataAsset* NodeInitializerDataAsset { nullptr };
};

USTRUCT()
struct FComposableCameraMixingCameraNodeCameraDefinition
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere)
	TSubclassOf<AComposableCameraCameraBase> CameraClass;

	UPROPERTY(EditAnywhere)
	FComposableCameraPersistentActivateParams ActivationParams;
};

/**
 * Node for mixing multiple cameras. 
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraMixingCameraNode :
	public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	virtual void OnBeginPlayNode_Implementation(const FComposableCameraPose& CurrentCameraPose) override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void BeginDestroy() override;

public:
	UPROPERTY(EditAnywhere, Category = InputParameters)
	TArray<FComposableCameraMixingCameraNodeCameraDefinition> Cameras;

	//@TODO: Needs a mechanism to pass in weights of these cameras.

	
private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<AComposableCameraCameraBase>> CameraInstances;
};
