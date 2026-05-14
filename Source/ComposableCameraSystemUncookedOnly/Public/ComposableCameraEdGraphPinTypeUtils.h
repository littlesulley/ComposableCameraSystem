// Copyright Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"

enum class EComposableCameraPinType: uint8;
class UScriptStruct;
class UEnum;

/**
 * Editor-side conversion helpers between the runtime camera-pin type enum and
 * Unreal's editor pin type descriptor.
 *
 * The plugin runs the same enum-to-FEdGraphPinType conversion in two places:
 *
 * - the camera type asset's own visual graph
 * (UComposableCameraNodeGraphNode in the editor module), and
 * - the K2 Activate Composable Camera node
 * (UK2Node_ActivateComposableCamera in this UncookedOnly module).
 *
 * Both call sites used to carry their own switch over EComposableCameraPinType,
 * which meant adding a new pin type required editing two switches. They now
 * delegate to MakeEdGraphPinTypeFromCameraPinType below - the single point of
 * truth for "what does this enum case look like as an FEdGraphPinType".
 *
 * This helper lives in ComposableCameraSystemUncookedOnly because:
 * - The runtime ComposableCameraSystem module doesn't depend on
 * BlueprintGraph and shouldn't have to (FEdGraphPinType is editor-only),
 * so it can't host this directly.
 * - The Editor module depends on the UncookedOnly module via Build.cs, which
 * lets the editor's graph node call into this header without anyone needing
 * to take a fresh dependency on a peer module.
 *
 * If you add a new EComposableCameraPinType case, update this helper. The
 * accompanying ensureMsgf in the implementation will fire on any unhandled
 * case so missing additions surface immediately.
 */
namespace ComposableCameraEdGraphPinTypeUtils
{
	/**
	 * Build an FEdGraphPinType for the given camera pin type.
	 *
	 * - Struct pins use StructType as the sub-category object (nullptr produces
	 * an unbound struct pin).
	 * - Enum pins use EnumType as the sub-category object on a PC_Byte pin
	 * (Blueprint represents enums as PC_Byte with an Enum sub-object - this
	 * matches how K2 nodes expose enum-typed parameters and how the engine
	 * serializes them). nullptr produces an unbound byte pin and an ensure
	 * fires elsewhere; pin authors should always supply the UEnum.
	 * - Delegate pins use SignatureFunction to set the PC_Delegate category
	 * with a PinSubCategoryMemberReference pointing at the signature UFunction.
	 * nullptr produces a typeless delegate pin that the K2 schema can't wire.
	 */
	COMPOSABLECAMERASYSTEMUNCOOKEDONLY_API FEdGraphPinType MakeEdGraphPinTypeFromCameraPinType(EComposableCameraPinType PinType,
		UScriptStruct* StructType = nullptr,
		UEnum* EnumType = nullptr,
		UFunction* SignatureFunction = nullptr);

	/**
	 * Pick the BP-callable typed setter on UComposableCameraBlueprintLibrary
	 * that matches the given pin type. Used by the three K2 nodes that emit
	 * SetParameterBlockValue calls in their ExpandNode (Activate / AddPatch /
	 * ActivateFromDataTable) -- dispatching per-pin-type to a typed setter
	 * sidesteps the UE 5.6 BP wildcard bug for CustomStructureParam pin
	 * defaults routed through MakeLiteralStruct intermediates (TechDoc.md
	 * Section 7.2). The wildcard `SetParameterBlockValue` is returned only as a
	 * fallback for pin types that need runtime FProperty inspection (Enum
	 * width normalization, arbitrary non-POD USTRUCT, Delegate).
	 */
	COMPOSABLECAMERASYSTEMUNCOOKEDONLY_API FName ResolveTypedSetterFunctionName(const FEdGraphPinType& PinType);
}
