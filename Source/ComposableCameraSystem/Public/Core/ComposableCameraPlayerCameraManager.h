// Copyright Sulley. All rights reserved.

#pragma once

#include "ComposableCameraModifierManager.h"
#include "Camera/PlayerCameraManager.h"
#include "Transitions/ComposableCameraTransitionBase.h"
#include "ComposableCameraNamespaces.h"
#include "ComposableCameraPlayerCameraManager.generated.h"

class UComposableCameraActionBase;
class UComposableCameraTransitionDataAsset;
class UComposableCameraNodeModifierDataAsset;
class UComposableCameraModifierManager;
class UComposableCameraDirector;
class UComposableCameraContextStack;
	
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

protected:
	FMinimalViewInfo GetCameraViewFromCameraPose(const FComposableCameraPose& OutPose) const;
	virtual void DoUpdateCamera(float DeltaTime) override;

private:
	// Update camera actions.
	void UpdateActions(float DeltaTime);
	
	// Build debug string for modifiers.
	void BuildModifierDebugString(FDisplayDebugManager& DisplayDebugManager);

public:
	// Whether to sync current camera rotation to ControlRotation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComposableCameraSystem")
	bool bSyncToControlRotation { false };

	// Whether to draw debug information during runtime.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComposableCameraSystem")
	bool bDrawDebugInformation { false };

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
	UComposableCameraNodeInitializerDataAsset* CurrentNodeInitializerDataAsset;

	UPROPERTY(Transient)
	FOnCameraFinishConstructed CurrentOnPreBeginplayEvent;

private:
	UPROPERTY(Transient)
	TObjectPtr<UComposableCameraContextStack> ContextStack;

	UPROPERTY(Transient)
	TObjectPtr<UComposableCameraModifierManager> ModifierManager;

	UPROPERTY(Transient)
	FMinimalViewInfo LastDesiredView;
};
	