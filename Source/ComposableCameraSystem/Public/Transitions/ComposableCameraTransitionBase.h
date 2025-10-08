// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "UObject/Object.h"
#include "ComposableCameraTransitionBase.generated.h"

class UComposableCameraTransitionBase;

DECLARE_MULTICAST_DELEGATE(FOnTransitionFinishes);

/**
 * Parameters for a camera transition.
 */
USTRUCT(BlueprintType)
struct FComposableCameraTransitionParams
{
	GENERATED_BODY()

public:
	// Transition class.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSubclassOf<UComposableCameraTransitionBase> TransitionClass;

	// Transition time.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float TransitionTime;
};

/**
 * Base class during transition evaluation.
 */
UCLASS(Abstract, DefaultToInstanced, EditInlineNew, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraTransitionBase
	: public UObject
{
	GENERATED_BODY()

public:
	FComposableCameraPose Evaluate(float DeltaTime, const FComposableCameraPose& CurrentTargetPose);
	void TransitionEnabled(AComposableCameraCameraBase* SourceCamera, AComposableCameraCameraBase* TargetCamera, const FComposableCameraPose& CurrentSourceCameraPose, float TransitionTime);
	void TransitionFinished();
	
	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Transition")
	FComposableCameraPose GetStartCameraPose() const { return StartCameraPose; }

	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Transition")
	bool IsFinished() const { return bFinished; }

	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Transition")
	float GetRemainingTime() const { return RemainingTime; }

	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Transition")
	float GetTransitionTime() const { return TransitionTime; }
	
protected:
	/** Begin Play event. Called when this is the first frame of transition and both source camera and target camera are evaluated, but before the first Tick event is called. \n
	 * This is usually for constructing or initializing internal parameters specialized for this type of transition, e.g., the Inertialized Transition. \n
	 * @param DeltaTime World delta time. \n
	 * @param CurrentTargetPose Current target camera pose. \n
	 * @return Returns the new blended camera pose.
	 */
	UFUNCTION(BlueprintNativeEvent, DisplayName = "OnBeginPlay", Category = "ComposableCameraSystem|Transition")
	void OnBeginPlay(float DeltaTime, const FComposableCameraPose& CurrentTargetPose);
	virtual void OnBeginPlay_Implementation(float DeltaTime, const FComposableCameraPose& CurrentTargetPose) {}

	/** Event to customize the evaluation function for each tick. When calling this function, RemainingTime has already been decremented, and assured to not go below 0. \n
	 * @param DeltaTime World delta time. \n
	 * @param CurrentTargetPose Current target camera pose. \n
	 * @return Returns the new blended camera pose.
	 */
	UFUNCTION(BlueprintNativeEvent, DisplayName = "OnTick", Category = "ComposableCameraSystem|Transition")
	FComposableCameraPose OnEvaluate(float DeltaTime, const FComposableCameraPose& CurrentTargetPose);
	virtual FComposableCameraPose OnEvaluate_Implementation(float DeltaTime, const FComposableCameraPose& CurrentTargetPose) { return FComposableCameraPose{}; }

	/**
	 * Event when the transition finishes. The base class simply sets bFinished to true.
	 */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "OnTransitionFinish"), Category = "ComposableCameraSystem|Transition")
	void OnFinished();

public:
	FOnTransitionFinishes OnTransitionFinishesDelegate;
	
protected:
	// Camera pose when transition starts.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FComposableCameraPose StartCameraPose;
	
	// Transition time.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float TransitionTime;

	// Remaining transition time.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float RemainingTime;

	// If finished transition.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	bool bFinished { false };

	// If at the first frame of transition.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	bool bFirstFrame { true };

	// Source camera.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	AComposableCameraCameraBase* SourceCamera;

	// Target camera.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	AComposableCameraCameraBase* TargetCamera;
};
