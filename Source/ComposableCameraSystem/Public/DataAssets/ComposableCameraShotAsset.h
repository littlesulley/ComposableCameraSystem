// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataAssets/ComposableCameraShot.h"
#include "Engine/DataAsset.h"
#include "ComposableCameraShotAsset.generated.h"

/**
 * Reusable Shot data asset. A `UDataAsset` wrapping one `FComposableCameraShot`.
 *
 * Phase E of Shot-Based Keyframing introduces two storage modes for Shot data
 * carried by a `UMovieSceneComposableCameraShotSection` (spec Section 3.4.1):
 *
 *   - **Inline**: Shot value-typed inside the Section. One-off framing for
 *     a specific moment in a sequence.
 *   - **AssetReference**: Section soft-refs a `UComposableCameraShotAsset`.
 *     Editing the asset propagates to every Section referencing it. Suitable
 *     for "close-up A", "two-shot wide", reusable framing presets.
 *
 * This class is the AssetReference target. It carries no behavior of its own
 *. It is a data envelope. The Shot Editor (Phase D) opens it via the
 * Sequencer Section selection sync (Phase E.5) or directly from the Content
 * Browser via `UAssetDefinition_ComposableCameraShotAsset::OpenAssets`.
 *
 * The shot asset is **not** related to `UComposableCameraTypeAsset` /
 * `UComposableCameraPatchTypeAsset`; those carry node graphs and are run by
 * the camera evaluation pipeline. ShotAsset just stores a `FComposableCameraShot`
 * struct that the Shot Track's evaluator pushes into the runtime
 * `UComposableCameraCompositionFramingNode::Shot` UPROPERTY at the playhead.
 */
UCLASS(BlueprintType, ClassGroup = ComposableCameraSystem,
	meta = (DisplayName = "Composable Camera Shot"))
class COMPOSABLECAMERASYSTEM_API UComposableCameraShotAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Authored shot. Edited via the Shot Editor (Phase D) or the Details
	 *  panel when opened directly from the Content Browser. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shot",
		meta = (ShowOnlyInnerProperties))
	FComposableCameraShot Shot;
};
