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
class UComposableCameraNodeModifierDataAsset;
class UComposableCameraTransitionDataAsset;

UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraDirector : public UObject
{
	GENERATED_BODY()

public:
	UComposableCameraDirector(const FObjectInitializer& ObjectInitializer);

	AComposableCameraCameraBase* ResumeCamera(
		AComposableCameraCameraBase* ResumeCamera,
		UComposableCameraTransitionBase* Transition,
		const FTransform& Transform);
	
	AComposableCameraCameraBase* ActivateNewCamera(
		AComposableCameraPlayerCamaraManager* PlayerCameraManager,
		TSubclassOf<AComposableCameraCameraBase> CameraClass,
		UComposableCameraTransitionDataAsset* TransitionDataAsset,
		FTransform InitialTransform,
		UComposableCameraNodeInitializerDataAsset* NodeInitializerDataAsset,
		bool bIsTransient,
		float LifeTime,
		FOnCameraFinishConstructed OnPreBeginplayEvent);
	
	[[nodiscard]] FComposableCameraPose Evaluate(float DeltaTime) const;

private:
	UPROPERTY(Transient)
	UComposableCameraEvaluationTree* EvaluationTree { nullptr };

	UPROPERTY(Transient)
	AComposableCameraCameraBase* RunningCamera { nullptr };

	void OnActivateNewCamera(AComposableCameraCameraBase* NewCamera, UComposableCameraTransitionBase* Transition);

};
