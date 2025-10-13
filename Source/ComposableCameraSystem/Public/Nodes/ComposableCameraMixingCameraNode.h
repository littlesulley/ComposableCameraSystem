// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "ComposableCameraMixingCameraNode.generated.h"

DECLARE_DYNAMIC_DELEGATE_RetVal(TArray<float>, FOnReceiveMixingCameraWeights);


/** Weight normalization method. */
UENUM()
enum class EComposableCameraMixingCameraWeightNormalizationMethod : uint8
{
	// Update rule: w_i <- |w_i| / (|w_1| + ... + |w_n|)
	L1,
	
	// Update rule: w_i <- w_i^2 / (w_1^2 + ... + w_n^2)
	L2,

	// Update rule: w_i <- exp(w_i) / (exp(w_1) + ... + exp(w_n))
	SoftMax
};

/** Mixing camera node mode. */
UENUM()
enum class EComposableCameraMixingCameraMode : uint8
{
	PositionOnly,
	RotationOnly,
	Both
};

/** Different methods to average rotations. Ref: https://sulley.cc/2024/01/11/20/06/. */
UENUM()
enum class EComposableCameraMixingCameraRotationMethod : uint8
{
	// Matrix interpolation.
	MatrixInterp,

	// Circular interpolation.
	CircularInterp,

	// Flip quaternion interpolation.
	QuaternionInterpolation,

	// Flip angle interpolation.
	AngleInterpolation
};

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
 * Node for mixing multiple cameras. \n
 * This node will instantiate new camera instances specified by parameter Cameras. \n
 * During runtime, you should pass in a UpdateWeight function that provides weights for these cameras. Make sure all weights are greater than zero. \n
 * If one camera instance is not valid, its weight will be set to zero, then a squared normalization will be applied to normalize all weights.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraMixingCameraNode :
	public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	virtual void OnBeginPlayNode_Implementation(const FComposableCameraPose& CurrentCameraPose) override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void ReceiveInitializerNode(UComposableCameraCameraNodeBase* Initializer) override;
	virtual void BeginDestroy() override;

public:
	// Whether to only mix position, rotation or both.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraMixingCameraMode MixMode { EComposableCameraMixingCameraMode::Both };

	// Weight normalization method.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraMixingCameraWeightNormalizationMethod WeightNormalizationMethod { EComposableCameraMixingCameraWeightNormalizationMethod::L2 };
	
	// Method to mix rotation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "MixMode == EComposableCameraMixingCameraMode::RotationOnly || MixMode == EComposableCameraMixingCameraMode::Both", EditConditionHides))
	EComposableCameraMixingCameraRotationMethod MixRotationMethod { EComposableCameraMixingCameraRotationMethod::MatrixInterp };

	// Epsilon for Circular Rotation Interpolation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (ClampMin = "-1.0", ClampMax = "1.0", EditCondition = "MixRotationMethod == EComposableCameraMixingCameraRotationMethod::CircularInterp", EditConditionHides))
	float CircularInterpEpsilon { 0.25f };
	
	// Camera classes to instantiate.
	UPROPERTY(EditAnywhere, Category = InputParameters)
	TArray<FComposableCameraMixingCameraNodeCameraDefinition> Cameras;

	// Update weight function.
	UPROPERTY()
	FOnReceiveMixingCameraWeights OnReceiveMixingCameraWeights;

public:
	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Node")
	void SetUpdateWeights(FOnReceiveMixingCameraWeights OnUpdateMixingCameraWeights);
	
private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<AComposableCameraCameraBase>> CameraInstances;

private:
	void NormalizeWeights(TArray<float>& Array);
	void TickCameras(TArray<FComposableCameraPose>& Poses, float DeltaTime);

	FVector GetMixedPosition(const TArray<FComposableCameraPose>& Poses, const TArray<float>& Weights);
	FRotator GetMixedRotation(const TArray<FComposableCameraPose>& Poses, const TArray<float>& Weights);
	double GetMixedFieldOfView(const TArray<FComposableCameraPose>& Poses, const TArray<float>& Weights);
};
