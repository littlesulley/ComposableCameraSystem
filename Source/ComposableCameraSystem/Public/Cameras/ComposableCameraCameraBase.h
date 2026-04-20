// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraTypes.h"
#include "Engine/EngineTypes.h"
#include "Engine/Scene.h"
#include "UObject/Object.h"
#include "Core/ComposableCameraRuntimeDataBlock.h"
#include "Core/ComposableCameraParameterBlock.h"
#include "ComposableCameraNamespaces.h"
#include "ComposableCameraCameraBase.generated.h"

class UComposableCameraModifierManager;
class UComposableCameraTransitionBase;
struct FComposableCameraDebugSnapshot;
class UComposableCameraActionBase;
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

/**
 * Camera pose state produced by node evaluation and consumed by the PCM.
 *
 * Field categories:
 *   - Transform (Position, Rotation) — always lerped.
 *   - FOV dual-mode (FieldOfView, FocalLength) — a pose expresses FOV either in
 *     degrees (FieldOfView > 0) or via physical optics (FocalLength > 0), never
 *     both. Use GetEffectiveFieldOfView() to resolve to degrees. BlendBy()
 *     resolves both sides to degrees BEFORE lerping and emits a degrees-mode
 *     result (FocalLength = -1). See "FOV resolution invariant" in DesignDoc.
 *   - Physical camera (SensorWidth/Height, Aperture, FocusDistance, ISO, etc.)
 *     — always lerped; only applied to post-process when
 *     PhysicalCameraBlendWeight > 0 via ApplyPhysicalCameraSettings().
 *   - Projection & aspect (ProjectionMode, ConstrainAspectRatio, ...) —
 *     booleans and enums snap at 50% blend factor; numerics (OrthographicWidth
 *     etc.) lerp normally.
 *   - Post-process (PostProcessSettings) — blended via
 *     FPostProcessUtils::BlendPostProcessSettings in BlendBy(). Individual
 *     properties are only active when their corresponding bOverride_* flag is
 *     true; a default-constructed FPostProcessSettings (all overrides off)
 *     contributes nothing, so cameras without a PostProcess node pay no cost.
 *     At apply-time in GetCameraViewFromCameraPose, pose PP is layered on top
 *     of the camera component's PP using OverridePostProcessSettings.
 *
 * Sentinel semantics (<= 0 means "unset"):
 *   - FieldOfView: -1 means "use FocalLength".
 *   - FocusDistance: -1 means "no DoF override".
 *   These fields use LerpOptional semantics in BlendBy(): if one side is unset,
 *   the valid side's value is inherited across the blend rather than lerped
 *   through a meaningless range.
 */
USTRUCT(BlueprintType)
struct FComposableCameraPose
{
	GENERATED_BODY()

public:
	// --- Transform ---

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "ComposableCameraSystem|CameraPose|Transform")
	FVector Position { 0, 0, 0 };

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "ComposableCameraSystem|CameraPose|Transform")
	FRotator Rotation { 0, 0, 0 };

	// --- FOV (dual-mode: specify either FieldOfView OR FocalLength, not both) ---

	/** Horizontal FOV in degrees. If <= 0, FocalLength is used instead. */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "ComposableCameraSystem|CameraPose|FOV")
	double FieldOfView { -1.0 };

	/** Focal length in mm. If <= 0, FieldOfView (in degrees) is used instead. */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "ComposableCameraSystem|CameraPose|FOV")
	float FocalLength { 35.f };

	// --- Physical camera (DoF / exposure inputs) ---

	/** Sensor width in mm. Super35 default. */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "ComposableCameraSystem|CameraPose|Physical")
	float SensorWidth { 24.89f };

	/** Sensor height in mm. */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "ComposableCameraSystem|CameraPose|Physical")
	float SensorHeight { 18.67f };

	/** Lens aperture in f-stops. Used for DoF when PhysicalCameraBlendWeight > 0. */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "ComposableCameraSystem|CameraPose|Physical")
	float Aperture { 2.8f };

	/** Focus distance in world units. <= 0 means "no DoF override" (sentinel). */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "ComposableCameraSystem|CameraPose|Physical")
	float FocusDistance { -1.f };

	/** Shutter speed in 1/seconds. Used for auto-exposure when PhysicalCameraBlendWeight > 0. */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "ComposableCameraSystem|CameraPose|Physical")
	float ShutterSpeed { 60.f };

	/** Sensor sensitivity in ISO. */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "ComposableCameraSystem|CameraPose|Physical")
	float ISO { 100.f };

	/** Number of blades in the lens diaphragm (affects bokeh shape). */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "ComposableCameraSystem|CameraPose|Physical")
	int32 DiaphragmBladeCount { 8 };

	/** Anamorphic squeeze factor. 1.0 = spherical. */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "ComposableCameraSystem|CameraPose|Physical")
	float SqueezeFactor { 1.f };

	/** Sensor overscan percentage (0 = none). */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "ComposableCameraSystem|CameraPose|Physical")
	float Overscan { 0.f };

	/**
	 * Blend weight for physical-camera post-process contribution.
	 * 0 = skip ApplyPhysicalCameraSettings entirely (no DoF/exposure override).
	 * 1 = apply physical settings at full strength.
	 * Naturally gates the fade-in of DoF during non-physical -> physical transitions.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "ComposableCameraSystem|CameraPose|Physical")
	float PhysicalCameraBlendWeight { 0.f };

	// --- Projection & aspect ---

	/** Projection mode (Perspective or Orthographic). Snapped at 50% blend. */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "ComposableCameraSystem|CameraPose|Projection")
	TEnumAsByte<ECameraProjectionMode::Type> ProjectionMode { ECameraProjectionMode::Perspective };

	/** Whether to constrain aspect ratio (letterbox). Snapped at 50% blend. */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "ComposableCameraSystem|CameraPose|Projection")
	bool ConstrainAspectRatio { false };

	/** Whether to override the default aspect ratio axis constraint. Snapped at 50% blend. */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "ComposableCameraSystem|CameraPose|Projection")
	bool OverrideAspectRatioAxisConstraint { false };

	/** Axis constraint (only honored when OverrideAspectRatioAxisConstraint is true). Snapped at 50% blend. */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "ComposableCameraSystem|CameraPose|Projection")
	TEnumAsByte<EAspectRatioAxisConstraint> AspectRatioAxisConstraint { EAspectRatioAxisConstraint::AspectRatio_MaintainYFOV };

	/** Orthographic view width in world units (only honored when ProjectionMode = Orthographic). */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "ComposableCameraSystem|CameraPose|Projection")
	float OrthographicWidth { 512.f };

	/** Ortho near clip plane (only honored when ProjectionMode = Orthographic). */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "ComposableCameraSystem|CameraPose|Projection")
	float OrthoNearClipPlane { 0.f };

	/** Ortho far clip plane (only honored when ProjectionMode = Orthographic). */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "ComposableCameraSystem|CameraPose|Projection")
	float OrthoFarClipPlane { 10000.f };

	// --- Post-process ---

	/**
	 * Post-process settings carried by this pose.
	 * Default-constructed: all bOverride_* flags are false, meaning "no opinion".
	 * Nodes (e.g., PostProcessNode) set specific bOverride_* flags + values.
	 *
	 * BlendBy() uses FPostProcessUtils::BlendPostProcessSettings to lerp all
	 * properties (including override flags). At apply-time, only properties
	 * whose bOverride_* flag is true layer on top of the camera component's
	 * designer-authored post-process via OverridePostProcessSettings.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "ComposableCameraSystem|CameraPose|PostProcess")
	FPostProcessSettings PostProcessSettings;

public:
	// --- API ---

	/**
	 * Resolve the effective horizontal FOV in degrees.
	 * Uses FocalLength + SensorWidth if FocalLength > 0, otherwise uses FieldOfView directly.
	 * Falls back to a reasonable default if both are unset.
	 */
	COMPOSABLECAMERASYSTEM_API double GetEffectiveFieldOfView() const;

	/**
	 * Set FOV in degrees, clearing the FocalLength sentinel so this pose is unambiguously in degrees mode.
	 * Nodes that produce an FOV in degrees (like FieldOfViewNode) should call this instead of assigning FieldOfView directly.
	 */
	COMPOSABLECAMERASYSTEM_API void SetFieldOfViewDegrees(double InFieldOfViewDegrees);

	/**
	 * Apply physical-camera-derived settings (DoF, auto-exposure) to a post-process settings block.
	 * No-op if PhysicalCameraBlendWeight <= 0. Scales contribution by PhysicalCameraBlendWeight.
	 * Mirrors GameplayCameras' FCameraPose::ApplyPhysicalCameraSettings.
	 *
	 * @param InOutPostProcessSettings  Target to modify.
	 * @param bOverwriteSettings        If true, overwrites already-set post-process entries; else only writes unset ones.
	 * @return true if any settings were written, false if the call was a no-op.
	 */
	COMPOSABLECAMERASYSTEM_API bool ApplyPhysicalCameraSettings(FPostProcessSettings& InOutPostProcessSettings, bool bOverwriteSettings = false) const;

	/**
	 * Blend this pose toward Other by OtherWeight in [0, 1].
	 * Blend rules (see struct comment for categories):
	 *   - Position: linear lerp.
	 *   - Rotation: delta-angle lerp (normalized).
	 *   - FOV: resolve both sides via GetEffectiveFieldOfView(), lerp degrees, emit degrees-mode result (FocalLength = -1).
	 *   - Physical numerics: linear lerp.
	 *   - Sentinel fields (FocusDistance): LerpOptional — inherit valid side if the other is unset.
	 *   - Projection booleans/enums: snap at OtherWeight >= 0.5.
	 */
	COMPOSABLECAMERASYSTEM_API void BlendBy(const FComposableCameraPose& Other, float OtherWeight);
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

	// Whether to freeze the outgoing (source) camera during the transition.
	// When true, the source camera stops ticking and holds its last evaluated pose
	// while the transition blends to the new camera. Has no effect if there is no transition.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bFreezeSourceCamera { false };

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

	/**
	 * Node-scoped actions fired around each node's TickNode. The PCM registers
	 * actions here when their ExecutionType is PreNodeTick / PostNodeTick (see
	 * AComposableCameraPlayerCameraManager::AddCameraAction /
	 * BindCameraActionsForNewCamera). Matching is by exact class (Node->GetClass()
	 * == Action->TargetNodeClass), same rule as the Modifier system.
	 *
	 * These are NOT UPROPERTY — ownership lives on the PCM's CameraActions
	 * UPROPERTY TSet, which is the GC root. This camera-local view is just a
	 * hot-path iteration cache; the PCM clears it via UnregisterNodeAction when
	 * an action expires, and EndPlay clears it defensively.
	 */
	TArray<UComposableCameraActionBase*> PreNodeTickActions;
	TArray<UComposableCameraActionBase*> PostNodeTickActions;

	void RegisterNodeAction(UComposableCameraActionBase* Action);
	void UnregisterNodeAction(UComposableCameraActionBase* Action);
	
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
