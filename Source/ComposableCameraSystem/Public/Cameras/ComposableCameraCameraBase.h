// Copyright 2026 Sulley. All Rights Reserved.

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
 *   - Transform (Position, Rotation). Always lerped.
 *   - FOV dual-mode (FieldOfView, FocalLength). A pose expresses FOV either in
 *     degrees (FieldOfView > 0) or via physical optics (FocalLength > 0), never
 *     both. Use GetEffectiveFieldOfView() to resolve to degrees. BlendBy()
 *     resolves both sides to degrees BEFORE lerping and emits a degrees-mode
 *     result (FocalLength = -1). See "FOV resolution invariant" in DesignDoc.
 *   - Physical camera (SensorWidth/Height, Aperture, FocusDistance, ISO, etc.).
 *     Always lerped; only applied to post-process when
 *     PhysicalCameraBlendWeight > 0 for DoF or ExposureBlendWeight > 0 for exposure.
 *   - Projection & aspect (ProjectionMode, ConstrainAspectRatio, ...).
 *     Booleans and enums snap at 50% blend factor; numerics (OrthographicWidth
 *     etc.) lerp normally.
 *   - Post-process (PostProcessSettings). Blended via
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

	/** Shutter speed in 1/seconds. Used for exposure when ExposureBlendWeight > 0. */
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
	 * Blend weight for physical-camera DoF post-process contribution.
	 * 0 = skip DoF-derived physical settings.
	 * 1 = apply DoF settings at full strength.
	 * Naturally gates the fade-in of DoF during non-physical -> physical transitions.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "ComposableCameraSystem|CameraPose|Physical")
	float PhysicalCameraBlendWeight { 0.f };

	/**
	 * Blend weight for physical-camera exposure contribution (ISO / ShutterSpeed).
	 * 0 = leave exposure untouched. 1 = apply exposure settings at full strength.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "ComposableCameraSystem|CameraPose|Physical")
	float ExposureBlendWeight { 0.f };

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
	 * Apply physical-camera-derived settings (DoF, exposure) to a post-process settings block.
	 * DoF is gated by PhysicalCameraBlendWeight; exposure is gated by ExposureBlendWeight.
	 * The physical-exposure enable flag is preserved, not toggled here. When
	 * that flag is enabled upstream, Lens-driven f-stop changes follow UE's
	 * physical-camera semantics and may affect exposure.
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
	 *   - Sentinel fields (FocusDistance): LerpOptional. Inherit valid side if the other is unset.
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activation")
	bool bPreserveCameraPose { true };
	
	// Initial transform to spawn the camera if bPreserveCameraPose is false.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activation")
	FTransform InitialTransform;

	// Whether to use InitialTransform's rotation to override the new camera's rotation, regardless of bPreserveCameraPose.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activation")
	bool bUseInitialTransformRotation { false };

	// Whether to freeze the outgoing (source) camera during the transition.
	// When true, the source camera stops ticking and holds its last evaluated pose
	// while the transition blends to the new camera. Has no effect if there is no transition.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activation")
	bool bFreezeSourceCamera { false };

	// Whether this camera is transient.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activation")
	bool bIsTransient = false;

	// The life time if this camera is transient.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activation")
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

	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	/** Tag for this camera. Used by modifiers to distinguish different cameras. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ComposableCameraSystem|Composable Camera")
	FGameplayTag CameraTag {};

	/** Cached `CameraTag.ToString()` populated once at Initialize and reused
	 *  by per-tick `TRACE_CPUPROFILER_EVENT_SCOPE_STR` so the dynamic Insights
	 *  scope name doesn't allocate an FString per tick. CameraTag is
	 *  EditDefaultsOnly so the cache is stable across the camera's lifetime
	 * . Repopulated only on Initialize (in case the runtime mutates the
	 *  tag before construction completes). */
	FString CameraTagTraceName;

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
	 * UComposableCameraBeginPlayStartGraphNode. See EditorDesignDoc Section 8
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
	 * per-frame tick multicasts. They only run once, from BeginPlayCamera.
	 *
	 * Called twice during type-asset activation: first from Initialize() (where
	 * CameraNodes is still empty. A no-op), then again from
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
	 * OwningPlayerCameraManager->GetCurrentCameraPose(). Which is why this
	 * function no longer takes a pose parameter.
	 */
	void BeginPlayCamera();
	[[nodiscard]] FComposableCameraPose TickCamera(float DeltaTime);

	/**
	 * Force the next `TickCamera` (or `TickWithInputPose`) on this camera to walk
	 * the node chain even if it has already ticked this frame.
	 *
	 * The default per-frame memoization (see `LastTickedFrameCounter`) is a
	 * correctness requirement for cameras inside the snapshot DAG (a single
	 * underlying leaf reachable via multiple RefLeaf paths must tick exactly
	 * once). Callers that own a camera OUTSIDE the DAG. Most notably
	 * `UComposableCameraLevelSequenceComponent`'s `InternalCamera` and the
	 * Patch-overlay evaluators. Can use this hook to rerun the chain after
	 * pushing fresh inputs in the same frame (e.g. a Sequencer Shot / Patch
	 * override applied after an earlier same-frame tick has already cached a
	 * stale pose). Inside the DAG, calling
	 * this would re-introduce the double-advance bug the cache exists to
	 * prevent. DAG callers must not use it.
	 */
	void InvalidateTickCache() { LastTickedFrameCounter = 0; }

	/**
	 * Per-frame entry point for evaluators driven by an upstream pose, used by
	 * the Camera Patch system. Sets `CameraPose = InputPose` so the first node in
	 * the chain reads the upstream pose as its starting state, then delegates to
	 * `TickCamera`. Returns the post-chain pose (TickCamera's normal return).
	 *
	 * Side effects on the per-frame state are deliberate and identical to a normal
	 * tick afterward: `LastFrameCameraPose` will reflect this frame's upstream
	 * input on next tick (per PatchSystemProposal Section 16.7. Damping / spring nodes
	 * inside a Patch see "how much did upstream change between frames", which is
	 * the right input for smoothing the upstream's motion).
	 *
	 * Memoization caveat (carried from TickCamera): if this is called twice in
	 * the same `GFrameCounter` on the same evaluator, the second call returns the
	 * cached `CameraPose` from the first call. Node chain does NOT re-tick.
	 * Patches do not currently exercise this case (each evaluator is referenced
	 * by exactly one PatchInstance in exactly one Director's ActivePatches list).
	 */
	[[nodiscard]] FComposableCameraPose TickWithInputPose(float DeltaTime, const FComposableCameraPose& InputPose);

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
	 * These are NOT UPROPERTY. Ownership lives on the PCM's CameraActions
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

#if !UE_BUILD_SHIPPING
	/**
	 * Draw world-space debug primitives for this camera.
	 *
	 * Invoked from the viewport debug ticker when `CCS.Debug.Viewport` is
	 * enabled. Two independently gated pieces:
	 *  - Frustum pyramid at the camera's current pose. Drawn only when
	 *    `bDrawFrustum` is true. The ticker passes true only while the
	 *    player is NOT viewing through the camera (F8 eject / SIE /
	 *    `CCS.Debug.Viewport.AlwaysShow`), because otherwise the pyramid
	 *    just occludes the near plane.
	 *  - A walk over `CameraNodes` calling each node's `DrawNodeDebug`.
	 *    Always invoked. Each node's override checks its own per-node
	 *    CVar (`CCS.Debug.Viewport.<NodeName>`) and early-outs when zero.
	 *    Per-node gizmos are therefore visible in BOTH possessed play
	 *    and ejected state, because they rarely occlude the viewpoint.
	 *
	 * Reads `CameraPose` (the leaf-local evaluated pose), not the PCM's
	 * blended pose. For the running camera in a steady state these are
	 * the same; during a transition, this shows the pose this camera is
	 * contributing, not the blended result.
	 *
	 * Compiled out in shipping builds.
	 */
	void DrawCameraDebug(class UWorld* World, bool bDrawFrustum) const;

	/**
	 * 2D counterpart to DrawCameraDebug. Walks `CameraNodes` and invokes
	 * each node's `DrawNodeDebug2D` override. Called by the viewport debug
	 * service's "Game"-channel hook (HUD pass). Fires during PIE possessed
	 * play, not during F8 eject. Each node gates its own output on its
	 * per-node CVar, same pattern as the 3D path.
	 */
	void DrawCameraDebug2D(class UCanvas* Canvas, class APlayerController* PC) const;
#endif

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
	 * Per-frame tick memoization.
	 *
	 * When the evaluation DAG (produced by snapshot-based RefLeaves)
	 * reaches the same camera via multiple paths in a single frame, e.g.
	 * the pop transition's target subtree and the RefLeaf-A push-source
	 * RefLeaf both bottom out at the same original A leaf, a second
	 * TickCamera would double-advance the camera's per-node
	 * state (damping, interpolator `bStartFrame`, spline progress, noise
	 * seeds, etc.). TickCamera compares GFrameCounter against this
	 * value: if it matches, the cached CameraPose is returned verbatim
	 * and the node chain is NOT walked again.
	 *
	 * 0 is a valid sentinel: GFrameCounter starts above 0 in any real
	 * engine session, so a freshly-constructed camera (counter = 0)
	 * will always take the full-tick path on its first evaluation.
	 * Not a UPROPERTY. Purely transient evaluation-time scratch.
	 */
	uint64 LastTickedFrameCounter { 0 };

	/**
	 * Set by `UComposableCameraCompositionFramingNode::OnTickNode` each tick
	 *. True when the primary `SolveShot` returned `bValid=false` (anchor
	 * unresolvable, all weights zero, target index out of range, etc.).
	 *
	 * Consumed by `UComposableCameraLevelSequenceComponent::ProjectPoseToCineCamera`
	 * to skip the CineCamera transform write on solver failure, so the
	 * CineCamera holds its last-valid transform instead of having a default-
	 * identity (or upstream-default) pose burned in. Critical for the
	 * invalid-framing path: if evaluation runs before Sequencer has pushed
	 * the first Shot override, the solver runs against the framing node's
	 * default empty Shot, fails, and would otherwise project an origin pose
	 * onto the CineCam. Destroying the camera's spawn-position transform
	 * just before the PCM ViewTarget switch reads it.
	 *
	 * Cameras without a CompositionFramingNode keep this at the default
	 * `false` and project normally. The flag is only ever written by the
	 * framing node, so non-framing pipelines aren't affected.
	 *
	 * Transient. Not a UPROPERTY. Reset by spawn (default ctor->false)
	 * and by every framing node tick.
	 */
	bool bLastTickFramingFailed { false };

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
	 * ComputeFullExecChain is non-empty. In that case OnTypeAssetCameraConstructed
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
	 * Nodes hold raw pointers into this block. They never outlive the camera.
	 */
	TUniquePtr<FComposableCameraRuntimeDataBlock> OwnedRuntimeDataBlock;

	/**
	 * The type asset that was used to construct this camera.
	 * Stored so that ReactivateCurrentCamera (triggered by modifier changes)
	 * can fully reconstruct the camera from the same source asset instead of
	 * producing an empty shell.
	 *
	 * STRONG ref by design (not weak). Reactivation routes through
	 * `OnTypeAssetCameraConstructed`, which dereferences this to walk the
	 * type asset's NodeTemplates / ExposedParameters / FullExecChain. If
	 * the asset was originally loaded transiently. Soft path resolved
	 * mid-frame, DataTable row asset, BP local that already went out of
	 * scope. The only remaining reference at activation time may be this
	 * one. A weak ref would let GC reclaim the asset between activation
	 * and a later modifier-triggered Reactivate, the `.Get()` would return
	 * null, and the new camera would be built as an empty shell with no
	 * nodes / no data block (silent regression, no crash). The strong
	 * ref's only cost is keeping the type asset alive for the camera's
	 * lifetime. Acceptable because (a) type assets are small metadata,
	 * (b) the camera owns this anyway in spirit (it can't function
	 * without it), and (c) the field is `Transient` so save / load is
	 * unaffected.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UComposableCameraTypeAsset> SourceTypeAsset;

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
