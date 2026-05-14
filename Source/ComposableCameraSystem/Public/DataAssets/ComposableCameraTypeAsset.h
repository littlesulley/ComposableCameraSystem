// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Core/ComposableCameraRuntimeDataBlock.h"
#include "Core/ComposableCameraParameterBlock.h"
#include "Nodes/ComposableCameraNodePinTypes.h"
#include "ComposableCameraTypeAsset.generated.h"

class UComposableCameraCameraNodeBase;
class UComposableCameraComputeNodeBase;
class UComposableCameraTransitionBase;
class AComposableCameraCameraBase;
class AComposableCameraPlayerCameraManager;

/**
 * Build status for camera type asset validation.
 */
UENUM()
enum class EComposableCameraBuildStatus : uint8
{
	NotBuilt,
	Success,
	SuccessWithWarnings,
	Failed
};

/**
 * A single message from the build/validation pipeline.
 */
USTRUCT()
struct FComposableCameraBuildMessage
{
	GENERATED_BODY()

	UPROPERTY()
	uint8 Severity = 0; // 0 = Info, 1 = Warning, 2 = Error

	UPROPERTY()
	FText Message;

	/** Which node the message relates to (-1 for asset-level). */
	UPROPERTY()
	int32 NodeIndex = INDEX_NONE;

	/** Which pin the message relates to (None for node-level). */
	UPROPERTY()
	FName PinName;
};

/**
 * Describes a parameter exposed to callers from a camera type asset.
 * Created when a designer right-clicks a node input pin and selects "Expose as Camera Parameter".
 */
USTRUCT(BlueprintType)
struct COMPOSABLECAMERASYSTEM_API FComposableCameraExposedParameter
{
	GENERATED_BODY()

	/** Unique name of the parameter (used in K2Node pins, DataTable columns, and ParameterBlock keys).
	 *
	 *  Read-only on purpose: this name is the lookup key for every consumer
	 *  (K2 node UserOverrideNames, DataTable row Values map, ParameterBlock
	 *  activation keys, the row editor's orphan detection, the K2 node's
	 *  per-pin SetParameterBlockValue emission). Editing it from the Details
	 *  panel would silently break all of those without any rename plumbing.
	 *  To rename an exposed parameter, unexpose it from the graph and re-expose
	 *  the underlying pin under a new name (or rename the underlying C++ pin
	 *  declaration in code). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Parameter")
	FName ParameterName;

	/** Display name shown in the editor and K2Node. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	FText DisplayName;

	/** The data type of this parameter (mirrors the source node pin's type). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Parameter")
	EComposableCameraPinType PinType = EComposableCameraPinType::Float;

	/** For Struct types: the specific USTRUCT. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Parameter")
	TObjectPtr<UScriptStruct> StructType = nullptr;

	/** For Enum types: the specific UEnum. Mirrors the source node pin's EnumType. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Parameter")
	TObjectPtr<UEnum> EnumType = nullptr;

	/** For Delegate types: the UFunction defining the delegate signature. Mirrors
	 *  the source node pin's SignatureFunction. Used by the editor to emit a
	 *  PC_Delegate pin with the correct MemberReference. Ignored for other pin types. */
	UPROPERTY()
	TObjectPtr<UFunction> SignatureFunction = nullptr;

	/** Which node this parameter feeds into (index in NodeTemplates). */
	UPROPERTY()
	int32 TargetNodeIndex = INDEX_NONE;

	/** Which input pin on the target node this parameter feeds into. */
	UPROPERTY()
	FName TargetPinName;

	/** Whether the caller is required to provide this parameter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	bool bRequired = false;

	/** Tooltip shown in the K2Node and editor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	FText Tooltip;
};

/**
 * Describes a camera-level variable (internal or caller-exposed).
 *
 * This is the struct used for BOTH InternalVariables and ExposedVariables on
 * UComposableCameraTypeAsset. The two arrays share the same field layout and
 * the runtime also stores both in a single FNameffset map. The distinction
 * lives purely at the authoring surface: which array an entry lives in.
 *
 *  - InternalVariables: node-only slots. InitialValueString is applied at camera
 *    instantiation; callers have no way to override it.
 *
 *  - ExposedVariables:  same node-level read/write semantics, but the caller's
 *    FComposableCameraParameterBlock may override the initial value at
 *    activation time (by keying on VariableName, same channel used for
 *    ExposedParameters). After activation, there is no external read/write API
 *    for them. They behave exactly like internal variables from that point on.
 *
 * All fields apply to both usages; none are ignored in either case.
 */
USTRUCT(BlueprintType)
struct COMPOSABLECAMERASYSTEM_API FComposableCameraInternalVariable
{
	GENERATED_BODY()

	/**
	 * Stable identifier for this variable, independent of VariableName.
	 *
	 * VariableName is the user-facing key used at runtime to look up values in
	 * the ParameterBlock / RuntimeDataBlock, but the editor needs a separate
	 * identity that survives renames so existing Get/Set variable graph nodes
	 * can follow a variable across a rename in the Details panel. The editor's
	 * UComposableCameraVariableGraphNode tracks its variable by this GUID, and
	 * the toolkit populates a missing GUID lazily on load (see
	 * UComposableCameraTypeAsset::PostLoad).
	 *
	 * This field is never read by the runtime. It exists purely for editor
	 * identity tracking and serialization round-trip.
	 */
	UPROPERTY()
	FGuid VariableGuid;

	/** Name of this variable.
	 *
	 *  Serves both as the runtime lookup key (ParameterBlock / RuntimeDataBlock)
	 *  and as the display label in the graph editor context menu and on
	 *  Get/Set variable nodes. There is no separate DisplayName. The FName IS
	 *  the display name. Renames are tracked via VariableGuid so existing
	 *  Get/Set graph nodes follow the variable through edits in the Details panel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variable")
	FName VariableName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variable")
	EComposableCameraPinType VariableType = EComposableCameraPinType::Float;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variable")
	TObjectPtr<UScriptStruct> StructType = nullptr;

	/** For VariableType == Enum: the specific UEnum this variable represents.
	 *  Stored internally as a normalized int64 (matching Enum pin behavior). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variable")
	TObjectPtr<UEnum> EnumType = nullptr;

	/** Initial value at camera instantiation (serialized string). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variable")
	FString InitialValueString;

	/** If true, the variable resets to InitialValue at the start of every frame (before node execution).
	 *  If false, the variable persists across frames. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variable")
	bool bResetEveryFrame = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variable")
	FText Tooltip;
};

/**
 * Data asset defining a camera type: node composition, exposed parameters,
 * internal variables, pin connections, and default transition.
 *
 * Replaces the need to create Blueprint subclasses of AComposableCameraCameraBase
 * for most camera types. Designers create and configure camera types entirely
 * within the visual node graph editor.
 *
 * At runtime, Instantiate() creates an AComposableCameraCameraBase with node
 * instances duplicated from the templates, a wired RuntimeDataBlock, and caller-provided
 * parameter values.
 */
UCLASS(BlueprintType, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraTypeAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// --- Node Chain --------------------------------------------------------

	/** Flat list of node template UObjects owned by this asset.
	 *
	 *  This array is intentionally **hidden from the Details panel**. The visual
	 *  node graph editor is the authoritative editing surface for camera nodes,
	 *  and showing a separate "NodeTemplates" array in Details would tempt users
	 *  into thinking its order has semantic meaning. It does not: execution order
	 *  comes from the exec-pin wiring chain (see FullExecChain / ExecutionOrder),
	 *  not from this array's order. The array exists purely as a GC anchor and
	 *  serialization container for the instanced node UObjects.
	 *
	 *  UPROPERTY has no EditAnywhere / Category keywords on purpose. The field
	 *  is still serialized (bare UPROPERTY) and Instanced (so subobjects round-trip),
	 *  but invisible to IDetailCustomization panels. Editor code accesses this
	 *  array directly through SyncToTypeAsset / RebuildFromTypeAsset. */
	UPROPERTY(Instanced)
	TArray<TObjectPtr<UComposableCameraCameraNodeBase>> NodeTemplates;

	/** Per-asset authoring overrides for the pins on each node template.
	 *
	 *  Parallel array to NodeTemplates: NodePinOverrides[i] holds the sparse
	 *  list of per-pin overrides for NodeTemplates[i]. Entries store the
	 *  user-edited default value and the bAsPin toggle for a specific
	 *  (node instance, pin name) pair; pins without an entry inherit their
	 *  class-level declaration defaults (bAsPin = true, DefaultValueString).
	 *
	 *  Invariant: after any successful SyncToTypeAsset,
	 *  NodePinOverrides.Num() == NodeTemplates.Num(). Legacy assets saved
	 *  before this field existed start with an empty array; the first sync
	 *  after load grows it to match.
	 *
	 *  Hidden from the Details panel for the same reason NodeTemplates is - the visual graph (+ its per-node Details customization) is the
	 *  authoritative editing surface. Editor code accesses this array
	 *  directly through the Sync/Rebuild phases and through the graph
	 *  node's accessor helpers. */
	UPROPERTY()
	TArray<FComposableCameraNodeTemplatePinOverrides> NodePinOverrides;

	// --- BeginPlay Compute Chain -------------------------------------------
	//
	// Parallel to NodeTemplates / NodePinOverrides / PinConnections /
	// FullExecChain, but for one-shot compute nodes that run on the BeginPlay
	// execution chain (see UComposableCameraComputeNodeBase and
	// AComposableCameraCameraBase::BeginPlayCamera for the runtime model).
	//
	// Compute templates live in their own index space. The index used for
	// FComposableCameraPinKey at runtime is (NodeTemplates.Num() + ComputeIdx),
	// which keeps output pin keys unique across the two chains without having
	// to teach the pin key to disambiguate. See
	// BuildRuntimeDataLayout for the offset math and OnTypeAssetCameraConstructed
	// for the duplication + wiring that applies it.
	//
	// Schema invariant (enforced editor-side): no direct data wires between a
	// compute node and a camera node. Cross-chain communication goes through
	// internal / exposed variables: the compute node calls SetInternalVariable
	// from inside ExecuteBeginPlay, and downstream camera nodes read via
	// GetInternalVariable at Initialize or TickNode time. Preventing cross-chain
	// data wires at wire time keeps PinConnections / ComputePinConnections as
	// two non-overlapping index domains.

	/** Flat list of compute node templates authored on this type asset.
	 *  Hidden from Details for the same reason NodeTemplates is. The visual
	 *  graph editor is the authoritative editing surface. */
	UPROPERTY(Instanced)
	TArray<TObjectPtr<UComposableCameraComputeNodeBase>> ComputeNodeTemplates;

	/** Per-asset authoring overrides for the pins on each compute node
	 *  template. Parallel array to ComputeNodeTemplates; same semantics as
	 *  NodePinOverrides. Invariant: after any successful SyncToTypeAsset,
	 *  ComputeNodePinOverrides.Num() == ComputeNodeTemplates.Num(). */
	UPROPERTY()
	TArray<FComposableCameraNodeTemplatePinOverrides> ComputeNodePinOverrides;

	/** Intra-compute-chain data wires. Same struct as PinConnections but the
	 *  Source/Target node indices refer to ComputeNodeTemplates rather than
	 *  NodeTemplates. Cross-chain data wires are disallowed by the schema, so
	 *  this array never contains camera-node endpoints. */
	UPROPERTY()
	TArray<FComposableCameraPinConnection> ComputePinConnections;

	/** Ordered list of compute node indices defining the BeginPlay execution
	 *  chain, filtered to compute nodes only. This is a compute-node-only
	 *  projection of ComputeFullExecChain (analogous to how ExecutionOrder is a
	 *  camera-node-only projection of FullExecChain). Kept for backward compat
	 *  with code paths that only care about node ordering. */
	UPROPERTY()
	TArray<int32> ComputeExecutionOrder;

	/** Full BeginPlay execution chain including both compute nodes and
	 *  internal-variable Set operations, in exec-wire order. Parallel to
	 *  FullExecChain but for the compute chain.
	 *
	 *  The editor walks the exec pin chain starting from the BeginPlay Start
	 *  sentinel's ExecOut and records each step here. For CameraNode entries,
	 *  CameraNodeIndex indexes into ComputeNodeTemplates (not NodeTemplates).
	 *  For SetVariable entries, CameraNodeIndex also indexes
	 *  ComputeNodeTemplates. The source node is always on the same chain.
	 *
	 *  The runtime copies this to AComposableCameraCameraBase::ComputeFullExecChain
	 *  during OnTypeAssetCameraConstructed. BeginPlayCamera walks it to
	 *  interleave compute node execution with scratch-variable writes. */
	UPROPERTY()
	TArray<FComposableCameraExecEntry> ComputeFullExecChain;

	// --- Camera Identity ---------------------------------------------------

	/** Tag for this camera type. Propagated to spawned camera instances so
	 *  modifiers can distinguish different cameras at runtime. Mirrors
	 *  AComposableCameraCameraBase::CameraTag. The TypeAsset carries it so
	 *  designers don't need to subclass the camera in Blueprint just to set a tag. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
	FGameplayTag CameraTag;

	/** Whether cameras of this type preserve the previous camera's pose when
	 *  resumed from the context stack (e.g., after a transient camera pops).
	 *  Propagated to spawned camera instances. Mirrors
	 *  AComposableCameraCameraBase::bDefaultPreserveCameraPose. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
	bool bDefaultPreserveCameraPose = true;

	// --- Transitions ------------------------------------------------------

	/** Optional enter transition. Used when this camera type becomes active.
	 *  The full resolution chain is:
	 *    1. Caller-supplied override
	 *    2. Transition table lookup (Source -> Target pair)
	 *    3. Source's ExitTransition
	 *    4. Target's EnterTransition  ->this field
	 *    5. Hard cut
	 *  Callers can always override via the activation API. */
	UPROPERTY(EditAnywhere, Instanced, Category = "Transition")
	TObjectPtr<UComposableCameraTransitionBase> EnterTransition;

	/** Optional exit transition. Used when leaving this camera type.
	 *  Checked at priority 3 in the resolution chain (after the table,
	 *  before the target's EnterTransition). Useful for cameras that
	 *  must always leave with a specific transition regardless of what
	 *  comes next (e.g., puzzle cameras, UI overlays). */
	UPROPERTY(EditAnywhere, Instanced, Category = "Transition")
	TObjectPtr<UComposableCameraTransitionBase> ExitTransition;

	// --- Exposed Parameters ------------------------------------------------

	/** Parameters that callers provide when activating this camera type.
	 *
	 *  Authoring model. Split between graph and Details panel:
	 *
	 *    - **Structure** (which pins are exposed) is authored **exclusively**
	 *      through the visual graph editor: designers right-click a node input
	 *      pin and select "Expose as Camera Parameter" (or "Unexpose"). The
	 *      array is `EditFixedSize`, so the Details panel does not offer
	 *      Add/Remove buttons. Adding a free-standing entry that doesn't map
	 *      to any node pin would have no runtime meaning, and removing one
	 *      from Details would silently leave the underlying pin in an
	 *      "orphaned-exposed" state on the graph side.
	 *
	 *    - **Per-parameter metadata** (DisplayName, bRequired, Tooltip) IS
	 *      editable on existing entries from
	 *      the Details panel. The first time a pin is exposed, these fields
	 *      are seeded from the C++ FComposableCameraNodePinDeclaration, but
	 *      the per-asset values become the source of truth from that point on
	 *      and are preserved across SyncToTypeAsset (which only rewrites
	 *      TargetNodeIndex when node ordering changes; see
	 *      ComposableCameraNodeGraph.cpp Step 7).
	 *
	 *    - **Identity fields** (ParameterName, PinType, StructType,
	 *      TargetNodeIndex, TargetPinName) are deliberately read-only or
	 *      hidden. Renaming or retyping them in Details would silently break
	 *      every consumer keying by name (K2 node UserOverrideNames, DataTable
	 *      row Values, ParameterBlock activations, the row editor's orphan
	 *      detection). To rename or retype, change it on the underlying pin
	 *      and re-expose. */
	UPROPERTY(EditAnywhere, EditFixedSize, Category = "Parameters")
	TArray<FComposableCameraExposedParameter> ExposedParameters;

	// --- Internal Variables ------------------------------------------------

	/** Camera-level variables not exposed to callers but readable/writable by nodes.
	 *  Used for cross-node communication and cross-frame state caching.
	 *
	 *  The struct type (FComposableCameraInternalVariable) is shared with
	 *  ExposedVariables below; the two categories differ only in whether the
	 *  caller's ParameterBlock may override the initial value at activation time
	 *  (ExposedVariables: yes; InternalVariables: no). At runtime both arrays
	 *  feed the same InternalVariableOffsets map on the data block -Get/Set
	 *  variable graph nodes treat them uniformly. Names must be unique across
	 *  ExposedParameters inInternalVariables inExposedVariables; see Build(). */
	UPROPERTY(EditAnywhere, Category = "Internal Variables")
	TArray<FComposableCameraInternalVariable> InternalVariables;

	// --- Exposed Variables -------------------------------------------------

	/** Camera-level variables whose initial value may be overridden by the
	 *  caller at activation time, but which are otherwise identical to internal
	 *  variables.
	 *
	 *  Authoring model: edited in the Details panel like InternalVariables (not
	 *  via the graph's "Expose as Camera Parameter" flow, which is for input
	 *  pins on camera nodes). The struct type is the same as InternalVariables.
	 *
	 *  Activation model: ApplyParameterBlock() first tries to copy the caller's
	 *  ParameterBlock entry keyed by VariableName into this slot; if the caller
	 *  didn't supply a value, it falls back to parsing InitialValueString via
	 *  FComposableCameraParameterBlock::ApplyStringValue (the same parser the
	 *  DataTable row path uses).
	 *
	 *  Runtime model: after the activation-time write, these are indistinguishable
	 *  from InternalVariables. Their slots live in the same
	 *  FComposableCameraRuntimeDataBlock::InternalVariableOffsets map, and the
	 *  editor's variable graph nodes (Get/Set) find them via the same
	 *  FindVariable() lookup path. Both arrays are searched.
	 *
	 *  Name uniqueness is enforced across ExposedParameters inInternalVariables
	 *  inExposedVariables in Build(); see that function for the rationale. */
	UPROPERTY(EditAnywhere, Category = "Exposed Variables")
	TArray<FComposableCameraInternalVariable> ExposedVariables;

	// --- Pin Wiring (serialized by the editor) -----------------------------

	/** Describes all data-pin connections between nodes.
	 *  Each entry maps a target node's input pin to a source node's output pin. */
	UPROPERTY()
	TArray<FComposableCameraPinConnection> PinConnections;

	// --- Execution Chain (serialized by the editor) ------------------------

	/** Ordered list of node indices defining the execution chain, filtered down
	 *  to camera nodes only.
	 *
	 *  ExecutionOrder[0] is the first camera node executed (closest to Start's
	 *  exec out), ExecutionOrder[Last] is the last (closest to Output's exec in).
	 *
	 *  This is a cached projection of FullExecChain for runtime consumers that
	 *  only care about camera nodes. Set-variable entries from FullExecChain are
	 *  stripped out here; the runtime (once it starts honoring scratch writes)
	 *  should iterate FullExecChain instead. */
	UPROPERTY()
	TArray<int32> ExecutionOrder;

	/** Full execution chain including both camera nodes and internal-variable
	 *  Set operations, in exec-wire order.
	 *
	 *  The editor walks the exec pin chain in the visual graph starting from the
	 *  Start sentinel's ExecOut and records each step here. Set-variable entries
	 *  capture the variable GUID being written plus the camera-node output pin
	 *  that supplies the value; camera-node entries just capture the node index.
	 *
	 *  ExecutionOrder above is a camera-node-only projection of this array. */
	UPROPERTY()
	TArray<FComposableCameraExecEntry> FullExecChain;

	// --- Variable Graph Nodes (serialized by the editor) -------------------

	/** Editor-only records describing each Get/Set variable graph node in the
	 *  visual editor, along with the camera-node pins each one is wired to.
	 *  The runtime doesn't consume these directly; they exist so the editor can
	 *  round-trip the variable-node layout and wires. */
	UPROPERTY()
	TArray<FComposableCameraVariableNodeRecord> VariableNodes;

	// --- Editor-Only Data --------------------------------------------------

#if WITH_EDITORONLY_DATA
	/** Canvas positions of each camera graph node, parallel to NodeTemplates.
	 *  NodeTemplatePositions[i] is the (X, Y) position of the graph node backed
	 *  by NodeTemplates[i]. Written by SyncToTypeAsset and read by
	 *  RebuildFromTypeAsset so the user's layout survives save/reopen.
	 *
	 *  Invariant: after any successful SyncToTypeAsset,
	 *  NodeTemplatePositions.Num() == NodeTemplates.Num(). For legacy assets
	 *  that were saved before this field existed, the array is empty and
	 *  RebuildFromTypeAsset falls back to a default horizontal layout. The
	 *  first save after load will populate it.
	 *
	 *  Editor-only because layout is purely a visual concern. */
	UPROPERTY()
	TArray<FVector2D> NodeTemplatePositions;

	/** Canvas position of the Start sentinel node. Serialized so the sentinel
	 *  stays where the user dragged it across save/reopen. Default matches the
	 *  layout produced for a freshly-created asset. */
	UPROPERTY()
	FVector2D StartNodePosition = FVector2D(-300.0, 0.0);

	/** Canvas position of the Output sentinel node. Serialized so the sentinel
	 *  stays where the user dragged it across save/reopen. Default matches the
	 *  layout produced for a freshly-created asset. */
	UPROPERTY()
	FVector2D OutputNodePosition = FVector2D(600.0, 0.0);

	/** Canvas positions of each compute graph node, parallel to
	 *  ComputeNodeTemplates. Same semantics as NodeTemplatePositions but for
	 *  the BeginPlay compute chain. Invariant: after any successful
	 *  SyncToTypeAsset, ComputeNodeTemplatePositions.Num() ==
	 *  ComputeNodeTemplates.Num(). */
	UPROPERTY()
	TArray<FVector2D> ComputeNodeTemplatePositions;

	/** Canvas position of the BeginPlay Start sentinel node. Serialized so
	 *  the sentinel stays where the user dragged it across save/reopen. The
	 *  default places it below the main Start sentinel so brand-new type
	 *  assets get a visually distinct landing spot for the compute chain. */
	UPROPERTY()
	FVector2D BeginPlayStartNodePosition = FVector2D(-300.0, 400.0);


	/** EdGraph for the visual node editor. Created lazily by the editor toolkit.
	 *
	 *  MUST be Transient. The editor graph is a pure derived view. Every single
	 *  thing it contains (graph nodes, pin wires, layout, variable graph nodes,
	 *  exec chain) is reconstructed from scratch by
	 *  UComposableCameraNodeGraph::RebuildFromTypeAsset on every editor open,
	 *  reading from this TypeAsset's durable fields (NodeTemplates, PinConnections,
	 *  ExecutionOrder, FullExecChain, VariableNodes, OutputConnection*,
	 *  ExposedParameters). Serializing the EditorGraph adds zero value and creates
	 *  multiple failure modes that were observed as "all content I created is
	 *  gone on reopen":
	 *
	 *    1. DefaultToInstanced cross-outer collisions between
	 *       GraphNode->NodeTemplate and TypeAsset->NodeTemplates. Because
	 *       UComposableCameraCameraNodeBase is declared with DefaultToInstanced,
	 *       UHT implicitly promotes any TObjectPtr<UComposableCameraCameraNodeBase>
	 *       property to Instanced. So the GraphNode's serializer tries to
	 *       export NodeTemplate inline under itself, colliding with the
	 *       TypeAsset's NodeTemplates array export of the same object. See
	 *       ComposableCameraNodeGraphNode.h and EditorDesignDoc Section 8 "Gotcha".
	 *
	 *    2. Re-entrancy hazards on load. RebuildFromTypeAsset starts by calling
	 *       RemoveNode on every existing graph node; RemoveNode fires
	 *       NotifyGraphChanged -> the toolkit's OnGraphChanged->SyncToTypeAsset,
	 *       which walks the (partially-drained) Nodes array and writes the
	 *       resulting empty NodeTemplates back to the TypeAsset, clobbering
	 *       the freshly-loaded durable data.
	 *
	 *    3. Load-order hazards between the toolkit's EnsureEditorGraph calls,
	 *       SGraphEditor's own handlers, and any property-change broadcasts
	 *       that fire during initial layout restoration.
	 *
	 *  Marking this Transient means EnsureEditorGraph always takes the
	 *  `if (!TypeAsset->EditorGraph)` branch on load and creates a fresh empty
	 *  graph. RebuildFromTypeAsset then fills it from the authoritative durable
	 *  state. No serialization of graph nodes ever happens, so none of the
	 *  above hazards can occur.
	 *
	 *  IMPORTANT: this is a UPROPERTY flag change. Live Coding will NOT pick
	 *  this up. You must do a full editor restart after changing it. */
	UPROPERTY(Transient)
	TObjectPtr<UEdGraph> EditorGraph;

	/** Last build status. */
	UPROPERTY(Transient)
	EComposableCameraBuildStatus BuildStatus = EComposableCameraBuildStatus::NotBuilt;

	/** Messages from the last build. */
	UPROPERTY(Transient)
	TArray<FComposableCameraBuildMessage> BuildMessages;
#endif

public:
	// --- Runtime API -------------------------------------------------------

	/** Get the list of exposed parameters for K2Node / DataTable introspection. */
	const TArray<FComposableCameraExposedParameter>& GetExposedParameters() const { return ExposedParameters; }

	/** Resolve the effective default value for an exposed parameter.
	 *
	 *  The default lives on the node's pin, not on the ExposedParameter struct.
	 *  This helper looks up NodeTemplates[Param.TargetNodeIndex], gathers its
	 *  pin declarations, finds the one matching Param.TargetPinName, then
	 *  checks NodePinOverrides for a per-instance override. Returns the
	 *  override if present, otherwise the class-level declaration default.
	 *
	 *  Returns an empty string if the target node/pin cannot be resolved
	 *  (stale index, missing pin name, etc.). */
	FString GetExposedParameterDefaultValue(const FComposableCameraExposedParameter& Param) const;

	/**
	 * Build a RuntimeDataBlock layout from this type asset's pin declarations,
	 * connections, exposed parameters, and internal variables.
	 *
	 * This computes all byte offsets and connection mappings. Called once per
	 * camera instantiation (or cached if the asset hasn't changed).
	 */
	FComposableCameraRuntimeDataBlock BuildRuntimeDataLayout() const;

	/**
	 * Fill a RuntimeDataBlock's exposed parameter slots from a ParameterBlock.
	 * Called after BuildRuntimeDataLayout() and before the camera starts ticking.
	 */
	void ApplyParameterBlock(FComposableCameraRuntimeDataBlock& DataBlock,
							 const FComposableCameraParameterBlock& Parameters) const;

	/** Apply delegate bindings from the parameter block to the camera's node UPROPERTYs.
	 *
	 *  Called after ApplyParameterBlock. Iterates exposed parameters that are
	 *  Delegate-typed and, for each one that has an entry in
	 *  Parameters.DelegateValues, writes the FScriptDelegate into the target
	 *  node's FDelegateProperty via reflection.
	 *
	 *  Delegates cannot go through the data block (they're not POD) so this is
	 *  a separate pass that operates directly on node instances. */
	void ApplyDelegateBindings(class AComposableCameraCameraBase* Camera,
							   const FComposableCameraParameterBlock& Parameters) const;

#if WITH_EDITOR
	/**
	 * Validate the asset. Checks for unwired required pins, type mismatches,
	 * duplicate names, etc. Populates BuildStatus and BuildMessages.
	 *
	 * @param bLogResult When true (default), prints a one-line summary to
	 *        `LogComposableCameraSystem` after validation. The editor toolkit's
	 *        graph-sync path passes `false` so continuous editing doesn't spam
	 *        the log. The per-node error badges driven by the same
	 *        BuildMessages serve as the ambient status indicator instead.
	 */
	void Build(bool bLogResult = true);

	/**
	 * Subclass extension point. Called from the tail of Build() so subclasses
	 * can append their own validation results without having to override Build
	 * itself. Default: empty.
	 *
	 * Messages appended here flow through the same BuildMessages / inline-badge
	 * pipeline as the base checks. Used by UComposableCameraPatchTypeAsset to
	 * flag Patch-incompatible nodes (PatchSystemProposal Section 11).
	 */
	virtual void ValidateAdditional(TArray<FComposableCameraBuildMessage>& OutMessages) const {}

	/** Ensure every internal variable has a valid VariableGuid, generating
	 *  one for any legacy entry whose GUID is invalid. Safe to call multiple
	 *  times. It only touches variables whose GUID is invalid. */
	void EnsureInternalVariableGuids();

	/** Ensure every exposed variable has a valid VariableGuid, generating one
	 *  for any legacy entry whose GUID is invalid. Same rationale as
	 *  EnsureInternalVariableGuids. The editor's Get/Set variable graph nodes
	 *  identify variables by GUID so renames in the Details panel don't orphan
	 *  existing graph nodes. Safe to call multiple times. */
	void EnsureExposedVariableGuids();

	/**
	 * Return a name that does not collide with any existing entry in
	 * ExposedParameters inExposedVariables inInternalVariables.
	 *
	 * If BaseName is already free, returns BaseName unchanged. Otherwise,
	 * appends the smallest free numeric suffix ("_2", "_3", ...) and returns
	 * the result. The asset itself is not modified. Callers are responsible
	 * for assigning the returned name to the new entry.
	 *
	 * Used by:
	 *   - UComposableCameraNodeGraphNode::ExposePinAsParameter to disambiguate
	 *     when two camera nodes both have a pin with the same FName and the
	 *     user tries to expose both.
	 *   - DeduplicateExposedNames during PostLoad migration of legacy assets
	 *     that contain duplicates from before this guard existed.
	 *
	 * NameAlreadyOwned (optional): a name to ignore during the collision check.
	 * Use this when renaming an existing entry. Pass the entry's current name
	 * so the helper doesn't consider the entry's own slot as a collision and
	 * leave the name unchanged. Pass NAME_None when adding a brand-new entry.
	 */
	FName MakeUniqueExposedName(FName BaseName, FName NameAlreadyOwned = NAME_None) const;

	/**
	 * Walk ExposedParameters->ExposedVariables ->InternalVariables (in that
	 * order) and rename any duplicate-name entries to a unique suffix using
	 * MakeUniqueExposedName. The first occurrence of each name is preserved
	 * unchanged; subsequent occurrences are renamed.
	 *
	 * Returns true if anything was renamed. The asset is not marked dirty - callers (typically PostLoad) decide whether to flag the package on a
	 * silent migration.
	 *
	 * Logs LogComposableCameraSystem::Warning per rename so the user can spot
	 * what changed in the Output Log on next load.
	 */
	bool DeduplicateExposedNames();

	// --- UObject Editor Interface ------------------------------------------
	virtual void PostLoad() override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif

	// --- UPrimaryDataAsset Interface ---------------------------------------

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
};
