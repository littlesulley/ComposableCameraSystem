// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraPlayerCamaraManager.h"
#include "UObject/Object.h"
#include "Cameras/ComposableCameraCameraBase.h"
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

	AComposableCameraCameraBase* CreateNewCamera(
		AComposableCameraPlayerCamaraManager* PlayerCameraManager,
		TSubclassOf<AComposableCameraCameraBase> CameraClass,
		const FComposableCameraActivateParams& ActivationParams);
	
	AComposableCameraCameraBase* ActivateNewCamera(
		AComposableCameraPlayerCamaraManager* PlayerCameraManager,
		TSubclassOf<AComposableCameraCameraBase> CameraClass,
		UComposableCameraTransitionDataAsset* TransitionDataAsset,
		const FComposableCameraActivateParams& ActivationParams,
		FOnCameraFinishConstructed OnPreBeginplayEvent);

	AComposableCameraCameraBase* ReactivateCurrentCamera(
		AComposableCameraPlayerCamaraManager* PlayerCameraManager,
		TSubclassOf<AComposableCameraCameraBase> CameraClass,
		UComposableCameraTransitionBase* Transition,
		UComposableCameraNodeInitializerDataAsset* NodeInitializerDataAsset,
		const FOnCameraFinishConstructed& OnPreBeginplayEvent);
	
	[[nodiscard]] FComposableCameraPose Evaluate(float DeltaTime) const;

private:
	UPROPERTY(Transient)
	UComposableCameraEvaluationTree* EvaluationTree { nullptr };

	UPROPERTY(Transient)
	AComposableCameraCameraBase* RunningCamera { nullptr };

	void ForceCameraPoses(AComposableCameraCameraBase* Camera, const FTransform& Transform);
	void OnActivateNewCamera(AComposableCameraCameraBase* NewCamera, UComposableCameraTransitionBase* Transition);

};
