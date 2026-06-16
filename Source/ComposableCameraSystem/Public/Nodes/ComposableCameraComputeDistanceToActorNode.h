// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/ComposableCameraComputeNodeBase.h"
#include "ComposableCameraComputeDistanceToActorNode.generated.h"

/**
 * Example compute node: measures the distance between two actors at camera
 * activation and publishes the result.
 *
 * Typical use case: at activation time, compute the distance between the
 * player and a target, then feed that into downstream camera nodes to scale
 * a boom arm length, set an initial FOV, or pick a blend weight. Values
 * that are sampled once and held stable for the camera's lifetime.
 *
 * Inputs:
 *   - ActorA (Actor): first actor (e.g. the player pawn)
 *   - ActorB (Actor): second actor (e.g. the look-at target)
 *
 * Outputs:
 *   - Distance (Float): Euclidean distance between the two actors
 *   - Direction (Vector3D): unit direction from ActorA to ActorB
 */
UCLASS(ClassGroup = ComposableCameraSystem, meta = (DisplayName = "Begin Play: Distance To Actor", ToolTip = "Measures distance and direction between two actors at camera activation."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraComputeDistanceToActorNode
	: public UComposableCameraComputeNodeBase
{
	GENERATED_BODY()

public:
	UComposableCameraComputeDistanceToActorNode() { PaletteCategory = TEXT("Math"); }

public:
	virtual void ExecuteBeginPlay() override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;
};
