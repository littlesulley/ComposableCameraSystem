// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraTransitionBase.h"
#include "ComposableCameraPathGuidedTransition.generated.h"

class USplineComponent;
class ACameraRig_Rail;
class UComposableCameraInertializedTransition;

UENUM()
enum class EComposableCameraPathGuidedTransitionType : uint8
{
	// Use inertialized camera as a bridge to achieve path guided transition.
	Inertialized,
	
	// Use auto-generated splines to achieve path guided transition. \n
	// @NOTE: This type won't update TargetCameraPose, so if the target camera is moving during transition, DO NOT use this type.
	Auto
};

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
	// Driving transition for base camera transition. Used for both Inertialized and Auto.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced)
	UComposableCameraTransitionBase* DrivingTransition;

	// Type of path guided transition.
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	EComposableCameraPathGuidedTransitionType Type { EComposableCameraPathGuidedTransitionType::Inertialized };
	
	// The rail actor thet contains the desired guiding spline. The tangents of the spline should not be too small nor too large.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta = (EditCondition = "Type == EComposableCameraPathGuidedTransitionType::Inertialized", EditConditionHides))
	TSoftObjectPtr<ACameraRig_Rail> RailActor;

	// Normalized timestamps to start/end guide. It's recommended to set a not-close-to-one end timestamp ensuring the camera can return to the desired target position smoothly.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta = (ClampMin = "0", ClampMax = "1", EditCondition = "Type == EComposableCameraPathGuidedTransitionType::Inertialized", EditConditionHides))
	FVector2D GuideRange { 0.25, 0.75 };
	
	// How the virtual camera should move on spline. This curve is normalized. Input range is [0,1], start c[0]=0, c[1]=1.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta = (EditCondition = "Type == EComposableCameraPathGuidedTransitionType::Inertialized", EditConditionHides))
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
	
	UPROPERTY()
	USplineComponent* InternalSpline;
	
	UPROPERTY()
	AActor* DebugSplineActor;
	
private:
	void DrawDebugSplinePoints(const TArray<FVector>& SplinePoints);
	void BuildInternalSpline(const FComposableCameraPose& CurrentTargetPose, float DeltaTime);
};
