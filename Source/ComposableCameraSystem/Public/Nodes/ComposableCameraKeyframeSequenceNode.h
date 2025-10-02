// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "Nodes/ComposableCameraRelativeFixedPoseNode.h"
#include "ComposableCameraKeyframeSequenceNode.generated.h"

class ULevelSequence;
class ULevelSequencePlayer;
class UMovieSceneFloatSection;
class UMovieScene3DTransformSection;

/**
 * Node for playing a keyframed sequence relative to some transform.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraKeyframeSequenceNode : public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	virtual void OnBeginPlayNode_Implementation(const FComposableCameraPose& CurrentCameraPose) override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;

public:
	// The level sequence you want to use for setting camera pose.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	ULevelSequence* CameraSequence { nullptr };
	
	// Method to use for selecting the target transform.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraRelativeFixedPoseMethod Method;

	// Relative transform when method is RelativeToTransform.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "Method == EComposableCameraRelativeFixedPoseMethod::RelativeToTransform", EditConditionHides))
	FTransform RelativeTransform;

	// Relative actor when method is RelativeToActor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "Method == EComposableCameraRelativeFixedPoseMethod::RelativeToActor", EditConditionHides))
	AActor* RelativeActor;

	// Relative socket when method is RelativeToActor and the actor has a SkeletalMeshComponent. If no such component exists, will use RelativeActor's transform.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "Method == EComposableCameraRelativeFixedPoseMethod::RelativeToActor", EditConditionHides))
	FName RelativeSocket;

	// How long to stay at the last frame when the sequence finishes playing. >=0 means the time to stay. <0 means infinite waiting for an explicit ActivateNewCamera or TerminateCurrentCamera. 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	float StayAtLastFrameTime { 0.0f };

private:
	std::pair<float, FTransform> GetTargetTransform(FFrameTime FrameTime);

private:
	bool bValidCameraSequence { true };
	
	ULevelSequencePlayer* CameraPlayer { nullptr };
	UMovieSceneFloatSection* FOVSection { nullptr };
	UMovieScene3DTransformSection* TransformSection { nullptr };
	
	USkeletalMeshComponent* SkeletalMeshComponentForRelativeActor { nullptr };
	
	FFrameTime ElapsedFrames { 0 };
	float ElapsedTime { 0.0f };
	float ElapsedStayAtLastFrameTime { 0.f };
};
