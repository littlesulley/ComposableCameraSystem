// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Camera/CameraActor.h"
#include "UObject/Object.h"
#include "Core/ComposableCameraRuntimeDataBlock.h"
#include "Core/ComposableCameraParameterBlock.h"
#include "ComposableCameraNamespaces.h"
#include "ComposableCameraCameraBase.generated.h"

class UComposableCameraModifierManager;
class UComposableCameraTransitionBase;
struct FComposableCameraDebugSnapshot;
class UComposableCameraTransitionDataAsset;
class UComposableCameraCameraNodeBase;
class UComposableCameraTypeAsset;
class UComposableCameraComputeNodeBase;
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
		bool bInIsTransient,
		float InLifeTime)
			: bPreserveCameraPose(bInPreserveCameraPose)
			, InitialTransform(InInitialTransform)
			, bUseInitialTransformRotation(bInUseInitialTransformRotation)
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
UCLASS(DefaultToInstanced, BlueprintType, NotBlueprintable, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API AComposableCameraCameraBase
	: public ACameraActor
{
	GENERATED_BODY()

public:
	AComposableCameraCameraBase(const FObjectInitializer& ObjectInitializer);

	/** Tag for this camera. Used by modifiers to distinguish different cameras. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ComposableCameraSystem|Composable Camera")
	FGameplayTag CameraTag {};

	/** Enter transition. Usually used for returning back to this camera from a transient camera. */
	UPROPERTY(EditDefaultsOnly, Instanced, Category = "ComposableCameraSystem|Composable Camera")
	UComposableCameraTransitionBase* EnterTransition;

	/** Whether to preserve last camera's pose when resuming this camera. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ComposableCameraSystem|Composable Camera")
	bool bDefaultPreserveCameraPose { true };

	/** Nodes for this camera. They're executed in the order they are placed in this array.
	 * Each node reads input pin values, applies its logic, and writes output pin values.
	 * Inter-node data flow is handled entirely through the pin-based RuntimeDataBlock system.
	 *
	 * @NOTE: This property is exposed as EditAnywhere, only for debug purposes at runtime. You should NEVER modify this for instances.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "ComposableCameraSystem|Composable Camera")
	TArray<UComposableCameraCameraNodeBase*> CameraNodes;

	/**
	 * One-shot compute nodes that run during BeginPlayCamera, after every node
	 * (both camera nodes and compute nodes) has had Initialize() run. They are
	 * walked in array order and each has ExecuteBeginPlay() called exactly once.
	 *
	 * Compute nodes are NOT per-frame-ticked. They exist to perform C++ math /
	 * data shaping at activation time and publish the result via internal
	 * variables or output pins. Downstream camera nodes then read those values
	 * in their own Initialize or TickNode bodies.
	 *
	 * Populated during type-asset activation:
	 * AComposableCameraPlayerCameraManager::OnTypeAssetCameraConstructed
	 * duplicates every non-null entry of
	 * UComposableCameraTypeAsset::ComputeNodeTemplates into this array,
	 * then reorders by the type asset's ComputeExecutionOrder (built from
	 * the editor's BeginPlay compute chain rooted at
	 * UComposableCameraBeginPlayStartGraphNode — see EditorDesignDoc §8
	 * "Dual exec chains: camera chain vs BeginPlay compute chain").
	 *
	 * @NOTE: Like CameraNodes, this is EditAnywhere only for debug inspection.
	 * Do not mutate at runtime.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "ComposableCameraSystem|Composable Camera")
	TArray<TObjectPtr<UComposableCameraComputeNodeBase>> ComputeNodes;

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	
	void Initialize(AComposableCameraPlayerCameraManager* Manager);

	/**
	 * Per-node initialization loop. Walks CameraNodes and ComputeNodes, calls
	 * Node->Initialize on each, and wires OnPreTick/OnPostTick delegates for
	 * CameraNodes only. Compute nodes are initialized but NOT wired to the
	 * per-frame tick multicasts — they only run once, from BeginPlayCamera.
	 *
	 * Called twice during type-asset activation: first from Initialize() (where
	 * CameraNodes is still empty — a no-op), then again from
	 * OnTypeAssetCameraConstructed once templates have been duplicated and the
	 * RuntimeDataBlock wired, so every node's Initialize() runs exactly once.
	 */
	void InitializeNodes();

	void ApplyModifiers(const T_NodeModifier& Modifiers);

	/**
	 * Runs the BeginPlay compute chain: walks ComputeNodes in order and calls
	 * ExecuteBeginPlay on each. Called exactly once per activation from
	 * AActor::BeginPlay, after per-node Initialize has run for every node.
	 *
	 * Compute nodes that need the outgoing camera pose read it from
	 * OwningPlayerCameraManager->GetCurrentCameraPose() — which is why this
	 * function no longer takes a pose parameter.
	 */
	void BeginPlayCamera();
	[[nodiscard]] FComposableCameraPose TickCamera(float DeltaTime);

public:
	FOnPreTick		  OnPreTick;
	FOnPostTick		  OnPostTick;
	FOnActionPreTick  OnActionPreTick;
	FOnActionPostTick OnActionPostTick;
	
public:
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

	/**
	 * Full execution chain for the per-frame camera tick, including both
	 * camera-node steps and internal-variable Set operations. Copied from
	 * UComposableCameraTypeAsset::FullExecChain during OnTypeAssetCameraConstructed.
	 *
	 * CameraNodeIndex in each entry references the author-order index in
	 * CameraNodes (which is parallel to TypeAsset::NodeTemplates).
	 */
	UPROPERTY(Transient)
	TArray<FComposableCameraExecEntry> FullExecChain;

	/**
	 * Full execution chain for the BeginPlay compute pass, including both
	 * compute-node steps and internal-variable Set operations. Copied from
	 * UComposableCameraTypeAsset::ComputeFullExecChain during
	 * OnTypeAssetCameraConstructed.
	 *
	 * CameraNodeIndex in each entry references the author-order index in
	 * ComputeNodes (which is parallel to TypeAsset::ComputeNodeTemplates when
	 * ComputeFullExecChain is non-empty — in that case OnTypeAssetCameraConstructed
	 * skips the legacy reorder to preserve index correspondence).
	 */
	UPROPERTY(Transient)
	TArray<FComposableCameraExecEntry> ComputeFullExecChain;

	/**
	 * The number of entries in TypeAsset::NodeTemplates at construction time.
	 * Used as the base offset for compute-node pin keys in the RuntimeDataBlock
	 * (compute node i has pin key NodeIndex = TypeAssetNodeTemplateCount + i).
	 *
	 * Stored explicitly because CameraNodes.Num() can differ from
	 * NodeTemplates.Num() if OnTypeAssetCameraConstructed skips null templates
	 * during duplication.
	 */
	int32 TypeAssetNodeTemplateCount = 0;

	/**
	 * Owned RuntimeDataBlock for type-asset-based cameras.
	 * Allocated during activation from a UComposableCameraTypeAsset.
	 * Nodes hold raw pointers into this block — they never outlive the camera.
	 */
	TUniquePtr<FComposableCameraRuntimeDataBlock> OwnedRuntimeDataBlock;

	/**
	 * The type asset that was used to construct this camera.
	 * Stored so that ReactivateCurrentCamera (triggered by modifier changes)
	 * can fully reconstruct the camera from the same source asset instead of
	 * producing an empty shell.
	 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UComposableCameraTypeAsset> SourceTypeAsset;

	/**
	 * The parameter block that was applied when this camera was activated
	 * from a type asset. Stored alongside SourceTypeAsset so reactivation
	 * preserves the original caller-provided parameter values.
	 *
	 * Empty for type-asset cameras activated without any parameter overrides.
	 */
	FComposableCameraParameterBlock SourceParameterBlock;

protected:
	UPROPERTY(Transient)
	TObjectPtr<AComposableCameraPlayerCameraManager> CameraManager;

#if WITH_EDITOR
public:
	/**
	 * Capture a debug snapshot of this camera's current state for editor overlay.
	 *
	 * Called by the editor toolkit's debug ticker during PIE. Walks CameraNodes,
	 * reads per-node DebugPoseAfterTick and output pin values from the
	 * RuntimeDataBlock, and formats them as human-readable strings.
	 *
	 * Zero-cost in non-editor builds (compiled out entirely). The function itself
	 * does allocate (TArray, FString), but it runs on the editor tick, not the
	 * game tick, so it does not violate the "no hot-path allocations" rule.
	 */
	struct FComposableCameraDebugSnapshot SnapshotDebugState() const;

	/**
	 * Clear per-node debug flags at the start of each TickCamera call.
	 * Must be called before the node loop so bDebugWasTickedThisFrame
	 * reflects only the current frame.
	 */
	void ClearNodeDebugFlags();
#endif
};
