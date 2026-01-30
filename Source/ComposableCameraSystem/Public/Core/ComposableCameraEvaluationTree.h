// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "UObject/Object.h"
#include "ComposableCameraEvaluationTree.generated.h"

class UComposableCameraTransitionBase;
class AComposableCameraCameraBase;

USTRUCT()
struct FComposableCameraEvaluationTreeLeafNode
{
	GENERATED_BODY()

public:
	UPROPERTY(Transient)
	AComposableCameraCameraBase* RunningCamera;

public:
	FComposableCameraPose Evaluate(float DeltaTime);
};

USTRUCT()
struct FComposableCameraEvaluationTreeInnerNode
{
	GENERATED_BODY()

public:
	UPROPERTY(Transient)
	UComposableCameraTransitionBase* Transition;
	
	bool bFreezePreviousCamera { false };
	FComposableCameraPose FreezedCameraPose;
	FComposableCameraEvaluationTreeLeafNode& RightNode;
	FComposableCameraEvaluationTreeInnerNode& LeftNode;
	
public:
	FComposableCameraPose Evaluate(float DeltaTime);
};

/**
 * Evaluation tree for each tick. It's used as the root place to evaluate the final camera pose. \n
 * When a new camera is activated, it will be added to the EvaluationTree array and start transition. \n
 * You should be very careful about *transient* cameras, because they may break the camera chain you'd expect.
 */
UCLASS(Classgroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraEvaluationTree
	: public UObject
{
	GENERATED_BODY()

public:
	using Node = TVariant<FComposableCameraEvaluationTreeLeafNode, FComposableCameraEvaluationTreeInnerNode>;
	UComposableCameraEvaluationTree(const FObjectInitializer& ObjectInitializer);
	
	[[nodiscard]] FComposableCameraPose Evaluate(float DeltaTime);
	void OnActivateNewCamera(AComposableCameraCameraBase* NewCamera, UComposableCameraTransitionBase* Transition);

private:
	// Evaluation tree.
	TArray<Node> EvaluationTree;

	Node& GetRootNode();
	
	
	// Currently running camera.
	UPROPERTY(Transient)
	AComposableCameraCameraBase* RunningCamera;

	// Current transition.
	UPROPERTY(Transient)
	UComposableCameraTransitionBase* Transition;
};
