// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Camera/CameraActor.h"
#include "UObject/Object.h"
#include "ComposableCameraCameraBase.generated.h"

class UComposableCameraVariableCollection;
class UComposableCameraCameraNodeBase;
class AComposableCameraPlayerCamaraManager;

USTRUCT(BlueprintType)
struct FComposableCameraPose
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category = "ComposableCameraSystem|CameraPose")
	FVector Position { 0, 0, 0 };

	UPROPERTY(BlueprintReadWrite, Category = "ComposableCameraSystem|CameraPose")
	FRotator Rotation { 0, 0, 0 };

	UPROPERTY(BlueprintReadWrite, Category = "ComposableCameraSystem|CameraPose")
	double FieldOfView { 75.f };
};

/**
 * Base camera class.
 */
UCLASS(Abstract, DefaultToInstanced, BlueprintType, Blueprintable, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API AComposableCameraCameraBase
	: public ACameraActor
{
	GENERATED_BODY()

public:
	AComposableCameraCameraBase(const FObjectInitializer& ObjectInitializer);

	/** Tag for this camera. Used by modifiers to distinguish different cameras. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ComposableCameraSystem|Camera")
	FGameplayTag CameraTag {};

	/** Nodes for this camera. They're executed in the order they are placed in this array.
	 * Each node has two types of parameters: Input Parameters and Context Parameters.
	 * Input parameters are the node's own parameters used to update its inner variables and execute its logic.
	 * Context parameters are references to variables in ContextVariables, which can be read/written by each node.
	 * 
	 * For example, the ComposableCameraReceivePivotActorNode has an input parameter PivotActor, and a context parameter PivotPosition.
	 * It reads the location of the PivotActor and writes the location to PivotPosition.
	 * After this node, PivotPosition can be read by the other following nodes.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Instanced, Category = "ComposableCameraSystem|Camera")
	TArray<UComposableCameraCameraNodeBase*> CameraNodes;

	/** Context variable collection for this camera. You should create a new collection specific to this camera first,
	 * and then assign it here. All nodes within this camera can only use this collection.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ComposableCameraSystem|Camera")
	TSoftObjectPtr<UComposableCameraVariableCollection> ContextVariables;
	

protected:
	void Initialize(AComposableCameraPlayerCamaraManager* Manager);
	void BeginPlayCamera();
	void TickCamera(float DeltaTime);
	void UpdateCamera(float DeltaTime);

	/**
	 * Do something when finishing initializing. This is called before all nodes begin to play.
	 */
	UFUNCTION(BlueprintImplementableEvent, DisplayName = "OnInitializedCamera", Category = "ComposableCameraSystem|Camera")
	void OnInitialized();

	/**
	 * A function used to execute custom logic when internal camera tick finishes. 
	 * @param OldCameraPose Old camera pose for last frame.
	 * @param NewCameraPose New camera pose calculated by nodes.
	 */
	UFUNCTION(BlueprintImplementableEvent, DisplayName = "OnTickedCamera", Category = "ComposableCameraSystem|Camera")
	void OnTicked(const FComposableCameraPose& OldCameraPose, const FComposableCameraPose& NewCameraPose);

	/**
	 * A function used to fully or partially override the current camera pose. You can write your own camera update logic here.
	 * @param DeltaTime World ticked delta time for this frame.
	 * @param CurrentCameraPose Current camera pose.
	 * @param OutPose The camera pose actually used for final camera position, location, FOV and other parameters.
	 * @return If true, use the returned OutPose as the actual pose. If false, use the pose calculated by nodes.
	 */
	UFUNCTION(BlueprintImplementableEvent, DisplayName = "OnUpdateCamera", Category = "ComposableCameraSystem|Camera")
	bool OnUpdateCamera(float DeltaTime, FComposableCameraPose CurrentCameraPose, FComposableCameraPose& OutPose);

public:
	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Camera")
	AComposableCameraPlayerCamaraManager* GetOwningPlayerCameraManager() { return CameraManager; }

	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Camera")
	FComposableCameraPose GetCameraPose() const { return CameraPose; }

	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Camera")
	FComposableCameraPose GetLastFrameCameraPose() const { return LastFrameCameraPose; }

public:
	UPROPERTY(Transient)
	FComposableCameraPose CameraPose;

	UPROPERTY(Transient)
	FComposableCameraPose LastFrameCameraPose;

private:
	TObjectPtr<AComposableCameraPlayerCamaraManager> CameraManager;
};
