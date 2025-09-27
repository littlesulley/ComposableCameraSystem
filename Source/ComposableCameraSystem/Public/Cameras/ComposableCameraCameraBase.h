// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Camera/CameraActor.h"
#include "UObject/Object.h"
#include "Variables/ComposableCameraVariableCollection.h"
#include "ComposableCameraCameraBase.generated.h"

class UComposableCameraVariableCollection;
class UComposableCameraCameraNodeBase;
class AComposableCameraPlayerCamaraManager;

USTRUCT(BlueprintType)
struct FComposableCameraPose
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "ComposableCameraSystem|CameraPose")
	FVector Position { 0, 0, 0 };

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "ComposableCameraSystem|CameraPose")
	FRotator Rotation { 0, 0, 0 };

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "ComposableCameraSystem|CameraPose")
	double FieldOfView { 75.f };

	void BlendBy(const FComposableCameraPose& Other, float OtherWeight)
	{
		Position = FMath::Lerp(Position, Other.Position, OtherWeight);
		
		const FRotator DeltaAng = (Other.Rotation - Rotation).GetNormalized();
		Rotation = OtherWeight * DeltaAng;

		FieldOfView = FMath::Lerp(FieldOfView, Other.FieldOfView, OtherWeight);
	}
};

/**
 * Parameters when activating a new camera.
 */
USTRUCT(BlueprintType)
struct FComposableCameraActivateParams
{
	GENERATED_BODY()

public:
	// Data table for node initializers. If not set, no initializer will be applied.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UDataTable* NodeInitializerDataTable;

	// Tags to use for node initializers. Only matched tags will be used.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTagContainer NodeInitializerTags;

	// Whether this camera is transient.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsTransient = false;

	// The life time if this camera is transient.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float LifeTime = 0.f;
};

DECLARE_DYNAMIC_DELEGATE_OneParam(FOnCameraFinishConstructed, AComposableCameraCameraBase*, Camera);

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
	 *
	 * @NOTE: This property is exposed as EditAnywhere, only for debug purposes at runtime. You should NEVER modify this for instances.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "ComposableCameraSystem|Camera")
	TArray<UComposableCameraCameraNodeBase*> CameraNodes;

	/** Context variable collection for this camera. You should create a new collection specific to this camera first,
	 * and then assign it here. All nodes within this camera can only use this collection.
	 *
	 * @NOTE: This property is exposed as EditAnywhere, only for debug purposes at runtime. You should NEVER modify this for instances.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ComposableCameraSystem|Camera")
	TSoftObjectPtr<UComposableCameraVariableCollection> ContextVariables;

public:
	virtual void BeginPlay() override;
	
	void Initialize(AComposableCameraPlayerCamaraManager* Manager);
	void BeginPlayCamera(const FComposableCameraPose& CurrentCameraPose);
	[[nodiscard]] FComposableCameraPose TickCamera(float DeltaTime);

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
	// Reset all variables in the owning variable collection.
	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Camera")
	void ResetVariableCollection()
	{
		if (ContextVariables.IsValid())
		{
			ContextVariables->ResetVariables();
		}
	}

	// Get the owning node by class. If no such node exists, returns nullptr.
	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Camera", meta = (DeterminesOutputType = "NodeClass"))
	UComposableCameraCameraNodeBase* GetNodeByClass(TSubclassOf<UComposableCameraCameraNodeBase> NodeClass);
	
	// Get owning player camera manager. Must be type ComposableCameraPlayerCamaraManager.
	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Camera")
	AComposableCameraPlayerCamaraManager* GetOwningPlayerCameraManager() { return CameraManager; }

	// Set owning player camera manager.
	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Camera")
	void SetOwningPlayerCameraManager(AComposableCameraPlayerCamaraManager* InCameraManager) { CameraManager = InCameraManager; }

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
	UPROPERTY(Transient, VisibleAnywhere, Category = "ComposableCameraSystem|Camera")
	FComposableCameraPose CameraPose;

	// Camera pose for last frame.
	UPROPERTY(Transient, VisibleAnywhere, Category = "ComposableCameraSystem|Camera")
	FComposableCameraPose LastFrameCameraPose;

	// Whether this camera is transient, i.e., it has a fixed life time.
	UPROPERTY(Transient, VisibleAnywhere, Category = "ComposableCameraSystem|Camera")
	bool bIsTransient { false };

	// Life time if this camera is transient.
	UPROPERTY(Transient, VisibleAnywhere, Category = "ComposableCameraSystem|Camera")
	float LifeTime { 0.f };

	// Remaining life time.
	UPROPERTY(Transient, VisibleAnywhere, Category = "ComposableCameraSystem|Camera")
	float RemainingLifeTime { 0.f };

	// Whether this camera is currently active (running) and not yet killed, generally waiting to be resumed.
	UPROPERTY(Transient, VisibleAnywhere, Category = "ComposableCameraSystem|Camera")
	bool bIsRunning { true };

	// Pending camera to be resumed. This happens when the running camera is transient and once it finishes, ParentPendingCamera will be resumed.
	UPROPERTY(Transient, VisibleAnywhere, Category = "ComposableCameraSystem|Camera")
	AComposableCameraCameraBase* ParentPendingCamera;

protected:
	UPROPERTY(Transient)
	TObjectPtr<AComposableCameraPlayerCamaraManager> CameraManager;
};
