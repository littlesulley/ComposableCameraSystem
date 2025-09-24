// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "UObject/Object.h"
#include "ComposableCameraEvaluationTree.generated.h"

class UComposableCameraTransitionBase;
class AComposableCameraCameraBase;

/**
 * Evaluation tree for each tick. It's used as the root place to evaluate the final camera pose.
 */
UCLASS(Classgroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraEvaluationTree
	: public UObject
{
	GENERATED_BODY()

public:
	[[nodiscard]] FComposableCameraPose Evaluate(float DeltaTime);
	void OnActivateNewCamera(AComposableCameraCameraBase* NewCamera, UComposableCameraTransitionBase* Transition);

private:
	// Last camera pose.
	FComposableCameraPose LastCameraPose;
	
	// Currently running camera.
	AComposableCameraCameraBase* RunningCamera;

	// Current transition.
	UComposableCameraTransitionBase* Transition;
};
