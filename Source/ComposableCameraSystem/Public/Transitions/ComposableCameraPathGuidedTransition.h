// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraTransitionBase.h"
#include "ComposableCameraPathGuidedTransition.generated.h"

class ACameraRig_Rail;

UENUM()
enum class EComposableCameraPathGuidedTransitionType : uint8
{
	// The camera will be put exactly on spline, if specified.
	HardGuide,

	// The camera is attracted to the path and can not be on the spline.
	SoftGuide
};

/**
 * A transition which utilizes a path　(spline) to guide its position during transition.
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

	// How to guide the camera transition.
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	EComposableCameraPathGuidedTransitionType GuideType { EComposableCameraPathGuidedTransitionType::SoftGuide };

	// The strength that attracts the base position to spline. Should be large enough.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta = (ClampMin = "0", EditCondition = "GuideType == EComposableCameraPathGuidedTransitionType::SoftGuide", EditConditionHides))
	float AttractionStrength { 100.f };
	
	// Speed when the camera follows the attraction. Should be small.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta = (ClampMin = "0", ClampMax = "10", EditCondition = "GuideType == EComposableCameraPathGuidedTransitionType::SoftGuide", EditConditionHides))
	float FollowSpeed { 2.f };

	// Speed when camera returns back to the target position. Should be small enough.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta = (ClampMin = "0", ClampMax = "5", EditCondition = "GuideType == EComposableCameraPathGuidedTransitionType::SoftGuide", EditConditionHides))
	float ResumeSpeed { 0.5f };
	
	// Normalized timestamps to start/end guide. It's recommended to set a not-close-to-one end timestamp ensuring the camera can return to the desired target position smoothly.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta = (ClampMin = "0", ClampMax = "1", EditCondition = "GuideType == EComposableCameraPathGuidedTransitionType::SoftGuide", EditConditionHides))
	FVector2D GuideRange { 0, 0.75 };
	
	// How the virtual camera should move on spline. This curve is normalized. Input range is [0,1], start c[0]=0, c[1]=1.
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	UCurveFloat* SplineMoveCurve;
	
	// How to interpolate between the base camera and the virtual spline camera, if GlobalMoveCurve is not specified.
	// This curve is normalized. Input range is [0,1], start c[0]=0, c[1]=0.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta = (EditCondition = "GuideType == EComposableCameraPathGuidedTransitionType::HardGuide", EditConditionHides))
	UCurveFloat* InterpCurve;

private:
	ACameraRig_Rail* Rail;
	FVector PreviousBasePosition {};
	FVector PreviousResultPosition {};
	float ActualAttracton { 0.f };

	FVector GetPositionOnSpline(float DurationPct);
};
