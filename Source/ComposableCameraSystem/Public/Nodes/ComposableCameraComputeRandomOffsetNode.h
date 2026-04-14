// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/ComposableCameraComputeNodeBase.h"
#include "ComposableCameraComputeRandomOffsetNode.generated.h"

/**
 * Example compute node: generates a random offset vector at camera activation
 * and publishes it as an output pin.
 *
 * Typical use case: spawn-time camera shake seed, randomized starting position
 * jitter, or any one-shot random value that downstream camera nodes consume
 * every frame but that should remain stable across the camera's lifetime.
 *
 * The random offset is generated once in ExecuteBeginPlay and written to the
 * "RandomOffset" output pin. Downstream camera nodes (e.g. PivotOffset,
 * CameraOffset) can wire this into their input pins to apply the offset
 * every frame.
 *
 * Inputs:
 *   - MinOffset (Vector3D): minimum bound for each axis (default -50, -50, -50)
 *   - MaxOffset (Vector3D): maximum bound for each axis (default  50,  50,  50)
 *
 * Outputs:
 *   - RandomOffset (Vector3D): the generated random vector, stable for the
 *     camera's lifetime
 */
UCLASS(ClassGroup = ComposableCameraSystem, meta = (DisplayName = "Compute: Random Offset", ToolTip = "Generates a stable random offset vector at camera activation for downstream nodes."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraComputeRandomOffsetNode
	: public UComposableCameraComputeNodeBase
{
	GENERATED_BODY()

public:
	virtual void ExecuteBeginPlay() override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

	/** Minimum bound for each axis of the random offset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameters")
	FVector MinOffset = FVector(-50.0, -50.0, -50.0);

	/** Maximum bound for each axis of the random offset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameters")
	FVector MaxOffset = FVector(50.0, 50.0, 50.0);
};
