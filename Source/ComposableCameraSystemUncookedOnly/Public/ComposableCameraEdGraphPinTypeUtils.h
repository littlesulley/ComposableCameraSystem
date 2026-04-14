// Copyright Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"

enum class EComposableCameraPinType : uint8;
class UScriptStruct;

/**
 * Editor-side conversion helpers between the runtime camera-pin type enum and
 * Unreal's editor pin type descriptor.
 *
 * The plugin runs the same enum-to-FEdGraphPinType conversion in two places:
 *
 *  - the camera type asset's own visual graph
 *    (UComposableCameraNodeGraphNode in the editor module), and
 *  - the K2 Activate Composable Camera node
 *    (UK2Node_ActivateComposableCamera in this UncookedOnly module).
 *
 * Both call sites used to carry their own switch over EComposableCameraPinType,
 * which meant adding a new pin type required editing two switches. They now
 * delegate to MakeEdGraphPinTypeFromCameraPinType below — the single point of
 * truth for "what does this enum case look like as an FEdGraphPinType".
 *
 * This helper lives in ComposableCameraSystemUncookedOnly because:
 *  - The runtime ComposableCameraSystem module doesn't depend on
 *    BlueprintGraph and shouldn't have to (FEdGraphPinType is editor-only),
 *    so it can't host this directly.
 *  - The Editor module depends on the UncookedOnly module via Build.cs, which
 *    lets the editor's graph node call into this header without anyone needing
 *    to take a fresh dependency on a peer module.
 *
 * If you add a new EComposableCameraPinType case, update this helper. The
 * accompanying ensureMsgf in the implementation will fire on any unhandled
 * case so missing additions surface immediately.
 */
namespace ComposableCameraEdGraphPinTypeUtils
{
	/**
	 * Build an FEdGraphPinType for the given camera pin type. For Struct pins,
	 * pass the script struct that PinType resolves to; nullptr is tolerated and
	 * produces a Struct pin with no sub-category object (UE will render it as
	 * an unbound struct pin).
	 */
	COMPOSABLECAMERASYSTEMUNCOOKEDONLY_API FEdGraphPinType MakeEdGraphPinTypeFromCameraPinType(
		EComposableCameraPinType PinType,
		UScriptStruct* StructType = nullptr);
}
