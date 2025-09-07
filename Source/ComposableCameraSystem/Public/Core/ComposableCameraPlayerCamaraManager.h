// Copyright Sulley. All rights reserved.

#pragma once

#include "Camera/PlayerCameraManager.h"
#include "GameplayTagContainer.h"
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

	AComposableCameraCameraBase* ActivateNewCamera(TSubclassOf<AComposableCameraCameraBase> CameraClass, UDataTable* NodeInitializerDataTable, FGameplayTagContainer NodeInitializerTags, bool bIsTransient, float LifeTime);
	
protected:
	virtual void DoUpdateCamera(float DeltaTime) override;

	// Current running camera. 
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = Camera)
	AComposableCameraCameraBase* RunningCamera;

public:
	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Camera")
	AComposableCameraCameraBase* GetRunningCamera () const
	{
		return RunningCamera;
	}
	
private:
	TObjectPtr<UComposableCameraDirector> Director;
};
	