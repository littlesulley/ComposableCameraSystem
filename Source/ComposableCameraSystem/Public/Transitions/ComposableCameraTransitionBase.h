// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "UObject/Object.h"
#include "ComposableCameraTransitionBase.generated.h"

class UComposableCameraTransitionBase;

DECLARE_MULTICAST_DELEGATE(FOnTransitionFinishes);

/**
 * Base class during transition evaluation.
 */
UCLASS(Abstract, DefaultToInstanced, EditInlineNew, ClassGroup = ComposableCameraSystem, CollapseCategories)
class COMPOSABLECAMERASYSTEM_API UComposableCameraTransitionBase
	: public UObject
{
	GENERATED_BODY()

public:
	FComposableCameraPose Evaluate(float DeltaTime, const FComposableCameraPose& CurrentTargetPose);
	void TransitionEnabled(AComposableCameraCameraBase* SourceCamera, AComposableCameraCameraBase* TargetCamera, const FComposableCameraPose& CurrentSourceCameraPose);
	void TransitionFinished();

	void SetTransitionTime(float NewTransitionTime);
	void ResetTransitionState();
	
	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Transition")
	FComposableCameraPose GetStartCameraPose() const { return StartCameraPose; }

	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Transition")
	bool IsFinished() const { return bFinished; }

	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Transition")
	float GetRemainingTime() const { return RemainingTime; }

	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Transition")
	float GetTransitionTime() const { return TransitionTime; }

	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Transition")
	float GetPercentage() const { return Percentage; }
	
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
	// Transition time.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float TransitionTime;

	// Remaining transition time.
	UPROPERTY(BlueprintReadOnly)
	float RemainingTime;

	// If finished transition.
	UPROPERTY(BlueprintReadOnly)
	bool bFinished { false };

	// If at the first frame of transition.
	UPROPERTY(BlueprintReadOnly)
	bool bFirstFrame { true };

	// Camera pose when transition starts.
	UPROPERTY(BlueprintReadOnly)
	FComposableCameraPose StartCameraPose;

	// Source camera.
	UPROPERTY(BlueprintReadOnly)
	AComposableCameraCameraBase* SourceCamera;

	// Target camera.
	UPROPERTY(BlueprintReadOnly)
	AComposableCameraCameraBase* TargetCamera;

	// How much percentage this transition has completed.
	UPROPERTY(BlueprintReadOnly)
	float Percentage { 0.f };
};
