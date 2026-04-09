// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "UObject/Object.h"
#include "ComposableCameraTransitionBase.generated.h"

class UComposableCameraTransitionBase;

DECLARE_MULTICAST_DELEGATE(FOnTransitionFinishes);

/**
 * Parameters passed when a transition is initialized.
 * Contains the source pose data needed for transitions to set up their internal state.
 */
USTRUCT(BlueprintType)
struct FComposableCameraTransitionInitParams
{
	GENERATED_BODY()

	/** Source pose at the moment the transition starts (the blended output the player was seeing). */
	UPROPERTY(BlueprintReadOnly)
	FComposableCameraPose CurrentSourcePose;

	/** Previous frame's source pose (for velocity-based transitions like inertialization). */
	UPROPERTY(BlueprintReadOnly)
	FComposableCameraPose PreviousSourcePose;

	/** Delta time of the frame when the transition was created. */
	UPROPERTY(BlueprintReadOnly)
	float DeltaTime { 0.f };
};

/**
 * Base class for transition evaluation.
 *
 * Transitions are pose-only operators: they receive source and target poses each tick,
 * maintain their own internal blend state, and output a blended pose.
 * They never reference cameras or Directors directly.
 */
UCLASS(Abstract, DefaultToInstanced, EditInlineNew, ClassGroup = ComposableCameraSystem, CollapseCategories)
class COMPOSABLECAMERASYSTEM_API UComposableCameraTransitionBase
	: public UObject
{
	GENERATED_BODY()

public:
	/** Evaluate the transition for this frame, blending between source and target poses. */
	FComposableCameraPose Evaluate(float DeltaTime, const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose);

	/** Initialize the transition with source pose data. Called once before the first Evaluate. */
	void TransitionEnabled(const FComposableCameraTransitionInitParams& InInitParams);

	/** Mark the transition as finished. */
	void TransitionFinished();

	void SetTransitionTime(float NewTransitionTime);
	void ResetTransitionState();

	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Transition")
	bool IsFinished() const { return bFinished; }

	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Transition")
	float GetRemainingTime() const { return RemainingTime; }

	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Transition")
	float GetTransitionTime() const { return TransitionTime; }

	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Transition")
	float GetPercentage() const { return Percentage; }

protected:
	/** Begin Play event. Called on the first frame of the transition, before the first OnEvaluate. \n
	 * Use this to construct or initialize internal parameters specialized for this type of transition. \n
	 * @param DeltaTime World delta time. \n
	 * @param CurrentSourcePose Current source camera pose. \n
	 * @param CurrentTargetPose Current target camera pose. \n
	 */
	UFUNCTION(BlueprintNativeEvent, DisplayName = "OnBeginPlay", Category = "ComposableCameraSystem|Transition")
	void OnBeginPlay(float DeltaTime, const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose);
	virtual void OnBeginPlay_Implementation(float DeltaTime, const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose) {}

	/** Event to customize the evaluation function for each tick. When calling this function, RemainingTime has already been decremented, and assured to not go below 0. \n
	 * @param DeltaTime World delta time. \n
	 * @param CurrentSourcePose Current source camera pose. \n
	 * @param CurrentTargetPose Current target camera pose. \n
	 * @return Returns the new blended camera pose.
	 */
	UFUNCTION(BlueprintNativeEvent, DisplayName = "OnTick", Category = "ComposableCameraSystem|Transition")
	FComposableCameraPose OnEvaluate(float DeltaTime, const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose);
	virtual FComposableCameraPose OnEvaluate_Implementation(float DeltaTime, const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose) { return FComposableCameraPose{}; }

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

	// Initialization parameters from TransitionEnabled.
	UPROPERTY(BlueprintReadOnly)
	FComposableCameraTransitionInitParams InitParams;

	// How much percentage this transition has completed.
	UPROPERTY(BlueprintReadOnly)
	float Percentage { 0.f };
};
