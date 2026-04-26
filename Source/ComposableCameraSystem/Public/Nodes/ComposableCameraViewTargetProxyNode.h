// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "ComposableCameraViewTargetProxyNode.generated.h"

class UCameraComponent;

/**
 * A lightweight camera node that reads FMinimalViewInfo from a target actor's
 * UCameraComponent each tick and converts it into a FComposableCameraPose.
 *
 * This node is NOT meant to be placed in a camera type asset by designers.
 * It is created programmatically by the PCM's SetViewTarget override (implicit
 * camera activation) to relay an external camera's state into CCS.
 *
 * SetViewTargetActor() points to a specific actor. The node reads from that
 * actor's UCameraComponent every tick. If the target actor is missing or has
 * no UCameraComponent, the node outputs the unmodified input pose.
 */
UCLASS(NotBlueprintable, Hidden, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraViewTargetProxyNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	UComposableCameraViewTargetProxyNode() { PaletteCategory = TEXT("Composition"); }

public:
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;

	// Reads FMinimalViewInfo from an external UCameraComponent and writes it over
	// the pose — bypasses the upstream InPose entirely. Also not designer-placed
	// (created programmatically by PCM::SetViewTarget), but flagged for parity.
	virtual EComposableCameraNodePatchCompatibility GetPatchCompatibility_Implementation() const override
	{
		return EComposableCameraNodePatchCompatibility::Incompatible;
	}

	/** Set the actor and cache its UCameraComponent. */
	void SetViewTargetActor(AActor* InActor);

	/** Get the current view target actor. */
	AActor* GetViewTargetActor() const { return ViewTargetActor.Get(); }

private:
	/** The actor whose UCameraComponent provides the camera view each tick. */
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> ViewTargetActor;

	/** Cached camera component — resolved once in SetViewTargetActor. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCameraComponent> CachedCameraComponent;
};
