// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "UObject/Object.h"
#include "ComposableCameraTransitionBase.generated.h"

class UComposableCameraTransitionBase;

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
	void TransitionEnabled(FComposableCameraPose CurrentCameraPose, float TransitionTime);
	void TransitionFinished();
	
	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Transition")
	FComposableCameraPose GetStartCameraPose() const { return  StartCameraPose; }

	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Transition")
	bool IsFinished() const { return bFinished; }

	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Transition")
	float GetRemainingTime() const { return RemainingTime; }

	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Transition")
	float GetTransitionTime() const { return TransitionTime; }
	
protected:
	virtual FComposableCameraPose OnEvaluate(float DeltaTime, const FComposableCameraPose& CurrentTargetPose) PURE_VIRTUAL(UComposableCameraTransitionBase::Evaluate, return FComposableCameraPose{};);

	/**
	 * Event to customize the evaluation function for each tick. When calling this function, RemainingTime has already been decremented, and assured to not go below 0. \n
	 * @param DeltaTime World delta time. \n
	 * @param CurrentTargetPose Current target camera pose for this frame. Don't forget to use the automatically cached StartCameraPose. \n
	 * @param OutPose Output blended camera pose. \n
	 * @return Whether to use the result of OutPose for this frame.
	 */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "OnTransitionTick"), Category = "ComposableCameraSystem|Transition")
	bool OnTransitionEvaluate(float DeltaTime, const FComposableCameraPose& CurrentTargetPose, FComposableCameraPose& OutPose);

	/**
	 * Event when transition enabled, before any tick applies. This is where your custom transition creates internal variables and sets their default values.
	 */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "OnTransitionEnabled"), Category = "ComposableCameraSystem|Transition")
	void OnTransitionEnabled();
	
	/**
	 * Event when transitiono finishes. The base class simply sets bFinished to true.
	 */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "OnTransitionFinish"), Category = "ComposableCameraSystem|Transition")
	void OnTransitionFinished();
	
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
};
