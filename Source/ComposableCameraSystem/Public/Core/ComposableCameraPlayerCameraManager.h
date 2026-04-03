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
	
	AComposableCameraCameraBase* ActivateNewCamera(
		TSubclassOf<AComposableCameraCameraBase> CameraClass,
		UComposableCameraTransitionDataAsset* Transition,
		const FComposableCameraActivateParams& ActivationParams,
		FOnCameraFinishConstructed OnPreBeginplayEvent);

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
	// Used to maintain a maximum number of parent cameras in the camera chain. Default is 3.
	void RefreshCameraChain() const;

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
	TObjectPtr<UComposableCameraDirector> Director;
	
	UPROPERTY(Transient)
	TObjectPtr<UComposableCameraModifierManager> ModifierManager;

	UPROPERTY(Transient)
	FMinimalViewInfo LastDesiredView;
};
	