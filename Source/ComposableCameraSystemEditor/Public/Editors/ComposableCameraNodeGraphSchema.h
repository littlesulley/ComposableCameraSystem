// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphSchema.h"
#include "ComposableCameraNodeGraphSchema.generated.h"

class UComposableCameraCameraNodeBase;
class UComposableCameraNodeGraphNode;
class UComposableCameraTypeAsset;
class UEdGraphNode;
class UEdGraphPin;
class UGraphNodeContextMenuContext;
class UToolMenu;

/**
 * Which exec-chain a graph node belongs to.
 *
 * The editor maintains two parallel exec chains on a single graph: the
 * per-frame **camera chain** (rooted at the main Start sentinel, terminating
 * at the Output sentinel) and the one-shot **compute chain** (rooted at the
 * BeginPlay Start sentinel, no terminating sentinel). Wires from a pin
 * rooted on the main Start sentinel can only reach nodes classified as
 * Camera; wires from a pin rooted on the BeginPlay Start sentinel can only
 * reach nodes classified as Compute. The schema enforces this at wire time
 * in CanCreateConnection, which is the only call site of
 * ClassifyChainForNode.
 *
 * Data wires obey the same rule: camera nodes and compute nodes never
 * connect to each other directly. Cross-chain communication (compute -> 
 * variable->camera) is not yet modeled in the graph; for v1 it must
 * happen through SetInternalVariable calls inside the compute node's
 * Execute() C++ body.
 */
enum class EComposableCameraGraphChain: uint8
{
	/** Regular camera pipeline: Start sentinel, camera nodes, variable Get/Set
	 * nodes, Output sentinel. */
	Camera,

	/** BeginPlay compute chain: BeginPlay Start sentinel and compute nodes. */
	Compute,

	/** An unwired variable Set node whose chain cannot yet be determined.
	 * CanCreateConnection treats this as compatible with either chain so the
	 * user can drag the first exec wire from either sentinel. Once wired,
	 * ClassifyChainForNode will follow the exec chain backward and return
	 * Camera or Compute definitively. */
	Unclassified,
};

/**
 * Schema for the Composable Camera Type Asset node graph.
 * Enforces type-safe data pins, a single source per input, exec-pin driven
 * execution order, and exposure conflict prevention. Data-pin topology is
 * unconstrained by node layout - execution order is expressed purely through
 * the exec-pin chain (Start -> ... ->Output).
 */
UCLASS()
class COMPOSABLECAMERASYSTEMEDITOR_API UComposableCameraNodeGraphSchema: public UEdGraphSchema
{
	GENERATED_BODY()

public:
	// Connection Validation 

	virtual const FPinConnectionResponse CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const override;

	virtual bool TryCreateConnection(UEdGraphPin* A, UEdGraphPin* B) const override;

	// Connection Removal 
	// Wire removal has the same refresh contract as wire creation: update
	// the underlying UEdGraphPin state, then sync to the TypeAsset so
	// PinConnections is truthful, then NotifyGraphChanged so Slate
	// rebuilds ErrorText / UpdateErrorInfo widgets (UE's default
	// implementations skip both, leaving stale validation badges and a
	// TypeAsset with phantom connections).

	virtual void BreakSinglePinLink(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin) const override;

	virtual void BreakPinLinks(UEdGraphPin& TargetPin, bool bSendsNodeNotification) const override;

	virtual void BreakNodeLinks(UEdGraphNode& TargetNode) const override;

	// Context Menu 

	virtual void GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const override;

	virtual void GetContextMenuActions(class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const override;

	// Pin Colors 

	virtual FLinearColor GetPinTypeColor(const FEdGraphPinType& PinType) const override;

	// Connection Drawing 

	virtual FConnectionDrawingPolicy* CreateConnectionDrawingPolicy(int32 InBackLayerID,
		int32 InFrontLayerID,
		float InZoomFactor,
		const FSlateRect& InClippingRect,
		FSlateWindowElementList& InDrawElements,
		UEdGraph* InGraphObj) const override;

	// Default Nodes 

	virtual void CreateDefaultNodesForGraph(UEdGraph& Graph) const override;

	// Drag & Drop 

	virtual bool ShouldAlwaysPurgeOnModification() const override { return true; }

	// Graph Actions 

	/** Add a new node of the given class to the graph at the specified position.
	 *
	 * Routes the new template into either TypeAsset->NodeTemplates (for
	 * regular camera nodes) or TypeAsset->ComputeNodeTemplates (for compute
	 * nodes) based on NodeClass->IsChildOf(UComposableCameraComputeNodeBase).
	 * The graph node's NodeIndex is set to its position within whichever
	 * array it was added to, and Sync/Rebuild phases thread the two index
	 * spaces independently - they do not overlap. */
	static UEdGraphNode* AddNodeToGraph(UEdGraph* Graph, TSubclassOf<UComposableCameraCameraNodeBase> NodeClass, const FVector2D& Location);

	/** Classify a graph node as belonging to the camera chain or the compute
	 * chain. See EComposableCameraGraphChain for the rule set. Used by
	 * CanCreateConnection to refuse cross-chain exec and data wires.
	 *
	 * Sentinels and camera/compute graph nodes identify their chain via class
	 * identity. Variable Set nodes are dynamically classified by following
	 * their ExecIn wire backward to the nearest definitively-classified node
	 * (sentinel or camera/compute graph node). Variable Get nodes (no exec
	 * pins, pure data readers) default to Camera but CanCreateConnection
	 * relaxes the cross-chain check for their data wires so they can feed
	 * into either chain's node pins. Unwired Set nodes return Unclassified
	 * so CanCreateConnection allows the first exec wire from either chain. */
	static EComposableCameraGraphChain ClassifyChainForNode(const UEdGraphNode* Node);

private:
	/** Check if two pin types are compatible for connection. */
	static bool ArePinTypesCompatible(const FEdGraphPinType& SourceType, const FEdGraphPinType& TargetType);

	// Context Menu Builders 
	//
	// GetContextMenuActions is a thin dispatcher that classifies the click as
	// pin / node / (nothing) and delegates to one of these helpers. Keeping
	// the three branches as named functions lets each one be read in isolation
	// and makes the dispatcher's classification logic the only thing that has
	// to know about every case.

	/** Add the "Expose as Camera Parameter" / "Unexpose Parameter" section
	 * for a right-click on an exposable data input pin on a camera graph
	 * node. Only invoked when the click target satisfies the filter
	 * (camera-node input pin, non-exec PinCategory). */
	static void BuildPinContextMenuActions(UToolMenu* Menu,
		UComposableCameraNodeGraphNode* CameraGraphNode,
		const UEdGraphPin* ClickedPin);

	/** Add the "Delete" node-body entry for a right-click on a user-deletable
	 * node (no pin targeted). Delegates to a `Modify->DestroyNode -> Sync`
	 * lambda that works even when the tool menu context lacks a command list. */
	static void BuildNodeContextMenuActions(UToolMenu* Menu,
		const UEdGraphNode* ClickedNode);

	// Graph Palette Builders 
	//
	// GetGraphContextActions (the palette shown when the user right-clicks
	// empty graph canvas, or opens the "New Node" menu) is a thin dispatcher
	// that calls these two helpers in order. Splitting them lets each one be
	// read in isolation: camera-node palette vs variable palette have nothing
	// in common beyond their output stream (FGraphContextMenuBuilder).

	/** Enumerate every non-abstract UComposableCameraCameraNodeBase subclass
	 * *excluding* compute node subclasses, sort by resolved display name so
	 * the palette order matches the node title the user will see after drop,
	 * and emit one FComposableCameraNodeGraphSchemaAction_NewNode per class
	 * under the "Camera Nodes" category. Compute nodes are emitted by
	 * BuildComputeNodePaletteActions under a separate category. */
	static void BuildCameraNodePaletteActions(FGraphContextMenuBuilder& ContextMenuBuilder);

	/** Enumerate every non-abstract UComposableCameraComputeNodeBase subclass,
	 * sort by resolved display name, and emit one
	 * FComposableCameraNodeGraphSchemaAction_NewNode per class under the
	 * "Compute Nodes" category. Compute nodes live in a separate sub-menu
	 * from regular camera nodes so the author can see at a glance which
	 * classes are per-frame (camera nodes) vs one-shot BeginPlay (compute
	 * nodes). The AddNodeToGraph path routes compute templates into
	 * TypeAsset->ComputeNodeTemplates rather than NodeTemplates. */
	static void BuildComputeNodePaletteActions(FGraphContextMenuBuilder& ContextMenuBuilder);

	/** Emit one Get and one Set palette action per variable declared on the
	 * owning type asset, routed into "Variables|Get|Internal",
	 * "Variables|Set|Internal", "Variables|Get|Exposed", and
	 * "Variables|Set|Exposed" sub-categories so the author can see at a
	 * glance which variables accept caller overrides. The caller is
	 * responsible for resolving the type asset and bailing out of the
	 * variable palette if it isn't available; passing a null TypeAsset is a
	 * caller bug that this helper will not defend against. */
	static void BuildVariablePaletteActions(FGraphContextMenuBuilder& ContextMenuBuilder,
		const UComposableCameraTypeAsset* TypeAsset);
};
