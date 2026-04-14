// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"
#include "ComposableCameraGraphNodeBase.generated.h"

/**
 * Common base class for camera-pipeline graph nodes that participate in an
 * exec-pin chain on a camera type asset graph.
 *
 * Four concrete graph node types share the same execution-pin convention,
 * across two parallel chains — the per-frame camera chain and the one-shot
 * BeginPlay compute chain:
 *
 *  - UComposableCameraStartGraphNode          (only ExecOut — camera chain root)
 *  - UComposableCameraBeginPlayStartGraphNode (only ExecOut — compute chain root)
 *  - UComposableCameraNodeGraphNode           (ExecIn, ExecOut; classified as
 *                                              camera-chain or compute-chain at wire
 *                                              time by whether its NodeTemplate is a
 *                                              compute subclass)
 *  - UComposableCameraOutputGraphNode         (ExecIn — camera chain terminator)
 *
 * The compute chain has no terminator sentinel — it simply stops at whichever
 * compute graph node has an unwired ExecOut. See
 * UComposableCameraNodeGraphSchema::ClassifyChainForNode for the classification
 * rules and ComposableCameraBeginPlayStartGraphNode.h for the compute-chain
 * rationale.
 *
 * Pulling them under a common base buys three things:
 *
 *  1. The well-known pin names (`PN_ExecIn`, `PN_ExecOut`) are declared
 *     once instead of being duplicated on every subclass. The
 *     graph and schema were already assuming the names matched across the
 *     subclasses — this codifies that assumption.
 *
 *  2. The "create an exec pin with an empty friendly label" boilerplate
 *     becomes a one-line `CreateExecInPin()` / `CreateExecOutPin()` call from
 *     each subclass's AllocateDefaultPins, instead of every class building the
 *     FEdGraphPinType, calling CreatePin, then nullptr-checking and clearing
 *     the friendly name by hand.
 *
 *  3. The schema can `Cast<UComposableCameraGraphNodeBase>` once if it ever
 *     needs to ask a generic "is this one of our pipeline nodes?" question,
 *     instead of four separate IsA<> checks.
 *
 * The variable Get/Set graph node (UComposableCameraVariableGraphNode) is
 * deliberately *not* under this base. It also happens to have ExecIn/ExecOut
 * pins, but its semantic role is different: it represents a scratch-variable
 * read/write rather than a step in the camera-pose pipeline, and the editor
 * graph treats it as a peripheral node that wires into a pipeline node's input
 * pin. Sharing FName constants with it would be misleading. If a future
 * refactor decides to merge them, it should do so deliberately.
 *
 * The class is marked Abstract — it has no AllocateDefaultPins of its own and
 * is not meant to be instantiated directly.
 */
UCLASS(Abstract)
class COMPOSABLECAMERASYSTEMEDITOR_API UComposableCameraGraphNodeBase : public UEdGraphNode
{
	GENERATED_BODY()

public:
	// ─── Well-Known Pin Names ──────────────────────────────────────────
	//
	// Canonical pin name constants shared by every camera-pipeline graph
	// node. Subclasses use these directly via `PN_ExecIn` etc. (inherited
	// scope) and external consumers (the schema, the graph's
	// SyncToTypeAsset / RebuildFromTypeAsset walks) should reference them
	// through this base class — never through a concrete subclass — so the
	// invariant "all pipeline nodes use the same exec pin names" is
	// expressed in one place.

	static const FName PN_ExecIn;
	static const FName PN_ExecOut;

	// ─── Display Name Resolution ───────────────────────────────────────
	//
	// Camera node classes show up in two places that need a human-readable
	// label: the title bar of an instantiated graph node, and the
	// "Camera Nodes" palette in the schema's GetGraphContextActions. Both
	// sites used to roll their own munging — strip the "ComposableCamera"
	// prefix, strip the trailing "Node", insert spaces before capitals —
	// which meant a class authored with explicit `meta=(DisplayName=...)`
	// would have its label honored in some places and silently ignored in
	// others. Centralizing here gives a single answer:
	//
	//   1. If the class declared `UCLASS(meta=(DisplayName="..."))`, use it
	//      verbatim. This is the only way an author can override the
	//      generated name, so it must always win.
	//   2. Otherwise, fall back to the legacy munging so existing classes
	//      that never set DisplayName keep their current label.
	//
	// Both sites (UComposableCameraNodeGraphNode::GetNodeTitle and
	// UComposableCameraNodeGraphSchema::GetGraphContextActions) call this
	// helper, so adding `meta=(DisplayName="...")` to a node class
	// automatically updates *both* the graph title and the palette entry.

	/** Resolve the user-facing display name for a camera node UCLASS,
	 *  honoring `meta=(DisplayName=...)` first and falling back to a
	 *  munged class-name (strip "ComposableCamera" prefix, strip "Node"
	 *  suffix, insert spaces before camel-case boundaries). Returns an
	 *  empty FText if NodeClass is null. */
	static FText GetCameraNodeDisplayNameForClass(const UClass* NodeClass);

protected:
	// ─── Exec Pin Construction Helpers ─────────────────────────────────
	//
	// Subclass AllocateDefaultPins overrides call these instead of
	// hand-rolling the FEdGraphPinType + CreatePin + friendly-name dance.

	/** Allocate the standard exec input pin (`PN_ExecIn`) with an empty
	 *  friendly label. Returns the created pin, or nullptr on failure. */
	UEdGraphPin* CreateExecInPin();

	/** Allocate the standard exec output pin (`PN_ExecOut`) with an empty
	 *  friendly label. Returns the created pin, or nullptr on failure. */
	UEdGraphPin* CreateExecOutPin();
};
