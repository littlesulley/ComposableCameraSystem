// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/ComposableCameraNodePinTypes.h"
#include "StructUtils/PropertyBag.h"

/**
 * Helpers that bridge CCS's own pin-type taxonomy (EComposableCameraPinType) to
 * the generic FInstancedPropertyBag taxonomy (EPropertyBagPropertyType).
 *
 * Used by FComposableCameraTypeAssetReference to generate Parameters /
 * Variables bags from a TypeAsset's ExposedParameters / ExposedVariables, and
 * to read values back from those bags into an FComposableCameraParameterBlock
 * at camera activation time.
 *
 * Delegate pin type is intentionally not supported here — delegates cannot
 * round-trip through a property bag (they carry heap-owned bindings). Delegate
 * exposed parameters flow through the existing FComposableCameraParameterBlock
 * delegate path at activation time; the Level Sequence component bag covers
 * only POD-style parameters.
 */
namespace UE::ComposableCameras
{
	/**
	 * Map an EComposableCameraPinType (+ struct / enum metadata) to the matching
	 * EPropertyBagPropertyType and ValueTypeObject expected by
	 * FInstancedPropertyBag::AddProperty.
	 *
	 * Returns false for unsupported pin types (currently just Delegate); callers
	 * should skip those entries rather than adding them to the bag.
	 *
	 * @param InPinType             Source pin type.
	 * @param InStructType          Only read when InPinType == Struct; ignored otherwise.
	 * @param InEnumType            Only read when InPinType == Enum; ignored otherwise.
	 * @param OutBagPropertyType    Resulting bag property type.
	 * @param OutValueTypeObject    Struct / class / enum object carried alongside
	 *                              OutBagPropertyType. nullptr for POD types.
	 */
	COMPOSABLECAMERASYSTEM_API bool PinTypeToPropertyBagType(
		EComposableCameraPinType InPinType,
		const UScriptStruct* InStructType,
		const UEnum* InEnumType,
		EPropertyBagPropertyType& OutBagPropertyType,
		const UObject*& OutValueTypeObject);
}
