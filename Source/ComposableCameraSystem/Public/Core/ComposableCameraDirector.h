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
		TSubclassOf<AComposableCameraCameraBase> CameraClass,
		FComposableCameraTransitionParams TransitionParams,
		UDataTable* NodeInitializerDataTable,
		FGameplayTagContainer NodeInitializerTags,
		bool bIsTransient,
		float LifeTime);
	
	[[nodiscard]] FComposableCameraPose Evaluate(float DeltaTime);
	
private:
	FComposableCameraPose CurrentCameraPose;
	UComposableCameraEvaluationTree* EvaluationTree;
	AComposableCameraCameraBase* RunningCamera;

	void OnActivateNewCamera(AComposableCameraCameraBase* NewCamera, UComposableCameraTransitionBase* Transition);

};
