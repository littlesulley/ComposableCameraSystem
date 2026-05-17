// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/ComposableCameraParameterBlock.h"
#include "StructUtils/PropertyBag.h"
#include "ComposableCameraTypeAssetReference.generated.h"

class UComposableCameraTypeAsset;

/**
 * Designer-facing wrapper for a UComposableCameraTypeAsset + its exposed
 * parameters + exposed variables, laid out as two FInstancedPropertyBag fields.
 *
 * This is the struct a UComposableCameraLevelSequenceComponent owns. It exists
 * for two reasons:
 *
 * 1) Sequencer needs something it can bind standard property tracks against.
 *    Stock FMovieSceneFloatTrack / FMovieSceneDoubleVectorTrack / etc. walk
 *    FProperty paths on the bound object; we can't invent our own channel types
 *    per CCS pin type without writing a lot of custom MovieScene sections. The
 *    FInstancedPropertyBag route gives us type-correct FProperty's for free.
 *
 * 2) Designers editing the component in the Details panel need one field per
 *    exposed parameter (typed float / vector / actor picker / -, matching what
 *    the TypeAsset declared. FInstancedPropertyBag renders exactly that.
 *
 * Parameters vs Variables
 * -----------------------
 * The TypeAsset's ExposedParameters and ExposedVariables arrays are kept as
 * separate bags intentionally. They correspond to visually-distinct
 * categories in the Sequencer "Add Track" menu ("Camera Parameters" vs
 * "Camera Variables") and eliminate any chance of name collision between a
 * parameter and a variable that happen to share a name at the TypeAsset level.
 *
 * Lifecycle
 * ---------
 * RebuildBagsFromTypeAsset() must be called whenever TypeAsset changes (the
 * component calls it from PostEditChangeProperty). Values for properties whose
 * name + type survive the rebuild are carried over; everything else is reset
 * to the bag's default for that type.
 *
 * At camera activation time, BuildParameterBlock() walks both bags and emits a
 * FComposableCameraParameterBlock ready to hand to
 * UE::ComposableCameras::ConstructCameraFromTypeAsset.
 */
// Not BlueprintType: FInstancedPropertyBag is not Blueprint-supported, so the
// reference struct must not be BP-exposed as a whole. Designers still edit it
// through the Details panel (EditAnywhere) and C++ / editor code reads the bags
// directly; Blueprint just can't introspect it. The TypeAsset field is still a
// plain TObjectPtr which BP could handle on its own, but we don't need that
// surface in Phase B.
USTRUCT()
struct COMPOSABLECAMERASYSTEM_API FComposableCameraTypeAssetReference
{
	GENERATED_BODY()

	/** The TypeAsset this reference targets. Changing this triggers
	 *  RebuildBagsFromTypeAsset on the owning component. */
	UPROPERTY(EditAnywhere, Category = "Camera")
	TObjectPtr<UComposableCameraTypeAsset> TypeAsset;

	/** One entry per TypeAsset::ExposedParameters, typed according to each
	 *  exposed parameter's PinType.
	 *  - FixedLayout prevents the designer from reshaping the bag by hand in
	 *    the Details panel. Its structure is derived from the TypeAsset and
	 *    must only be mutated via RebuildBagsFromTypeAsset.
	 *  - We deliberately do NOT set meta=(InterpBagProperties=true) here.
	 *    That metadata would make Sequencer's core drill-in walk the bag
	 *    automatically and surface leaves through a deep "TypeAssetReference
	 *    -Parameters -Value -Leaf" chain. Duplicating what our own
	 *    FComposableCameraLevelSequenceComponentTrackEditor already surfaces
	 *    at two levels (Camera Parameters -Leaf). Instead, we only rely on
	 *    CPF_Interp being set on each dynamic bag leaf by
	 *    RebuildBagsFromTypeAsset (see UE::ComposableCameras::ExposedBag::AddDescIfSupported) - that single flag is what makes CanKeyProperty succeed; the outer
	 *    bag metadata is not required for the custom track-editor path. */
	UPROPERTY(EditAnywhere, Category = "Camera", meta = (FixedLayout = true))
	FInstancedPropertyBag Parameters;

	/** One entry per TypeAsset::ExposedVariables (NOT InternalVariables. Those
	 *  are not caller-overridable). Same FixedLayout and "no
	 *  InterpBagProperties" rationale as Parameters. */
	UPROPERTY(EditAnywhere, Category = "Camera", meta = (FixedLayout = true))
	FInstancedPropertyBag Variables;

	/**
	 * Regenerate the Parameters and Variables bag layouts to match the current
	 * TypeAsset, preserving any existing values whose name + type survive.
	 *
	 * If TypeAsset is null, both bags are reset empty.
	 *
	 * Called from UComposableCameraLevelSequenceComponent::PostEditChangeProperty
	 * when TypeAsset is swapped, and from ComponentActivated / OnRegister on
	 * first load so freshly-placed components pick up the latest interface.
	 */
	void RebuildBagsFromTypeAsset();

	/**
	 * Read every current bag value into OutBlock so it can be passed to
	 * UE::ComposableCameras::ConstructCameraFromTypeAsset. Uses
	 * FComposableCameraParameterBlock's existing typed setters so the block
	 * is indistinguishable from one produced by the K2Node activation path.
	 *
	 * Safe to call with a null TypeAsset. Writes nothing in that case.
	 */
	void BuildParameterBlock(FComposableCameraParameterBlock& OutBlock) const;
};
