// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ComposableCameraCameraBase.generated.h"

class UComposableCameraCameraNodeBase;
class AComposableCameraPlayerCamaraManager;

USTRUCT(BlueprintType)
struct FComposableCameraPose
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite)
	FVector Position {};

	UPROPERTY(BlueprintReadWrite)
	FRotator Rotation {};

	UPROPERTY(BlueprintReadWrite)
	double FieldOfView {};
};

/**
 * Base camera class/
 */
UCLASS(Abstract, DefaultToInstanced, EditInlineNew, BlueprintType, Blueprintable, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraCameraBase
	: public UObject
{
	GENERATED_BODY()

public:
	UComposableCameraCameraBase(const FObjectInitializer& ObjectInitializer);
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = ComposableCamera)
	TArray<UComposableCameraCameraNodeBase*> CameraNodes;

protected:
	void Initialize(AComposableCameraPlayerCamaraManager* Manager);
	void TickCamera(float DeltaTime);
	void BeginPlayCamera();

	UFUNCTION(BlueprintNativeEvent, DisplayName = "Tick", Category = "ComposableCameraSystem|Camera")
	FComposableCameraPose OnTickCamera(float DeltaTime);
	virtual FComposableCameraPose OnTickCamera_Implementation(float DeltaTime)
	{
		return FComposableCameraPose{};
	}

	UFUNCTION(BlueprintNativeEvent, DisplayName = "BeginPlay", Category = "ComposableCameraSystem|Camera")
	void OnBeginPlayCamera();
	virtual void OnBeginPlayCamera_Implementation() {}

public:
	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Camera")
	AComposableCameraPlayerCamaraManager* GetOwningPlayerCameraManager() { return CameraManager; }

	UFUNCTION(CallInEditor)
	void Test()
	{
		OnTickCamera(0.1f);
	}
	
public:
	UPROPERTY(BlueprintReadOnly, Category = "ComposableCameraSystem|Camera")
	FComposableCameraPose CameraPose;

	UPROPERTY(BlueprintReadOnly, Category = "ComposableCameraSystem|Camera")
	FComposableCameraPose LastFrameCameraPose;

private:
	TObjectPtr<AComposableCameraPlayerCamaraManager> CameraManager;
};
