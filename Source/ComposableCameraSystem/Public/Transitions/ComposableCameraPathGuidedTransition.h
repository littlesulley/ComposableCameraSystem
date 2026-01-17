// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraTransitionBase.h"
#include "ComposableCameraPathGuidedTransition.generated.h"

class ACameraRig_Rail;
class UComposableCameraInertializedTransition;

/**
 * A transition which utilizes a path　(spline) to guide its position during transition.
 * This transition leverages two InertializedTransitions to achieve smoothness.
 * An intermediate camera will be spawned as a wrapper for the spline.
 * So this transition will be more expensive than other transitions.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraPathGuidedTransition : public UComposableCameraTransitionBase
{
	GENERATED_BODY()

public:
	virtual void OnBeginPlay_Implementation(float DeltaTime, const FComposableCameraPose& CurrentTargetPose) override;
	virtual FComposableCameraPose OnEvaluate_Implementation(float DeltaTime, const FComposableCameraPose& CurrentTargetPose) override;

public:
	// Driving transition for base camera transition.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced)
	UComposableCameraTransitionBase* DrivingTransition;

	// The rail actor thet contains the desired guiding spline.
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	TSoftObjectPtr<ACameraRig_Rail> RailActor;

	// Normalized timestamps to start/end guide. It's recommended to set a not-close-to-one end timestamp ensuring the camera can return to the desired target position smoothly.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta = (ClampMin = "0", ClampMax = "1"))
	FVector2D GuideRange { 0.25, 0.75 };
	
	// How the virtual camera should move on spline. This curve is normalized. Input range is [0,1], start c[0]=0, c[1]=1.
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	UCurveFloat* SplineMoveCurve;
	
private:
	UPROPERTY()
	AComposableCameraCameraBase* IntermediateCamera { nullptr };
	
	UPROPERTY()
	ACameraRig_Rail* Rail;
	
	UPROPERTY()
	UComposableCameraInertializedTransition* EnterTransition { nullptr };
	
	UPROPERTY()
	UComposableCameraInertializedTransition* ExitTransition { nullptr };
	
	FComposableCameraPose PreviousResultPose;

	void DrawDebugSplinePoints(const TArray<FVector>& SplinePoints);
};
