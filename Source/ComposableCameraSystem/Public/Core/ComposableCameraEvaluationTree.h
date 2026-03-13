// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "UObject/Object.h"
#include "ComposableCameraEvaluationTree.generated.h"

class UComposableCameraTransitionBase;
class AComposableCameraCameraBase;
class FComposableCameraEvaluationTreeNode;

struct FComposableCameraEvaluationTreeLeafNodeWrapper
{
	AComposableCameraCameraBase* RunningCamera { nullptr };
	FComposableCameraPose Evaluate(float DeltaTime);
};

struct FComposableCameraEvaluationTreeInnerNodeWrapper
{
	UComposableCameraTransitionBase* Transition { nullptr };
	FComposableCameraEvaluationTreeNode& LeftNode;
	FComposableCameraEvaluationTreeNode& RightNode;
	
	bool bFreezePreviousCamera { false };
	const FComposableCameraPose FreezedCameraPose;
	
	FComposableCameraPose Evaluate(float DeltaTime);
};

/** A basic evaluation tree node. */
USTRUCT()
struct FComposableCameraEvaluationTreeNode 
{
	GENERATED_BODY()

public:
	// This node can wrap either a transition or a running camera depending on its leaf node or inner node.
	TVariant<FComposableCameraEvaluationTreeLeafNodeWrapper, FComposableCameraEvaluationTreeInnerNodeWrapper> Wrapper;

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
	UComposableCameraEvaluationTree(const FObjectInitializer& ObjectInitializer);
	
	[[nodiscard]] FComposableCameraPose Evaluate(float DeltaTime);
	void OnActivateNewCamera(AComposableCameraCameraBase* NewCamera, UComposableCameraTransitionBase* Transition);

private:
	// Evaluation tree.
	TArray<FComposableCameraEvaluationTreeNode> EvaluationTree;

	FComposableCameraEvaluationTreeNode& GetRootNode();
	const FComposableCameraEvaluationTreeNode& GetRootNode() const;
	
	
	// Currently running camera.
	UPROPERTY(Transient)
	AComposableCameraCameraBase* RunningCamera;

	// Current transition.
	UPROPERTY(Transient)
	UComposableCameraTransitionBase* Transition;
};
