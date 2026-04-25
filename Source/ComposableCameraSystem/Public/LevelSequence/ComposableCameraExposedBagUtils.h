// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/ComposableCameraNodePinTypes.h"
#include "StructUtils/PropertyBag.h"

class UEnum;
class UScriptStruct;
struct FComposableCameraParameterBlock;
struct FInstancedPropertyBag;

namespace UE::ComposableCameras::ExposedBag
{
	/**
	 * Build a single FPropertyBagPropertyDesc and, if the pin type is
	 * representable in a property bag, append it to OutDescs. Returns false for
	 * types we intentionally skip (currently just Delegate).
	 *
	 * The descriptor's PropertyFlags include CPF_Edit | CPF_Interp. CPF_Interp
	 * is what Sequencer's FindPropertySetter checks to decide whether the leaf
	 * is keyable — without it CanKeyProperty returns false on every leaf and
	 * any track-editor parameter menu collapses. The flag propagates onto the
	 * dynamic FProperty that UPropertyBag::GetOrCreateFromDescs creates.
	 *
	 * Centralised here so both the LS Component (FComposableCameraTypeAssetReference)
	 * and the Patch Section (UMovieSceneComposableCameraPatchSection) use the
	 * exact same descriptor shape — divergence there would mean only one of the
	 * two surfaces is keyable in Sequencer.
	 */
	COMPOSABLECAMERASYSTEM_API bool AddDescIfSupported(
		FName Name,
		EComposableCameraPinType PinType,
		const UScriptStruct* StructType,
		const UEnum* EnumType,
		TArray<FPropertyBagPropertyDesc>& OutDescs);

	/**
	 * Copy a single bag value (keyed by Name) into OutBlock via the matching
	 * FComposableCameraParameterBlock typed setter. If the bag has no value
	 * for Name, the block is left untouched and the camera will fall back to
	 * the node's authored default during ApplyParameterBlock.
	 *
	 * Centralised for the same reason as AddDescIfSupported above — keeping
	 * one canonical shape for "bag → parameter block" guarantees any future
	 * pin-type addition flows uniformly through every consumer.
	 */
	COMPOSABLECAMERASYSTEM_API void CopyBagValueIntoBlock(
		const FInstancedPropertyBag& Bag,
		FName Name,
		EComposableCameraPinType PinType,
		const UScriptStruct* StructType,
		const UEnum* EnumType,
		FComposableCameraParameterBlock& OutBlock);
}
