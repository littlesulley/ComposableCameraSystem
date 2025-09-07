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
	void UpdateCamera();

	/**
	 * Do something when finishing initializing. This is called before all nodes begin to play.
	 */
	UFUNCTION(BlueprintImplementableEvent, DisplayName = "OnInitializedCamera", Category = "ComposableCameraSystem|Camera")
	void OnInitialized();

	/**
	 * A function used to fully or partially override the current camera pose. You can write your own camera update logic here. \n
	 * This is called when all tick events finish and you can fully override the OutPose for this frame. \n
	 * @param DeltaTime World ticked delta time for this frame. \n
	 * @param OldCameraPose Camera pose for last frame. \n
	 * @param CurrentCameraPose Current camera pose calculated by internal nodes. \n
	 * @param OutPose The camera pose actually used for final camera position, location, FOV and other parameters. \n
	 * @return If true, use the returned OutPose as the actual pose. If false, use the pose calculated by nodes.
	 */
	UFUNCTION(BlueprintImplementableEvent, DisplayName = "OnUpdateCamera", Category = "ComposableCameraSystem|Camera")
	bool OnUpdateCamera(float DeltaTime, FComposableCameraPose OldCameraPose, FComposableCameraPose CurrentCameraPose, FComposableCameraPose& OutPose);

public:
	// Get owning player camera manager. Must be type ComposableCameraPlayerCamaraManager.
	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Camera")
	AComposableCameraPlayerCamaraManager* GetOwningPlayerCameraManager() { return CameraManager; }

	// Get camera pose for this frame.
	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Camera")
	FComposableCameraPose GetCameraPose() const { return CameraPose; }

	// Get camera pose for last frame.
	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Camera")
	FComposableCameraPose GetLastFrameCameraPose() const { return LastFrameCameraPose; }

	// If this camera is transient.
	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Camera")
	bool IsTransient() const { return bIsTransient; }

	// Get life time. If the camera is not transient, -1 will be returned.
	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Camera")
	float GetLifeTime() const { return bIsTransient ? LifeTime : -1.f; }

	// Get remaining life time. If the camera is not transient, -1 will be returned.
	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Camera")
	float GetRemainingLifeTime() const { return bIsTransient ? RemainingLifeTime : -1.f; }

public:
	// Camera pose for this frame.
	UPROPERTY(Transient, VisibleAnywhere)
	FComposableCameraPose CameraPose;

	// Camera pose for last frame.
	UPROPERTY(Transient, VisibleAnywhere)
	FComposableCameraPose LastFrameCameraPose;

	// Whether this camera is transient, i.e., it has a fixed life time.
	UPROPERTY(Transient, VisibleAnywhere)
	bool bIsTransient { false };

	// Life time if this camera is transient.
	UPROPERTY(Transient, VisibleAnywhere)
	float LifeTime { 0.f };

	// Remaining life time.
	UPROPERTY(Transient, VisibleAnywhere)
	float RemainingLifeTime { 0.f };

	// Pending camera to be resumed. This happens when the running camera is transient and once it finishes, ParentPendingCamera will be resumed.
	UPROPERTY(Transient, VisibleAnywhere)
	AComposableCameraCameraBase* ParentPendingCamera;
	
private:
	TObjectPtr<AComposableCameraPlayerCamaraManager> CameraManager;
};
