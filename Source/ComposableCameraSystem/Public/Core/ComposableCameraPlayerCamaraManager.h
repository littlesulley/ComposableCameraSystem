// Copyright Sulley. All rights reserved.

#pragma once

#include "Camera/PlayerCameraManager.h"
#include "GameplayTagContainer.h"
#include "Transitions/ComposableCameraTransitionBase.h"
#include "ComposableCameraPlayerCamaraManager.generated.h"

class UComposableCameraDirector;
	
UCLASS(ClassGroup = ComposableCameraSystem, NotPlaceable)
class COMPOSABLECAMERASYSTEM_API AComposableCameraPlayerCamaraManager
	: public APlayerCameraManager
{
	GENERATED_BODY()

public:
	AComposableCameraPlayerCamaraManager(const FObjectInitializer& ObjectInitializer);
	virtual void BeginPlay() override;
	virtual void InitializeFor(APlayerController* PlayerController) override;
	virtual void SetViewTarget(AActor* NewViewTarget, FViewTargetTransitionParams TransitionParams = FViewTargetTransitionParams()) override;
	virtual void ProcessViewRotation(float DeltaTime, FRotator& OutViewRotation, FRotator& OutDeltaRot) override;

	AComposableCameraCameraBase* ActivateNewCamera(
	AComposableCameraPlayerCamaraManager* PlayerCameraManager,
		TSubclassOf<AComposableCameraCameraBase> CameraClass,
		FComposableCameraTransitionParams TransitionParams,
		UDataTable* NodeInitializerDataTable,
		FGameplayTagContainer NodeInitializerTags,
		bool bIsTransient,
		float LifeTime,
		FOnCameraFinishConstructed OnPreBeginplayEvent);

	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Camera")
	AComposableCameraCameraBase* GetRunningCamera () const
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

private:
	UPROPERTY(Transient)
	TObjectPtr<UComposableCameraDirector> Director;

	UPROPERTY(Transient)
	FMinimalViewInfo LastDesiredView;
};
	