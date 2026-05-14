// Copyright Sulley. All rights reserved.

#include "Editors/ComposableCameraNodeGraphSchema.h"
#include "ComposableCameraEditorStyle.h"
#include "Editors/ComposableCameraConnectionDrawingPolicy.h"
#include "Editors/ComposableCameraNodeGraph.h"
#include "Editors/ComposableCameraNodeGraphNode.h"
#include "Editors/ComposableCameraStartGraphNode.h"
#include "Editors/ComposableCameraBeginPlayStartGraphNode.h"
#include "Editors/ComposableCameraOutputGraphNode.h"
#include "Editors/ComposableCameraVariableGraphNode.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "Nodes/ComposableCameraComputeNodeBase.h"
#include "Nodes/ComposableCameraNodePinTypes.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "GraphEditorActions.h"
#include "ScopedTransaction.h"
#include "ToolMenu.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "UObject/UObjectIterator.h"

#define LOCTEXT_NAMESPACE "ComposableCameraNodeGraphSchema"

// Connection Validation 

const FPinConnectionResponse UComposableCameraNodeGraphSchema::CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const
{
	if (!A || !B)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("NullPin", "Invalid pin."));
	}

	// Disallow self-connections.
	if (A->GetOwningNode() == B->GetOwningNode())
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("SameNode", "Cannot connect a node to itself."));
	}

	// Ensure one is output and the other is input.
	const UEdGraphPin* OutputPin = nullptr;
	const UEdGraphPin* InputPin = nullptr;

	if (A->Direction == EGPD_Output && B->Direction == EGPD_Input)
	{
		OutputPin = A;
		InputPin = B;
	}
	else if (A->Direction == EGPD_Input && B->Direction == EGPD_Output)
	{
		OutputPin = B;
		InputPin = A;
	}
	else
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
			LOCTEXT("SameDirection", "Cannot connect two pins of the same direction."));
	}

	// Execution pin handling 
	const bool bIsExecConnection =
		OutputPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
	const bool bTargetIsExec =
		InputPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;

	// Exec pins can only connect to other exec pins.
	if (bIsExecConnection != bTargetIsExec)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
			LOCTEXT("ExecMismatch", "Cannot connect execution pins to data pins."));
	}

	// Cross-chain enforcement. Exec wires rooted on the main Start sentinel
	// can only reach nodes classified as the camera chain; exec wires rooted
	// on the BeginPlay Start sentinel can only reach nodes classified as the
	// compute chain. See ClassifyChainForNode for the classification rules.
	//
	// Data wires between camera nodes and compute nodes are still disallowed - 
	// a camera node and a compute node never connect directly. Cross-chain
	// communication goes through internal variables.
	//
	// Exception: variable Get nodes are chain-agnostic pure data readers.
	// A Get node's Value output may wire into any chain's node input pin,
	// because it reads from the shared RuntimeDataBlock and doesn't
	// participate in exec flow. The relaxation only applies to data wires - 
	// Get nodes have no exec pins so this branch is unreachable for exec
	// connections.
	const EComposableCameraGraphChain OutputChain = ClassifyChainForNode(OutputPin->GetOwningNode());
	const EComposableCameraGraphChain InputChain = ClassifyChainForNode(InputPin->GetOwningNode());
	if (OutputChain != InputChain)
	{
		// Unclassified nodes (unwired Set variable nodes) are compatible with
		// either chain - the first exec wire determines their chain identity.
		const bool bEitherIsUnclassified =
			OutputChain == EComposableCameraGraphChain::Unclassified ||
			InputChain == EComposableCameraGraphChain::Unclassified;

		// Allow cross-chain data wires for Get variable nodes. A Get variable
		// node is identified as a non-setter UComposableCameraVariableGraphNode.
		const bool bOutputIsGetVariable = [&]()
		{
			if (const auto* VarNode = Cast<UComposableCameraVariableGraphNode>(OutputPin->GetOwningNode()))
			{
				return !VarNode->bIsSetter;
			}
			return false;
		}();

		if (!bEitherIsUnclassified && !bOutputIsGetVariable)
		{
			return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
				LOCTEXT("CrossChainDisallowed",
					"Cannot wire across the camera chain and BeginPlay compute chain."));
		}
	}

	// For exec connections: allow freely (no forward-only constraint, no type check).
	if (bIsExecConnection)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_BREAK_OTHERS_AB,
			LOCTEXT("ExecConnectionAllowed", ""));
	}

	// Data pin handling 
	//
	// Data flow is no longer constrained by node order. Execution order is
	// expressed purely through the exec pin chain (Start -> ... ->Output).
	// Data pins may connect in any direction as long as types are compatible
	// - the same model used by MetaSound / Niagara / Blueprint function graphs.

	// Disallow variable-node variable-node wires. Variable nodes must wire
	// into camera node pins to have any effect on the data flow.
	const bool bSourceIsVariableNode =
		OutputPin->GetOwningNode()->IsA<UComposableCameraVariableGraphNode>();
	const bool bTargetIsVariableNode =
		InputPin->GetOwningNode()->IsA<UComposableCameraVariableGraphNode>();
	if (bSourceIsVariableNode && bTargetIsVariableNode)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
			LOCTEXT("VarToVar", "Variable nodes must be connected to camera node pins, not to each other."));
	}

	const bool bTargetIsOutputNode = InputPin->GetOwningNode()->IsA<UComposableCameraOutputGraphNode>();

	// Enforce type compatibility.
	if (!ArePinTypesCompatible(OutputPin->PinType, InputPin->PinType))
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
			LOCTEXT("TypeMismatch", "Pin types are not compatible."));
	}

	// Check that the target input pin is not exposed as a parameter.
	// (Only applicable to regular camera node graph nodes, not the Output node.)
	if (!bTargetIsOutputNode)
	{
		if (const UComposableCameraNodeGraphNode* TargetGraphNode =
			Cast<UComposableCameraNodeGraphNode>(InputPin->GetOwningNode()))
		{
			if (TargetGraphNode->IsInputPinExposed(InputPin->PinName))
			{
				return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
					LOCTEXT("PinExposed", "This input pin is exposed as a camera parameter. Unexpose it first."));
			}
		}
	}

	// Single source per input: if the input already has a connection, break it.
	return FPinConnectionResponse(CONNECT_RESPONSE_BREAK_OTHERS_B,
		LOCTEXT("ConnectionAllowed", ""));
}

bool UComposableCameraNodeGraphSchema::TryCreateConnection(UEdGraphPin* A, UEdGraphPin* B) const
{
	const FPinConnectionResponse Response = CanCreateConnection(A, B);
	if (Response.Response == CONNECT_RESPONSE_DISALLOW)
	{
		return false;
	}

	// Determine input and output.
	UEdGraphPin* OutputPin = (A->Direction == EGPD_Output) ? A: B;
	UEdGraphPin* InputPin = (A->Direction == EGPD_Input) ? A: B;

	// Break existing connections based on the response type.
	if (Response.Response == CONNECT_RESPONSE_BREAK_OTHERS_AB)
	{
		// Exec pins: single connection per exec in AND per exec out.
		OutputPin->BreakAllPinLinks();
		InputPin->BreakAllPinLinks();
	}
	else if (Response.Response == CONNECT_RESPONSE_BREAK_OTHERS_B)
	{
		// Data pins: single source per input.
		InputPin->BreakAllPinLinks();
	}

	OutputPin->MakeLinkTo(InputPin);

	// Sync the graph changes back to the type asset, then notify Slate so
	// the freshly-computed validation state (e.g. a now-satisfied Required
	// pin) is reflected in the node widgets.
	//
	// Why both calls matter: SyncToTypeAsset rebuilds PinConnections and
	// calls ApplyBuildMessagesToGraphNodes, which writes fresh
	// bHasCompilerMessage / ErrorMsg / ErrorType values onto each
	// UEdGraphNode. BUT the visible "WARNING!" banner is a Slate child
	// widget constructed in SGraphNode::UpdateErrorInfo, invoked only
	// from UpdateGraphNode, which is itself triggered by NotifyGraphChanged.
	// Skip the notify and Slate keeps the stale pre-connect banner even
	// though the underlying node state is already clean.
	if (UComposableCameraNodeGraph* NodeGraph = Cast<UComposableCameraNodeGraph>(OutputPin->GetOwningNode()->GetGraph()))
	{
		NodeGraph->SyncToTypeAsset();
		NodeGraph->NotifyGraphChanged();
	}

	return true;
}

void UComposableCameraNodeGraphSchema::BreakSinglePinLink(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin) const
{
	// Do the actual unlink via the base implementation so we don't
	// duplicate UE's "remove from both pins' LinkedTo arrays" logic.
	Super::BreakSinglePinLink(SourcePin, TargetPin);

	// Then sync + notify so both TypeAsset::PinConnections and Slate's
	// error / validation widgets catch up. Either endpoint's graph is fine
	// to resolve from - we prefer SourcePin for symmetry with
	// TryCreateConnection's OutputPin path.
	UEdGraphPin* AnyPin = SourcePin ? SourcePin: TargetPin;
	if (AnyPin && AnyPin->GetOwningNode())
	{
		if (UComposableCameraNodeGraph* NodeGraph = Cast<UComposableCameraNodeGraph>(AnyPin->GetOwningNode()->GetGraph()))
		{
			NodeGraph->SyncToTypeAsset();
			NodeGraph->NotifyGraphChanged();
		}
	}
}

void UComposableCameraNodeGraphSchema::BreakPinLinks(UEdGraphPin& TargetPin, bool bSendsNodeNotification) const
{
	Super::BreakPinLinks(TargetPin, bSendsNodeNotification);

	if (TargetPin.GetOwningNode())
	{
		if (UComposableCameraNodeGraph* NodeGraph = Cast<UComposableCameraNodeGraph>(TargetPin.GetOwningNode()->GetGraph()))
		{
			NodeGraph->SyncToTypeAsset();
			NodeGraph->NotifyGraphChanged();
		}
	}
}

void UComposableCameraNodeGraphSchema::BreakNodeLinks(UEdGraphNode& TargetNode) const
{
	Super::BreakNodeLinks(TargetNode);

	if (UComposableCameraNodeGraph* NodeGraph = Cast<UComposableCameraNodeGraph>(TargetNode.GetGraph()))
	{
		NodeGraph->SyncToTypeAsset();
		NodeGraph->NotifyGraphChanged();
	}
}

// Context Menu 

/**
 * Graph action for spawning a new camera node.
 */
struct FComposableCameraNodeGraphSchemaAction_NewNode: public FEdGraphSchemaAction
{
	TSubclassOf<UComposableCameraCameraNodeBase> NodeClass;

	FComposableCameraNodeGraphSchemaAction_NewNode() {}

	FComposableCameraNodeGraphSchemaAction_NewNode(FText InNodeCategory, FText InMenuDesc, FText InToolTip, int32 InGrouping,
		TSubclassOf<UComposableCameraCameraNodeBase> InNodeClass)
		: FEdGraphSchemaAction(MoveTemp(InNodeCategory), MoveTemp(InMenuDesc), MoveTemp(InToolTip), InGrouping)
		, NodeClass(InNodeClass)
	{
	}

	virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin,
		const FVector2D Location, bool bSelectNewNode = true) override
	{
		if (!NodeClass || !ParentGraph)
		{
			return nullptr;
		}

		return UComposableCameraNodeGraphSchema::AddNodeToGraph(ParentGraph, NodeClass, Location);
	}
};

/**
 * Graph action for spawning a Get/Set variable graph node.
 */
struct FComposableCameraNodeGraphSchemaAction_NewVariableNode: public FEdGraphSchemaAction
{
	/** Stable identity of the target internal variable. Preferred over VariableName
	 * because it survives renames. */
	FGuid VariableGuid;
	FName VariableName;
	bool bIsSetter = false;

	FComposableCameraNodeGraphSchemaAction_NewVariableNode() {}

	FComposableCameraNodeGraphSchemaAction_NewVariableNode(FText InNodeCategory, FText InMenuDesc, FText InToolTip, int32 InGrouping,
		FGuid InVariableGuid, FName InVariableName, bool bInIsSetter)
		: FEdGraphSchemaAction(MoveTemp(InNodeCategory), MoveTemp(InMenuDesc), MoveTemp(InToolTip), InGrouping)
		, VariableGuid(InVariableGuid)
		, VariableName(InVariableName)
		, bIsSetter(bInIsSetter)
	{
	}

	virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin,
		const FVector2D Location, bool bSelectNewNode = true) override
	{
		if (!ParentGraph || VariableName.IsNone())
		{
			return nullptr;
		}

		// Open our own transaction so Ctrl+Z cleanly removes the new variable
		// node AND any wire TryCreateConnection lays down below, as a single
		// atomic unit. UEdGraphSchema does not auto-wrap PerformAction in a
		// transaction; without this wrapping, the add-variable-node action
		// produces zero undo records. See the matching comment in
		// UComposableCameraNodeGraphSchema::AddNodeToGraph.
		const FScopedTransaction Transaction(bIsSetter
				? LOCTEXT("AddSetVariableNode", "Add Set Variable Node")
				: LOCTEXT("AddGetVariableNode", "Add Get Variable Node"));
		ParentGraph->Modify();

		UComposableCameraVariableGraphNode* NewVarNode = NewObject<UComposableCameraVariableGraphNode>(ParentGraph, NAME_None, RF_Transactional);
		NewVarNode->VariableGuid = VariableGuid;
		NewVarNode->VariableName = VariableName;
		NewVarNode->bIsSetter = bIsSetter;
		NewVarNode->CreateNewGuid();
		NewVarNode->NodePosX = Location.X;
		NewVarNode->NodePosY = Location.Y;
		NewVarNode->AllocateDefaultPins();

		ParentGraph->AddNode(NewVarNode, /*bFromUI=*/true, bSelectNewNode);

		// Auto-wire to the pin the user dragged from, if compatible. When
		// dragging off an exec pin onto a new Set node, prefer wiring into the
		// Set's exec pin rather than its Value pin (which would be a type
		// mismatch and silently fail). The schema's TryCreateConnection will
		// validate and break any incompatible link itself.
		if (FromPin)
		{
			const bool bFromExec = FromPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;

			UEdGraphPin* PreferredTarget = nullptr;
			if (bFromExec && bIsSetter)
			{
				PreferredTarget = (FromPin->Direction == EGPD_Output)
					? NewVarNode->FindPin(UComposableCameraVariableGraphNode::PN_ExecIn, EGPD_Input)
					: NewVarNode->FindPin(UComposableCameraVariableGraphNode::PN_ExecOut, EGPD_Output);
			}
			else
			{
				PreferredTarget = NewVarNode->FindPin(UComposableCameraVariableGraphNode::PN_Value);
			}

			if (PreferredTarget)
			{
				ParentGraph->GetSchema()->TryCreateConnection(FromPin, PreferredTarget);
			}
		}

		if (UComposableCameraNodeGraph* NodeGraph = Cast<UComposableCameraNodeGraph>(ParentGraph))
		{
			NodeGraph->SyncToTypeAsset();
			NodeGraph->NotifyGraphChanged();
		}

		return NewVarNode;
	}
};

void UComposableCameraNodeGraphSchema::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{
	// Thin dispatcher: emit camera-node palette actions, then compute-node
	// palette actions, then variable palette actions if we can resolve the
	// owning type asset. Each sub-palette is implemented as an independent
	// static helper - adding a new palette category means adding a new
	// Build*PaletteActions call here, nothing else.

	BuildCameraNodePaletteActions(ContextMenuBuilder);
	BuildComputeNodePaletteActions(ContextMenuBuilder);

	// Variable palette requires the owning type asset for its InternalVariables
	// / ExposedVariables arrays. Prefer the graph's back-reference, fall back
	// to the graph outer for assets or paths that don't set OwningTypeAsset.
	UComposableCameraTypeAsset* TypeAsset = nullptr;
	if (const UComposableCameraNodeGraph* NodeGraph =
			Cast<UComposableCameraNodeGraph>(ContextMenuBuilder.CurrentGraph))
	{
		TypeAsset = NodeGraph->OwningTypeAsset;
	}
	if (!TypeAsset && ContextMenuBuilder.CurrentGraph)
	{
		TypeAsset = Cast<UComposableCameraTypeAsset>(ContextMenuBuilder.CurrentGraph->GetOuter());
	}

	if (TypeAsset)
	{
		BuildVariablePaletteActions(ContextMenuBuilder, TypeAsset);
	}
}

// Graph Palette Builders 

void UComposableCameraNodeGraphSchema::BuildCameraNodePaletteActions(FGraphContextMenuBuilder& ContextMenuBuilder)
{
	// Gather all non-abstract subclasses of UComposableCameraCameraNodeBase,
	// *excluding* compute node subclasses. The compute chain palette lives
	// in its own category (BuildComputeNodePaletteActions below) so authors
	// can't accidentally drop a compute node into the per-frame camera
	// palette and then be confused about why it doesn't tick.
	TArray<UClass*> NodeClasses;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (Class->IsChildOf(UComposableCameraCameraNodeBase::StaticClass())
			&& !Class->HasAnyClassFlags(CLASS_Abstract | CLASS_NewerVersionExists)
			&& Class != UComposableCameraCameraNodeBase::StaticClass()
			&& !Class->IsChildOf(UComposableCameraComputeNodeBase::StaticClass())
			&& !Class->GetName().StartsWith(TEXT("SKEL_"))
			&& !Class->GetName().StartsWith(TEXT("REINST_")))
		{
			NodeClasses.Add(Class);
		}
	}

	// Sort alphabetically by the *resolved* display name (the same name the
	// palette and the eventual graph node title will show), so a class that
	// declares `meta=(DisplayName="Aim Lock")` lands in 'A' rather than wherever
	// its raw `UComposableCameraXyzNode` identifier would have sorted to. For
	// classes with no DisplayName metadata, the helper just returns the legacy
	// munged name, which is monotonic in the class name - so this is a no-op
	// for every node class that exists today.
	NodeClasses.Sort([](const UClass& A, const UClass& B)
	{
		const FText DisplayA = UComposableCameraGraphNodeBase::GetCameraNodeDisplayNameForClass(&A);
		const FText DisplayB = UComposableCameraGraphNodeBase::GetCameraNodeDisplayNameForClass(&B);
		return DisplayA.CompareTo(DisplayB) < 0;
	});

	for (UClass* Class: NodeClasses)
	{
		// Display name resolution lives on the shared graph-node base so the
		// palette entry here matches the title that
		// UComposableCameraNodeGraphNode::GetNodeTitle will produce after the
		// node is dropped into the graph. Honors UCLASS meta=(DisplayName=...)
		// when present, falls back to legacy class-name munging otherwise.
		const FText MenuDesc = UComposableCameraGraphNodeBase::GetCameraNodeDisplayNameForClass(Class);
		FText Tooltip = FText::Format(LOCTEXT("AddNodeTooltip", "Add a {0} node"), MenuDesc);

		// Per-class palette category: read off the CDO's PaletteCategory
		// FName field. C++ nodes set it in their constructor; Blueprint
		// subclasses set it via Class Defaults. Fallback "Misc" when unset
		// or null CDO. The "|" separator nests the per-class subcategory
		// under the "Camera Nodes" root automatically (UE's
		// FEdGraphSchemaAction::Category honors "|" as a path delimiter).
		const UComposableCameraCameraNodeBase* NodeCDO =
			Class->GetDefaultObject<UComposableCameraCameraNodeBase>();
		const FName SubCategoryName = (NodeCDO && !NodeCDO->PaletteCategory.IsNone())
			? NodeCDO->PaletteCategory: FName(TEXT("Misc"));
		const FText Category = FText::Format(LOCTEXT("CameraNodesCategoryFmt", "Camera Nodes|{0}"),
			FText::FromName(SubCategoryName));

		TSharedPtr<FComposableCameraNodeGraphSchemaAction_NewNode> Action =
			MakeShared<FComposableCameraNodeGraphSchemaAction_NewNode>(Category, MenuDesc, Tooltip, 0,
				TSubclassOf<UComposableCameraCameraNodeBase>(Class));

		ContextMenuBuilder.AddAction(Action);
	}
}

void UComposableCameraNodeGraphSchema::BuildComputeNodePaletteActions(FGraphContextMenuBuilder& ContextMenuBuilder)
{
	// Gather all non-abstract subclasses of UComposableCameraComputeNodeBase.
	// Compute nodes inherit from UComposableCameraCameraNodeBase (they reuse
	// the pin plumbing), so BuildCameraNodePaletteActions above explicitly
	// filters them OUT to prevent double-listing. The palette category
	// "Compute Nodes" is the sole surface where a user can spawn a compute
	// node onto the graph.
	TArray<UClass*> ComputeClasses;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (Class->IsChildOf(UComposableCameraComputeNodeBase::StaticClass())
			&& !Class->HasAnyClassFlags(CLASS_Abstract | CLASS_NewerVersionExists)
			&& Class != UComposableCameraComputeNodeBase::StaticClass()
			&& !Class->GetName().StartsWith(TEXT("SKEL_"))
			&& !Class->GetName().StartsWith(TEXT("REINST_")))
		{
			ComputeClasses.Add(Class);
		}
	}

	// Same display-name resolution as the camera palette - honors
	// meta=(DisplayName="...") first, falls back to legacy munging.
	ComputeClasses.Sort([](const UClass& A, const UClass& B)
	{
		const FText DisplayA = UComposableCameraGraphNodeBase::GetCameraNodeDisplayNameForClass(&A);
		const FText DisplayB = UComposableCameraGraphNodeBase::GetCameraNodeDisplayNameForClass(&B);
		return DisplayA.CompareTo(DisplayB) < 0;
	});

	for (UClass* Class: ComputeClasses)
	{
		const FText MenuDesc = UComposableCameraGraphNodeBase::GetCameraNodeDisplayNameForClass(Class);
		const FText Tooltip = FText::Format(LOCTEXT("AddComputeNodeTooltip",
				"Add a {0} compute node (runs once on BeginPlay before the first tick)."),
			MenuDesc);

		// Same CDO-based per-class subcategory rule as the camera palette
		// above, just nested under "Compute Nodes" instead of "Camera Nodes".
		// Compute nodes inherit PaletteCategory from
		// UComposableCameraCameraNodeBase, so the read path is identical.
		const UComposableCameraCameraNodeBase* NodeCDO =
			Class->GetDefaultObject<UComposableCameraCameraNodeBase>();
		const FName SubCategoryName = (NodeCDO && !NodeCDO->PaletteCategory.IsNone())
			? NodeCDO->PaletteCategory: FName(TEXT("Misc"));
		const FText Category = FText::Format(LOCTEXT("ComputeNodesCategoryFmt", "Compute Nodes|{0}"),
			FText::FromName(SubCategoryName));

		TSharedPtr<FComposableCameraNodeGraphSchemaAction_NewNode> Action =
			MakeShared<FComposableCameraNodeGraphSchemaAction_NewNode>(Category, MenuDesc, Tooltip, 0,
				TSubclassOf<UComposableCameraCameraNodeBase>(Class));

		ContextMenuBuilder.AddAction(Action);
	}
}

void UComposableCameraNodeGraphSchema::BuildVariablePaletteActions(FGraphContextMenuBuilder& ContextMenuBuilder,
	const UComposableCameraTypeAsset* TypeAsset)
{
	// Add one Get and one Set action per variable declared on the type asset.
	// Variables live in two author-facing arrays: InternalVariables (fully
	// internal - the caller can't touch them at activation time) and
	// ExposedVariables (caller may override the initial value through the
	// ParameterBlock at activation, but after that they behave identically to
	// internal variables). From the graph's point of view both kinds are
	// first-class read/write slots, so the authoring affordance is the same.
	//
	// They're routed into separate palette sub-categories ("Variables|Get|
	// Internal" / "|Exposed") so the author can see at a glance which ones
	// accept caller overrides. Both kinds produce the same purple variable
	// graph node type - the runtime distinction lives purely on the type
	// asset, not on the node.
	//
	// Caller contract: TypeAsset must be non-null. The dispatcher above does
	// the null check once; we don't repeat it here.
	check(TypeAsset);

	auto EmitVariableActions = [&ContextMenuBuilder](const TArray<FComposableCameraInternalVariable>& Variables,
		const FText& GetCategory,
		const FText& SetCategory)
	{
		for (const FComposableCameraInternalVariable& Variable: Variables)
		{
			if (Variable.VariableName.IsNone())
			{
				continue;
			}

			// VariableName is both the runtime key AND the display label - 
			// there is no separate DisplayName field on
			// FComposableCameraInternalVariable.
			const FText DisplayName = FText::FromName(Variable.VariableName);

			// Get action.
			{
				const FText MenuDesc = FText::Format(LOCTEXT("GetVarActionFmt", "Get {0}"), DisplayName);
				const FText Tooltip = FText::Format(LOCTEXT("GetVarActionTooltipFmt",
						"Add a Get node that reads the current value of '{0}'."),
					DisplayName);

				TSharedPtr<FComposableCameraNodeGraphSchemaAction_NewVariableNode> Action =
					MakeShared<FComposableCameraNodeGraphSchemaAction_NewVariableNode>(GetCategory, MenuDesc, Tooltip, 0,
						Variable.VariableGuid, Variable.VariableName, /*bInIsSetter=*/false);

				ContextMenuBuilder.AddAction(Action);
			}

			// Set action.
			{
				const FText MenuDesc = FText::Format(LOCTEXT("SetVarActionFmt", "Set {0}"), DisplayName);
				const FText Tooltip = FText::Format(LOCTEXT("SetVarActionTooltipFmt",
						"Add a Set node that writes the connected value to '{0}'."),
					DisplayName);

				TSharedPtr<FComposableCameraNodeGraphSchemaAction_NewVariableNode> Action =
					MakeShared<FComposableCameraNodeGraphSchemaAction_NewVariableNode>(SetCategory, MenuDesc, Tooltip, 0,
						Variable.VariableGuid, Variable.VariableName, /*bInIsSetter=*/true);

				ContextMenuBuilder.AddAction(Action);
			}
		}
	};

	EmitVariableActions(TypeAsset->InternalVariables,
		LOCTEXT("InternalVariableGetCategory", "Variables|Get|Internal"),
		LOCTEXT("InternalVariableSetCategory", "Variables|Set|Internal"));

	EmitVariableActions(TypeAsset->ExposedVariables,
		LOCTEXT("ExposedVariableGetCategory", "Variables|Get|Exposed"),
		LOCTEXT("ExposedVariableSetCategory", "Variables|Set|Exposed"));
}

// Per-Node / Per-Pin Context Menu 

void UComposableCameraNodeGraphSchema::GetContextMenuActions(class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const
{
	Super::GetContextMenuActions(Menu, Context);

	if (!Context || !Menu)
	{
		return;
	}

	const UEdGraphPin* ClickedPin = Context->Pin;
	const UEdGraphNode* ClickedNode = Context->Node;

	// Classify the right-click. Three mutually-exclusive branches:
	//
	// pin-on-node -> signal the graph so the toolkit's deferred selection
	// handler can suppress its details-panel repoint, then
	// add the pin quick-actions if the pin is an exposable
	// camera-node data input.
	//
	// node-only -> add the node-body actions (Delete).
	//
	// neither->nothing (the graph-level palette is built elsewhere, in
	// GetGraphContextActions).
	//
	// The flag is set via MarkPinContextMenuRequested rather than a direct
	// field poke so the entire pin-context signal lifecycle lives in two
	// methods on UComposableCameraNodeGraph.
	if (ClickedPin && ClickedNode)
	{
		if (UComposableCameraNodeGraph* OwningGraph =
			Cast<UComposableCameraNodeGraph>(const_cast<UEdGraph*>(ClickedNode->GetGraph())))
		{
			OwningGraph->MarkPinContextMenuRequested();
		}

		if (UComposableCameraNodeGraphNode* CameraGraphNode =
			Cast<UComposableCameraNodeGraphNode>(const_cast<UEdGraphNode*>(ClickedNode)))
		{
			if (ClickedPin->Direction == EGPD_Input &&
				ClickedPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
			{
				BuildPinContextMenuActions(Menu, CameraGraphNode, ClickedPin);
			}
		}
		return;
	}

	if (ClickedNode && ClickedNode->CanUserDeleteNode())
	{
		BuildNodeContextMenuActions(Menu, ClickedNode);
	}
}

// Context Menu Builders 

void UComposableCameraNodeGraphSchema::BuildPinContextMenuActions(UToolMenu* Menu,
	UComposableCameraNodeGraphNode* CameraGraphNode,
	const UEdGraphPin* ClickedPin)
{
	// Callers already verified: camera graph node, input data pin (non-exec),
	// non-null menu and pin. No need to re-validate here; the dispatcher is
	// the single source of truth for the classification.
	FToolMenuSection& Section = Menu->AddSection("ComposableCameraParameter",
		LOCTEXT("ParameterSectionLabel", "Camera Parameter"));

	const FName PinName = ClickedPin->PinName;
	const bool bIsExposed = CameraGraphNode->IsInputPinExposed(PinName);

	if (bIsExposed)
	{
		Section.AddMenuEntry(
			"UnexposeParameter",
			LOCTEXT("UnexposeParameter", "Unexpose Parameter"),
			LOCTEXT("UnexposeParameterTooltip", "Remove this pin from the camera type's exposed parameters."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([CameraGraphNode, PinName]()
			{
				// Open a transaction so Ctrl+Z restores the ExposedParameters
				// entry, the pin connections, and the [Exposed] chip as a
				// single atomic unit. UnexposePinParameter's internal Modify()
				// call is a no-op without an active transaction on the stack.
				const FScopedTransaction Transaction(LOCTEXT("UnexposeParameterAction", "Unexpose Camera Parameter"));
				CameraGraphNode->UnexposePinParameter(PinName);
			}))
		);
	}
	else
	{
		Section.AddMenuEntry(
			"ExposeAsParameter",
			LOCTEXT("ExposeAsParameter", "Expose as Camera Parameter"),
			LOCTEXT("ExposeAsParameterTooltip", "Expose this input pin as a parameter callers can set when activating this camera type."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([CameraGraphNode, PinName]()
			{
				// See the Unexpose branch above for why we need our own
				// FScopedTransaction here. The Modify() inside
				// ExposePinAsParameter only captures state if a transaction is
				// already active, and the tool-menu execution path does not
				// open one for us.
				const FScopedTransaction Transaction(LOCTEXT("ExposeParameterAction", "Expose Camera Parameter"));
				CameraGraphNode->ExposePinAsParameter(PinName);
			}))
		);
	}
}

void UComposableCameraNodeGraphSchema::BuildNodeContextMenuActions(UToolMenu* Menu,
	const UEdGraphNode* ClickedNode)
{
	// Callers already verified: non-null ClickedNode, no ClickedPin,
	// CanUserDeleteNode() == true. The dispatcher owns that classification.
	//
	// The FGenericCommands::Delete binding routes through the graph editor's
	// command list (see FComposableCameraTypeAssetEditorToolkit::CreateGraphEditorCommands),
	// which honors CanUserDeleteNode on each selected node. We use a lambda
	// here rather than routing to that binding because the tool menu context
	// doesn't always carry the graph editor widget's command list, and we
	// need a deterministic single-node delete that works from any menu entry
	// point. Multi-select deletion still goes through the command list and
	// DeleteSelectedNodes().
	FToolMenuSection& NodeSection = Menu->AddSection("ComposableCameraNodeActions",
		LOCTEXT("NodeActionsSectionLabel", "Node Actions"));

	NodeSection.AddMenuEntry(
		"DeleteNode",
		LOCTEXT("DeleteNode", "Delete"),
		LOCTEXT("DeleteNodeTooltip", "Delete this node from the graph."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Delete"),
		FUIAction(FExecuteAction::CreateLambda([ClickedNode]()
			{
				if (ClickedNode && ClickedNode->CanUserDeleteNode())
				{
					UEdGraph* Graph = ClickedNode->GetGraph();
					UEdGraphNode* MutableNode = const_cast<UEdGraphNode*>(ClickedNode);
					MutableNode->Modify();
					MutableNode->DestroyNode();
					if (UComposableCameraNodeGraph* NodeGraph = Cast<UComposableCameraNodeGraph>(Graph))
					{
						NodeGraph->SyncToTypeAsset();
						NodeGraph->NotifyGraphChanged();
					}
				}
			}),
			FCanExecuteAction::CreateLambda([ClickedNode]()
			{
				return ClickedNode && ClickedNode->CanUserDeleteNode();
			})
		)
	);
}

// Pin Colors 

FLinearColor UComposableCameraNodeGraphSchema::GetPinTypeColor(const FEdGraphPinType& PinType) const
{
	// The palette itself lives in `FComposableCameraEditorColors` - see
	// ComposableCameraEditorStyle.h for the full table and the rationale
	// for keeping pin colors centralised.

	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
	{
		return FComposableCameraEditorColors::PinExec;
	}
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
	{
		return FComposableCameraEditorColors::PinBool;
	}
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Int)
	{
		return FComposableCameraEditorColors::PinInt;
	}
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Real)
	{
		return FComposableCameraEditorColors::PinFloat;
	}
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
	{
		if (PinType.PinSubCategoryObject == TBaseStructure<FVector>::Get()
			|| PinType.PinSubCategoryObject == TBaseStructure<FVector2D>::Get()
			|| PinType.PinSubCategoryObject == TBaseStructure<FVector4>::Get())
		{
			return FComposableCameraEditorColors::PinVector;
		}
		if (PinType.PinSubCategoryObject == TBaseStructure<FRotator>::Get())
		{
			return FComposableCameraEditorColors::PinRotator;
		}
		if (PinType.PinSubCategoryObject == TBaseStructure<FTransform>::Get())
		{
			return FComposableCameraEditorColors::PinTransform;
		}
		return FComposableCameraEditorColors::PinStructGeneric;
	}
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Object)
	{
		return FComposableCameraEditorColors::PinObject;
	}
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Name)
	{
		return FComposableCameraEditorColors::PinName;
	}
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
	{
		// Enums are PC_Byte with a UEnum sub-category object (see
		// ComposableCameraEdGraphPinTypeUtils::MakeEdGraphPinTypeFromCameraPinType).
		// Plain (non-enum) byte pins also land here - visually distinct from
		// every other category in the schema, so the shared color is fine.
		return FComposableCameraEditorColors::PinByteEnum;
	}
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Delegate)
	{
		return FComposableCameraEditorColors::PinDelegate;
	}

	return FComposableCameraEditorColors::PinDefault;
}

// Connection Drawing 

FConnectionDrawingPolicy* UComposableCameraNodeGraphSchema::CreateConnectionDrawingPolicy(int32 InBackLayerID,
	int32 InFrontLayerID,
	float InZoomFactor,
	const FSlateRect& InClippingRect,
	FSlateWindowElementList& InDrawElements,
	UEdGraph* InGraphObj) const
{
	return new FComposableCameraConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements);
}

// Default Nodes 

void UComposableCameraNodeGraphSchema::CreateDefaultNodesForGraph(UEdGraph& Graph) const
{
	// This path fires for brand-new graphs that have never been rebuilt from
	// a type asset (i.e. right after asset creation, before the toolkit's
	// first RebuildFromTypeAsset). For any subsequent open, EditorGraph is
	// Transient so the graph starts empty and RebuildFromTypeAsset creates
	// all sentinels via its own phase functions - the defaults here are only
	// seen during initial asset authoring.
	//
	// Three sentinels are created unconditionally: the per-frame Start (top
	// left), the BeginPlay compute-chain Start (below the per-frame Start),
	// and the Output terminator (far right). The compute-chain sentinel has
	// no terminator - compute nodes chain off it freely and the runtime
	// executes whatever walk-order falls out at BeginPlay.

	// Create the Start node (far left).
	UComposableCameraStartGraphNode* StartNode = NewObject<UComposableCameraStartGraphNode>(
		&Graph, NAME_None, RF_Transactional);
	StartNode->CreateNewGuid();
	StartNode->NodePosX = -300.0f;
	StartNode->NodePosY = 0.0f;
	StartNode->AllocateDefaultPins();
	Graph.AddNode(StartNode, /*bFromUI=*/false, /*bSelectNewNode=*/false);

	// Create the BeginPlay Start node below the main Start. Position is
	// taken from the type asset when we can resolve it; otherwise we fall
	// back to the UPROPERTY default (-300, 400). The fallback is important
	// because CreateDefaultNodesForGraph can be invoked on a graph whose
	// OwningTypeAsset back-reference hasn't been wired yet (schema calls do
	// not flow through the toolkit that sets it).
	UComposableCameraBeginPlayStartGraphNode* BeginPlayStartNode =
		NewObject<UComposableCameraBeginPlayStartGraphNode>(&Graph, NAME_None, RF_Transactional);
	BeginPlayStartNode->CreateNewGuid();

	FVector2D BeginPlayStartPos(-300.0, 400.0);
	if (const UComposableCameraNodeGraph* NodeGraph = Cast<UComposableCameraNodeGraph>(&Graph))
	{
		if (NodeGraph->OwningTypeAsset)
		{
			BeginPlayStartPos = NodeGraph->OwningTypeAsset->BeginPlayStartNodePosition;
		}
	}
	BeginPlayStartNode->NodePosX = static_cast<int32>(BeginPlayStartPos.X);
	BeginPlayStartNode->NodePosY = static_cast<int32>(BeginPlayStartPos.Y);
	BeginPlayStartNode->AllocateDefaultPins();
	Graph.AddNode(BeginPlayStartNode, /*bFromUI=*/false, /*bSelectNewNode=*/false);

	// Create the Output node (far right).
	UComposableCameraOutputGraphNode* OutputNode = NewObject<UComposableCameraOutputGraphNode>(
		&Graph, NAME_None, RF_Transactional);
	OutputNode->CreateNewGuid();
	OutputNode->NodePosX = 600.0f;
	OutputNode->NodePosY = 0.0f;
	OutputNode->AllocateDefaultPins();
	Graph.AddNode(OutputNode, /*bFromUI=*/false, /*bSelectNewNode=*/false);
}

// Static Helpers 

UEdGraphNode* UComposableCameraNodeGraphSchema::AddNodeToGraph(UEdGraph* Graph, TSubclassOf<UComposableCameraCameraNodeBase> NodeClass, const FVector2D& Location)
{
	if (!Graph || !NodeClass)
	{
		return nullptr;
	}

	// Find the owning type asset.
	UComposableCameraTypeAsset* TypeAsset = nullptr;
	if (UComposableCameraNodeGraph* NodeGraph = Cast<UComposableCameraNodeGraph>(Graph))
	{
		TypeAsset = NodeGraph->OwningTypeAsset;
	}

	if (!TypeAsset)
	{
		TypeAsset = Cast<UComposableCameraTypeAsset>(Graph->GetOuter());
	}

	if (!TypeAsset)
	{
		return nullptr;
	}

	// Compute nodes are also UComposableCameraCameraNodeBase subclasses (they
	// reuse the pin plumbing) but live in a separate template array on the
	// type asset (ComputeNodeTemplates) and participate in the one-shot
	// BeginPlay exec chain rather than the per-frame camera chain. Route
	// them into the correct durable array here so the NodeIndex the graph
	// node caches is consistent with whichever array it will be resolved
	// against during future Rebuild calls.
	const bool bIsComputeClass = NodeClass->IsChildOf(UComposableCameraComputeNodeBase::StaticClass());

	// Open our own transaction so Ctrl+Z cleanly removes the new NodeTemplate,
	// the new UEdGraphNode, and any pin defaults it inherited - as a single
	// atomic unit. UEdGraphSchema does NOT auto-wrap PerformAction in a
	// transaction the way K2's FEdGraphSchemaAction_K2NewNode does, and
	// UEdGraph::AddNode / UObject::Modify are no-ops without an active
	// FScopedTransaction on the stack. Without this wrapping, the node-create
	// produces zero undo records, and a subsequent Expose/Unexpose transaction
	// can appear to "swallow" the node-create on Ctrl+Z because it's the only
	// nearby transaction the user sees in the buffer.
	const FScopedTransaction Transaction(bIsComputeClass
			? LOCTEXT("AddComputeNode", "Add Compute Node")
			: LOCTEXT("AddCameraNode", "Add Camera Node"));
	Graph->Modify();
	TypeAsset->Modify();

	// Create the node template on the type asset. Compute and camera node
	// classes share the same base type so NewObject is identical - the only
	// thing that differs is which array we stash the resulting template in
	// and which NodeIndex space it lives in.
	UComposableCameraCameraNodeBase* NewNodeTemplate = NewObject<UComposableCameraCameraNodeBase>(TypeAsset, NodeClass, NAME_None, RF_Transactional);

	int32 NewNodeIndex = INDEX_NONE;
	if (bIsComputeClass)
	{
		UComposableCameraComputeNodeBase* NewComputeTemplate =
			Cast<UComposableCameraComputeNodeBase>(NewNodeTemplate);
		// NewObject<UComposableCameraCameraNodeBase>(..., NodeClass, ...)
		// succeeded above with a compute class, so this cast must succeed
		// too - but assert via check() to surface a typo (e.g. filtering the
		// palette against the wrong base) as a hard failure rather than a
		// silent index mismatch.
		check(NewComputeTemplate);
		NewNodeIndex = TypeAsset->ComputeNodeTemplates.Add(NewComputeTemplate);
	}
	else
	{
		NewNodeIndex = TypeAsset->NodeTemplates.Add(NewNodeTemplate);
	}

	// Create the graph node. The same UComposableCameraNodeGraphNode class
	// is reused for both camera and compute graph nodes - the distinction
	// is entirely in the underlying template class, which the sync/rebuild
	// phases classify at round-trip time.
	UComposableCameraNodeGraphNode* GraphNode = NewObject<UComposableCameraNodeGraphNode>(Graph, NAME_None, RF_Transactional);
	GraphNode->NodeTemplate = NewNodeTemplate;
	GraphNode->NodeIndex = NewNodeIndex;
	GraphNode->CreateNewGuid();
	GraphNode->NodePosX = Location.X;
	GraphNode->NodePosY = Location.Y;
	GraphNode->AllocateDefaultPins();

	Graph->AddNode(GraphNode, /*bFromUI=*/true, /*bSelectNewNode=*/true);

	TypeAsset->MarkPackageDirty();

	return GraphNode;
}

EComposableCameraGraphChain UComposableCameraNodeGraphSchema::ClassifyChainForNode(const UEdGraphNode* Node)
{
	// Compute-side identities: the BeginPlay Start sentinel and any graph
	// node whose underlying NodeTemplate is a compute node class.
	//
	// Variable Set nodes are dynamically classified by following their ExecIn
	// wire backward to the nearest node with a definitive classification
	// (sentinel or camera/compute graph node). This supports variable nodes
	// living on either chain - the first exec wire determines the chain.
	//
	// Variable Get nodes (no exec pins) are chain-agnostic pure data readers.
	// They default to Camera for classification purposes, but
	// CanCreateConnection relaxes the cross-chain check for Get node data
	// wires so they can feed into either chain's node pins.
	//
	// Everything else - main Start sentinel, Output sentinel, regular camera
	// graph nodes - falls into the camera chain.

	if (!Node)
	{
		return EComposableCameraGraphChain::Camera;
	}

	if (Node->IsA<UComposableCameraBeginPlayStartGraphNode>())
	{
		return EComposableCameraGraphChain::Compute;
	}

	if (const UComposableCameraNodeGraphNode* CameraGraphNode = Cast<UComposableCameraNodeGraphNode>(Node))
	{
		if (CameraGraphNode->NodeTemplate &&
			CameraGraphNode->NodeTemplate->IsA<UComposableCameraComputeNodeBase>())
		{
			return EComposableCameraGraphChain::Compute;
		}
	}

	// Variable Set nodes: follow the exec chain backward to determine which
	// chain they belong to. The exec chain is linear (one ExecIn per node),
	// so we walk backward iteratively until we hit a node with a definitive
	// classification. Cycle guard prevents infinite loops on malformed graphs.
	if (const UComposableCameraVariableGraphNode* VarNode = Cast<UComposableCameraVariableGraphNode>(Node))
	{
		if (VarNode->bIsSetter)
		{
			TSet<const UEdGraphNode*> Visited;
			Visited.Add(Node);

			const UEdGraphNode* Current = Node;
			while (Current)
			{
				// Find the ExecIn pin on the current node. Set variable nodes
				// use their own pin name; pipeline graph nodes use the shared
				// base class pin name. Try both.
				const UEdGraphPin* ExecIn = Current->FindPin(UComposableCameraVariableGraphNode::PN_ExecIn, EGPD_Input);
				if (!ExecIn)
				{
					ExecIn = Current->FindPin(UComposableCameraGraphNodeBase::PN_ExecIn, EGPD_Input);
				}
				if (!ExecIn || ExecIn->LinkedTo.Num() == 0 || !ExecIn->LinkedTo[0])
				{
					break;
				}

				UEdGraphNode* Predecessor = ExecIn->LinkedTo[0]->GetOwningNode();
				if (!Predecessor || Visited.Contains(Predecessor))
				{
					break;
				}
				Visited.Add(Predecessor);

				// Check if the predecessor has a definitive classification.
				if (Predecessor->IsA<UComposableCameraStartGraphNode>())
				{
					return EComposableCameraGraphChain::Camera;
				}
				if (Predecessor->IsA<UComposableCameraBeginPlayStartGraphNode>())
				{
					return EComposableCameraGraphChain::Compute;
				}
				if (const UComposableCameraNodeGraphNode* PredGraphNode =
					Cast<UComposableCameraNodeGraphNode>(Predecessor))
				{
					if (PredGraphNode->NodeTemplate &&
						PredGraphNode->NodeTemplate->IsA<UComposableCameraComputeNodeBase>())
					{
						return EComposableCameraGraphChain::Compute;
					}
					return EComposableCameraGraphChain::Camera;
				}

				// Predecessor is another variable Set node - keep walking.
				Current = Predecessor;
			}

			// The backward walk exhausted without hitting a definitively-
			// classified node - the Set node has no exec wires (or its
			// predecessors are all other unwired Set nodes). Return
			// Unclassified so CanCreateConnection allows the first exec
			// wire from either chain sentinel.
			return EComposableCameraGraphChain::Unclassified;
		}

		// Get nodes default to Camera (chain-agnostic pure data readers;
		// CanCreateConnection relaxes the cross-chain check for their data
		// wires separately).
		return EComposableCameraGraphChain::Camera;
	}

	return EComposableCameraGraphChain::Camera;
}

bool UComposableCameraNodeGraphSchema::ArePinTypesCompatible(const FEdGraphPinType& SourceType, const FEdGraphPinType& TargetType)
{
	// Exact category match required.
	if (SourceType.PinCategory != TargetType.PinCategory)
	{
		return false;
	}

	// For Real type, Float and Double are interchangeable.
	if (SourceType.PinCategory == UEdGraphSchema_K2::PC_Real)
	{
		return true;
	}

	// For Struct type, the specific struct must match.
	if (SourceType.PinCategory == UEdGraphSchema_K2::PC_Struct)
	{
		return SourceType.PinSubCategoryObject == TargetType.PinSubCategoryObject;
	}

	// For Object type, check inheritance.
	if (SourceType.PinCategory == UEdGraphSchema_K2::PC_Object)
	{
		const UClass* SourceClass = Cast<UClass>(SourceType.PinSubCategoryObject.Get());
		const UClass* TargetClass = Cast<UClass>(TargetType.PinSubCategoryObject.Get());
		if (SourceClass && TargetClass)
		{
			return SourceClass->IsChildOf(TargetClass) || TargetClass->IsChildOf(SourceClass);
		}
		return true; // If either is null (generic UObject), allow it.
	}

	// Bool, Int32 - exact match (already matched by category).
	return true;
}

#undef LOCTEXT_NAMESPACE
