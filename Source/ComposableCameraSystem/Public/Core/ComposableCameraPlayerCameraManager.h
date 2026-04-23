// Copyright Sulley. All rights reserved.

#pragma once

#include "ComposableCameraModifierManager.h"
#include "Camera/PlayerCameraManager.h"
#include "Transitions/ComposableCameraTransitionBase.h"
#include "ComposableCameraNamespaces.h"
#include "Core/ComposableCameraParameterBlock.h"
// Always included — `GetPoseHistory` is part of the public surface even
// in shipping builds (returns an empty array there). Keeps the Panel
// cpp linkable without per-configuration #ifs around every call site.
#include "Debug/ComposableCameraPoseHistoryData.h"
#include "ComposableCameraPlayerCameraManager.generated.h"

class UComposableCameraActionBase;
class UComposableCameraTransitionDataAsset;
class UComposableCameraTypeAsset;
class UComposableCameraNodeModifierDataAsset;
class UComposableCameraModifierManager;
class UComposableCameraDirector;
class UComposableCameraContextStack;
class UComposableCameraTransitionBase;
struct FComposableCameraRuntimeDataBlock;
	
UCLASS(ClassGroup = ComposableCameraSystem, NotPlaceable)
class COMPOSABLECAMERASYSTEM_API AComposableCameraPlayerCameraManager
	: public APlayerCameraManager
{
	GENERATED_BODY()

public:
	AComposableCameraPlayerCameraManager(const FObjectInitializer& ObjectInitializer);
	virtual void BeginPlay() override;
	virtual void InitializeFor(APlayerController* PlayerController) override;
	virtual void SetViewTarget(AActor* NewViewTarget, FViewTargetTransitionParams TransitionParams = FViewTargetTransitionParams()) override;
	virtual void ProcessViewRotation(float DeltaTime, FRotator& OutViewRotation, FRotator& OutDeltaRot) override;
	virtual void DisplayDebug(class UCanvas* Canvas, const class FDebugDisplayInfo& DebugDisplay, float& YL, float& YPos) override;
	
	AComposableCameraCameraBase* CreateNewCamera(
		TSubclassOf<AComposableCameraCameraBase> CameraClass,
		const FComposableCameraActivateParams& ActivationParams);

	/**
	 * Activate a new camera, optionally specifying which context it belongs to.
	 * If ContextName is valid and that context isn't on the stack yet, it is auto-pushed.
	 * If ContextName is NAME_None, the camera activates on the current active context.
	 * When switching to a different context, the new context's evaluation tree gets a
	 * reference leaf pointing to the previous context's Director for inter-context blending.
	 */
	AComposableCameraCameraBase* ActivateNewCamera(
		TSubclassOf<AComposableCameraCameraBase> CameraClass,
		UComposableCameraTransitionDataAsset* Transition,
		const FComposableCameraActivateParams& ActivationParams,
		FOnCameraFinishConstructed OnPreBeginplayEvent,
		FName ContextName = NAME_None);

	/**
	 * Activate a new camera using a raw transition instance (not wrapped in a DataAsset).
	 * Used internally by ActivateNewCameraFromTypeAsset when the type asset provides its
	 * own DefaultTransition as an instanced UComposableCameraTransitionBase*.
	 */
	AComposableCameraCameraBase* ActivateNewCamera(
		TSubclassOf<AComposableCameraCameraBase> CameraClass,
		UComposableCameraTransitionBase* TransitionInstance,
		const FComposableCameraActivateParams& ActivationParams,
		FOnCameraFinishConstructed OnPreBeginplayEvent,
		FName ContextName = NAME_None);

	/**
	 * Activate a new camera from a Camera Type Asset (data-driven workflow).
	 * Creates a default AComposableCameraCameraBase, duplicates node templates from the type asset,
	 * wires the RuntimeDataBlock, and applies caller-provided parameter values.
	 *
	 * @param CameraTypeAsset The type asset defining the camera's node composition and parameters.
	 * @param TransitionOverride Optional transition override. If nullptr, uses the type asset's DefaultTransition.
	 * @param ActivationParams Standard activation parameters (transient, lifetime, pose preservation).
	 * @param Parameters The caller-provided parameter block with exposed parameter values.
	 * @param ContextName Context to activate in (NAME_None = current active context).
	 * @return The activated camera instance, or nullptr on failure.
	 */
	AComposableCameraCameraBase* ActivateNewCameraFromTypeAsset(
		UComposableCameraTypeAsset* CameraTypeAsset,
		UComposableCameraTransitionDataAsset* TransitionOverride,
		const FComposableCameraActivateParams& ActivationParams,
		const FComposableCameraParameterBlock& Parameters,
		FName ContextName = NAME_None);

	AComposableCameraCameraBase* ReactivateCurrentCamera(UComposableCameraTransitionBase* Transition);

	// Resume a given camera with a given transition.
	void ResumeCamera(AComposableCameraCameraBase* ResumeCamera, UComposableCameraTransitionBase* Transition, EComposableCameraResumeCameraTransformSchema TransformSchema, FTransform SpecifiedTransform, bool bUseSpecifiedRotation);

	// ~~~~ Modifiers.
	const TSet<UComposableCameraActionBase*>& GetCameraActions();
	void AddModifier(UComposableCameraNodeModifierDataAsset* ModifierAsset);
	void RemoveModifier(UComposableCameraNodeModifierDataAsset* ModifierAsset);
	void ApplyModifiers(AComposableCameraCameraBase* Camera, bool bRefreshModifierData = false);

	// Called when modifier is added or removed. When this happens, the modifier data will be refreshed and the current running camera may be re-activated.
	void OnModifierChanged();
	// ~~~~ 
	
	// ~~~~ Actions.
	UComposableCameraActionBase* AddCameraAction(TSubclassOf<UComposableCameraActionBase> ActionClass, bool bOnlyForCurrentCamera);
	UComposableCameraActionBase* FindCameraAction(TSubclassOf<UComposableCameraActionBase> ActionClass);
	void RemoveCameraAction(UComposableCameraActionBase* Action);
	void ExpireCameraAction(TSubclassOf<UComposableCameraActionBase> ActionClass);
	void BindCameraActionsForNewCamera(AComposableCameraCameraBase* Camera);
	// ~~~~

	// ~~~~ Context Stack.
	/**
	 * Pop a specific camera context by name.
	 * If this is the active context, the previous context resumes with an optional transition.
	 * Cannot pop the base context if it is the last one remaining.
	 *
	 * @param ContextName The name identifying which context to pop.
	 * @param TransitionOverride Optional transition. If nullptr, falls back to the resume camera's DefaultTransition.
	 * @param ActivationParams Optional activation params for the resume camera.
	 */
	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Context")
	void PopCameraContext(
		UPARAM(meta = (GetOptions = "ComposableCameraSystem.ComposableCameraProjectSettings.GetContextNames")) FName ContextName,
		UComposableCameraTransitionDataAsset* TransitionOverride = nullptr,
		const FComposableCameraActivateParams& ActivationParams = FComposableCameraActivateParams());

	/**
	 * Terminate the current camera context — pops the active (top) context off the stack.
	 * The previous context resumes with an optional transition. Cannot pop the base context.
	 * This is the explicit way to end a context. Transient cameras trigger this automatically.
	 *
	 * @param TransitionOverride Optional transition. If nullptr, falls back to the resume camera's DefaultTransition.
	 * @param ActivationParams Optional activation params for the resume camera.
	 */
	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Context")
	void TerminateCurrentCamera(
		UComposableCameraTransitionDataAsset* TransitionOverride = nullptr,
		const FComposableCameraActivateParams& ActivationParams = FComposableCameraActivateParams());

	/** Get the number of contexts on the stack. */
	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Context")
	int32 GetContextStackDepth() const;

	/** Get the name of the currently active (top) context. */
	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Context")
	FName GetActiveContextName() const;
	// ~~~~
	
	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Camera")
	AComposableCameraCameraBase* GetRunningCamera() const
	{
		return RunningCamera;
	}

	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Camera")
	FComposableCameraPose GetCurrentCameraPose() const
	{
		return CurrentCameraPose;
	}

	/** Read-only access to the Tier-1 context stack. Intended for debug
	 *  tooling (FComposableCameraDebugPanel, editor inspectors, tests).
	 *  Gameplay code should go through the PCM's ActivateCamera / Pop*
	 *  methods — do not mutate the stack through this pointer. */
	const UComposableCameraContextStack* GetContextStack() const { return ContextStack; }

	/** Read-only access to the modifier manager. Intended for debug tooling
	 *  (FComposableCameraDebugPanel's Modifier region). Gameplay code
	 *  should go through `AddModifier` / `RemoveModifier` on the PCM,
	 *  which also triggers reactivation on change. */
	const UComposableCameraModifierManager* GetModifierManager() const { return ModifierManager; }

	/**
	 * Copy the per-frame pose history ring into `OutHistory`, oldest entry
	 * first. The PCM captures one entry per `DoUpdateCamera` tick after
	 * the current-frame pose is finalized; capacity caps at
	 * `PoseHistoryCapacity` frames (~2 s at 60 fps).
	 *
	 * Debug-only consumer: the Pose History panel reads this every frame
	 * to render sparklines and hover tooltips. Not exposed to Blueprint —
	 * gameplay code should not depend on it.
	 *
	 * In shipping builds this is a no-op returning an empty array (the
	 * ring itself is `#if !UE_BUILD_SHIPPING`). The signature is kept in
	 * all configurations so panel code can call it unconditionally
	 * without per-config `#if` guards at every call site.
	 */
	void GetPoseHistory(TArray<FComposableCameraPoseHistoryEntry>& OutHistory) const;

	/** Fixed ring-buffer capacity. 120 frames ≈ 2 seconds at 60 fps, which
	 *  is enough to catch the "what happened half a second ago?" class of
	 *  debug questions without blowing memory. Per-entry footprint is
	 *  ~48 bytes so total is ~6 KB per PCM. */
	static constexpr int32 PoseHistoryCapacity = 120;

protected:
	FMinimalViewInfo GetCameraViewFromCameraPose(const FComposableCameraPose& OutPose) const;
	virtual void DoUpdateCamera(float DeltaTime) override;

private:
	// Update camera actions.
	void UpdateActions(float DeltaTime);

	// Build debug string for modifiers.
	void BuildModifierDebugString(FDisplayDebugManager& DisplayDebugManager);

	// ─── Type Asset Activation Helper ─────────────────────────────────────
	// FOnCameraFinishConstructed is a dynamic delegate that doesn't support BindLambda.
	// These transient members + UFUNCTION serve as the callback for ActivateNewCameraFromTypeAsset.

	/** Called by the dynamic delegate during type-asset-based camera activation. */
	UFUNCTION()
	void OnTypeAssetCameraConstructed(AComposableCameraCameraBase* Camera);

public:
	/**
	 * Resolve which transition to use when switching from one type-asset camera
	 * to another. Implements the five-tier resolution chain:
	 *
	 *   1. CallerOverride             (returned directly if non-null)
	 *   2. Transition table lookup    (exact A→B pair from project settings)
	 *   3. Source's ExitTransition    (SourceTypeAsset field — "always leave this way")
	 *   4. Target's EnterTransition   (TargetTypeAsset field — "always enter this way")
	 *   5. nullptr                    (hard cut)
	 *
	 * The table (tier 2) performs exact-match only — no wildcards. Per-camera
	 * ExitTransition and EnterTransition (tiers 3/4) serve as the per-camera
	 * fallbacks when no explicit pair is defined in the table.
	 *
	 * @param SourceTypeAsset  The type asset of the currently-running camera (may be nullptr).
	 * @param TargetTypeAsset  The type asset being activated (may be nullptr).
	 * @param CallerOverride   Explicit caller transition — if non-null, wins unconditionally.
	 * @return The resolved transition instance (owned by the type asset or table entry),
	 *         or nullptr for a hard cut. Caller must DuplicateObject before mutating.
	 */
	UComposableCameraTransitionBase* ResolveTransition(
		const UComposableCameraTypeAsset* SourceTypeAsset,
		const UComposableCameraTypeAsset* TargetTypeAsset,
		UComposableCameraTransitionDataAsset* CallerOverride) const;

	/**
	 * Prepare the pending type-asset state for a camera that is being resumed
	 * (e.g. after a context pop). If the camera was originally built from a type
	 * asset, this restores PendingTypeAsset / PendingParameterBlock and returns
	 * a callback bound to OnTypeAssetCameraConstructed. If not a type-asset
	 * camera, returns an empty (unbound) delegate.
	 *
	 * Called by ContextStack::PopActiveContextInternal so the resumed camera
	 * is fully reconstructed from its original type asset instead of producing
	 * an empty shell.
	 */
	FOnCameraFinishConstructed PrepareResumeCallback(AComposableCameraCameraBase* Camera);

private:
	UPROPERTY(Transient)
	TObjectPtr<UComposableCameraTypeAsset> PendingTypeAsset;

	/** Pending parameter block for the type-asset activation callback. Not a UPROPERTY — plain struct. */
	FComposableCameraParameterBlock PendingParameterBlock;

public:
	// ~~~~ Implicit Camera Activation (SetViewTarget bridge).
	//
	// When external code calls SetViewTarget (engine CameraCut handler, Possess,
	// SetViewTargetWithBlend, etc.) on an actor with a UCameraComponent, the PCM
	// automatically creates a transient proxy camera wrapping that actor and
	// activates it in the current context's director with a CCS transition
	// converted from FViewTargetTransitionParams. This is "implicit activation"
	// as opposed to the explicit ActivateCamera / ActivateNewCameraFromTypeAsset path.
	// ~~~~

	// Whether to sync current camera rotation to ControlRotation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComposableCameraSystem")
	bool bSyncToControlRotation { false };

	// The currently active context name (debug, read-only).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "ComposableCameraSystem")
	FName CurrentContext;

	// Current running camera.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "ComposableCameraSystem")
	AComposableCameraCameraBase* RunningCamera;

	// Current camera pose. 
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "ComposableCameraSystem")
	FComposableCameraPose CurrentCameraPose;

	// Current camera actions.
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "ComposableCameraSystem")
	TSet<UComposableCameraActionBase*> CameraActions;

	UPROPERTY(Transient)
	FOnCameraFinishConstructed CurrentOnPreBeginplayEvent;

private:
	UPROPERTY(Transient)
	TObjectPtr<UComposableCameraContextStack> ContextStack;

	UPROPERTY(Transient)
	TObjectPtr<UComposableCameraModifierManager> ModifierManager;

	UPROPERTY(Transient)
	FMinimalViewInfo LastDesiredView;

	// ─── Implicit Camera Activation State ────────────────────────────────
	/** Guard against re-entrant SetViewTarget calls during implicit activation.
	 *  When the PCM calls ActivateNewCamera internally, the Director may call
	 *  Super::SetViewTarget as part of its bookkeeping — the guard prevents
	 *  that from recursing back into implicit activation. */
	bool bIsImplicitlyActivating { false };

#if !UE_BUILD_SHIPPING
	// ─── Pose History Ring Buffer (debug only) ───────────────────────────
	//
	// Flat fixed-size array + head index. Writer advances HeadIndex each
	// frame; reader copies out into a chronological TArray by walking
	// from HeadIndex forward (oldest) to HeadIndex - 1 (newest).
	//
	// Ring grows up to PoseHistoryCapacity entries during the warm-up,
	// then stays at full capacity with overwrite-oldest semantics. No
	// allocation on the hot path past the initial reserve.
	TArray<FComposableCameraPoseHistoryEntry> PoseHistoryRing;
	int32                                     PoseHistoryHead      = 0;     // next write index
	int32                                     PoseHistoryCountUsed = 0;     // # entries actually populated (up to capacity)

	/** Capture one frame into the ring. Called from `DoUpdateCamera` after
	 *  `CurrentCameraPose` is finalized. */
	void CaptureCurrentFrameToPoseHistory();

public:
	/** Whether the pose-history ring buffer is currently frozen (driven by
	 *  `CCS.Debug.Panel.PoseHistory.Freeze`). Read-only accessor for the
	 *  debug panel so it can render a `[FROZEN]` indicator in the title bar
	 *  without having to duplicate the CVar declaration. Debug-only; not
	 *  declared outside `!UE_BUILD_SHIPPING`. */
	static bool IsPoseHistoryFrozen();
#endif
};