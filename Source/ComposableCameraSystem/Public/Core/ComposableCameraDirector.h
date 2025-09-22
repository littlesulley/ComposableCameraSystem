// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "Transitions/ComposableCameraTransitionBase.h"
#include "ComposableCameraDirector.generated.h"

class AComposableCameraCameraBase;
class UComposableCameraEvaluationTree;
class UComposableCameraTransitionBase;

UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraDirector : public UObject
{
	GENERATED_BODY()

public:
	UComposableCameraDirector(const FObjectInitializer& ObjectInitializer);

	AComposableCameraCameraBase* ActivateNewCamera(
		AComposableCameraPlayerCamaraManager* PlayerCameraManager,
		TSubclassOf<AComposableCameraCameraBase> CameraClass,
		FComposableCameraTransitionParams TransitionParams,
		UDataTable* NodeInitializerDataTable,
		FGameplayTagContainer NodeInitializerTags,
		bool bIsTransient,
		float LifeTime,
		FOnCameraFinishConstructed OnPreBeginplayEvent);
	
	[[nodiscard]] FComposableCameraPose Evaluate(float DeltaTime);
	
private:
	UPROPERTY(Transient)
	FComposableCameraPose CurrentCameraPose;

	UPROPERTY(Transient)
	UComposableCameraEvaluationTree* EvaluationTree;

	UPROPERTY(Transient)
	AComposableCameraCameraBase* RunningCamera;

	void OnActivateNewCamera(AComposableCameraCameraBase* NewCamera, UComposableCameraTransitionBase* Transition);

};
