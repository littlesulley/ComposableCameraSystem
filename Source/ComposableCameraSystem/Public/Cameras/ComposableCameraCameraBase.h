// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Camera/CameraActor.h"
#include "UObject/Object.h"
#include "Variables/ComposableCameraVariableCollection.h"
#include "ComposableCameraNamespaces.h"
#include "ComposableCameraCameraBase.generated.h"

class UComposableCameraModifierManager;
class UComposableCameraTransitionBase;
class UComposableCameraNodeInitializerDataAsset;
class UComposableCameraVariableCollection;
class UComposableCameraCameraNodeBase;
class AComposableCameraPlayerCameraManager;

using namespace ComposableCameraModifier;

UENUM(BlueprintType)
enum class EComposableCameraResumeCameraTransformSchema : uint8
{
	// Preserve current camera pose (position and rotation).
	PreserveCurrent,

	// Preserve the resumed camera pose.
	PreserveResumed,

	// Specify a transform.
	Specified
};

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
		OtherWeight = FMath::Clamp(OtherWeight, 0.f, 1.f);
		
		Position = FMath::Lerp(Position, Other.Position, OtherWeight);
		
		const FRotator DeltaAng = (Other.Rotation - Rotation).GetNormalized();
		Rotation = (Rotation + OtherWeight * DeltaAng).GetNormalized();

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

	FComposableCameraActivateParams() = default;
	FComposableCameraActivateParams(const FTransform& InInitialTransform) : InitialTransform(InInitialTransform) {}
	FComposableCameraActivateParams(
		bool bInPreserveCameraPose,
		const FTransform& InInitialTransform,
		bool bInUseInitialTransformRotation,
		UComposableCameraNodeInitializerDataAsset* InNodeInitializerDataAsset,
		bool bInIsTransient,
		float InLifeTime)
			: bPreserveCameraPose(bInPreserveCameraPose)
			, InitialTransform(InInitialTransform)
			, bUseInitialTransformRotation(bInUseInitialTransformRotation)
			, NodeInitializerDataAsset(InNodeInitializerDataAsset)
			, bIsTransient(bInIsTransient)
			, LifeTime(InLifeTime)
	{}

public:
	// Whether to preserve current camera pose when activating a new camera.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bPreserveCameraPose { true };
	
	// Initial transform to spawn the camera if bPreserveCameraPose is false.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FTransform InitialTransform;

	// Whether to use InitialTransform's rotation to override the new camera's rotation, regardless of bPreserveCameraPose.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bUseInitialTransformRotation { false };
	
	// Data asset for node initializers. If not set, no initializer will be applied.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UComposableCameraNodeInitializerDataAsset* NodeInitializerDataAsset { nullptr };

	// Whether this camera is transient.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsTransient = false;

	// The life time if this camera is transient.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float LifeTime = 0.f;
};

/** Called before any internal node is executed. */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnPreTick, float, const FComposableCameraPose&, FComposableCameraPose&);
/** Called after all internal nodes are executed. */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnPostTick, float, const FComposableCameraPose&, FComposableCameraPose&);
/** Called before any internal node is executed for camera actions. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnActionPreTick, float, DeltaTime, const FComposableCameraPose&, CurrentCameraPose, FComposableCameraPose&, OutputPose);
/** Called after all internal nodes are executed for camera actions. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnActionPostTick, float, DeltaTime, const FComposableCameraPose&, CurrentCameraPose, FComposableCameraPose&, OutputPose);
/** Called when the camera finishes constructed, before BeginPlay is called. */
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnCameraFinishConstructed, AComposableCameraCameraBase*, Camera);

/**
 * Base camera class.
 */
UCLASS(DefaultToInstanced, BlueprintType, Blueprintable, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API AComposableCameraCameraBase
	: public ACameraActor
{
	GENERATED_BODY()

public:
	AComposableCameraCameraBase(const FObjectInitializer& ObjectInitializer);

	/** Tag for this camera. Used by modifiers to distinguish different cameras. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ComposableCameraSystem|Composable Camera")
	FGameplayTag CameraTag {};

	/** Default transition. Usually used for returning back to this camera from a transient camera. */
	UPROPERTY(EditDefaultsOnly, Instanced, Category = "ComposableCameraSystem|Composable Camera")
	UComposableCameraTransitionBase* DefaultTransition;

	/** Whether to preserve last camera's pose when resuming this camera. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ComposableCameraSystem|Composable Camera")
	bool bDefaultPreserveCameraPose { true };

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
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "ComposableCameraSystem|Composable Camera")
	TArray<UComposableCameraCameraNodeBase*> CameraNodes;

	/** Context variable collection for this camera. You should create a new collection specific to this camera first,
	 * and then assign it here. All nodes within this camera can only use this collection.
	 *
	 * @NOTE: This property is exposed as EditAnywhere, only for debug purposes at runtime. You should NEVER modify this for instances.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ComposableCameraSystem|Composable Camera")
	TSoftObjectPtr<UComposableCameraVariableCollection> ContextVariables;

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	
	void Initialize(AComposableCameraPlayerCameraManager* Manager, UComposableCameraNodeInitializerDataAsset* NodeInitializerDataAsset);
	void ApplyModifiers(const T_NodeModifier& Modifiers);
	void BeginPlayCamera(const FComposableCameraPose& CurrentCameraPose);
	[[nodiscard]] FComposableCameraPose TickCamera(float DeltaTime);

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
	FOnPreTick		  OnPreTick;
	FOnPostTick		  OnPostTick;
	FOnActionPreTick  OnActionPreTick;
	FOnActionPostTick OnActionPostTick;
	
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
	AComposableCameraPlayerCameraManager* GetOwningPlayerCameraManager() { return CameraManager; }

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

	// If this camera should end its lifetime.
	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Camera")
	bool IsFinished() const { return bIsTransient && RemainingLifeTime <= 0.f; }

public:
	// Camera pose for this frame.
	UPROPERTY(Transient, VisibleAnywhere, Category = "ComposableCameraSystem|Composable Camera")
	FComposableCameraPose CameraPose;

	// Camera pose for last frame.
	UPROPERTY(Transient, VisibleAnywhere, Category = "ComposableCameraSystem|Composable Camera")
	FComposableCameraPose LastFrameCameraPose;

	// Whether this camera is transient, i.e., it has a fixed life time.
	UPROPERTY(Transient, VisibleAnywhere, Category = "ComposableCameraSystem|Composable Camera")
	bool bIsTransient { false };

	// Life time if this camera is transient.
	UPROPERTY(Transient, VisibleAnywhere, Category = "ComposableCameraSystem|Composable Camera")
	float LifeTime { 0.f };

	// Remaining life time.
	UPROPERTY(Transient, VisibleAnywhere, Category = "ComposableCameraSystem|Composable Camera")
	float RemainingLifeTime { 0.f };

	// Pending camera to be resumed. This happens when the running camera is transient and once it finishes, ParentPendingCamera will be resumed.
	UPROPERTY(Transient, VisibleAnywhere, Category = "ComposableCameraSystem|Composable Camera")
	TSoftObjectPtr<AComposableCameraCameraBase> ParentPendingCamera;

protected:
	UPROPERTY(Transient)
	TObjectPtr<AComposableCameraPlayerCameraManager> CameraManager;
};
