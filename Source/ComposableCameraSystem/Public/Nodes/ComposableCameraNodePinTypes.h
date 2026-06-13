// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/Object.h"
#include "ComposableCameraNodePinTypes.generated.h"

/**
 * Direction of a camera node data pin.
 */
UENUM(BlueprintType)
enum class EComposableCameraPinDirection : uint8
{
	Input,
	Output
};

/**
 * Supported data types for camera node pins.
 */
UENUM(BlueprintType)
enum class EComposableCameraPinType : uint8
{
	Bool,
	Int32,
	Float,
	Double,
	Vector2D,
	Vector3D,
	Vector4,
	Rotator,
	Transform,
	Actor,
	Object,
	/** Custom USTRUCT type. Currently rejected by runtime storage until typed ownership exists. */
	Struct,
	/** FName value. Stored as FName in the data block (POD: NAME_INDEX + NAME_NUMBER). */
	Name,
	/** UENUM value. Stored as a normalized int64 in the data block; the owning
	 *  UEnum* is carried on the declaration and used to narrow-cast into the
	 *  actual property's underlying width (uint8 / int32 / int64) at write time.
	 *  When this is selected, EnumType must be set. */
	Enum,
	/** Single-cast dynamic delegate (FScriptDelegate). NOT stored in the data
	 *  block. Delegates carry heap-owned state and cannot be memcpy'd. Instead
	 *  they are stored in a parallel map on FComposableCameraParameterBlock and
	 *  applied at activation time via reflection (FDelegateProperty). Per-frame
	 *  auto-resolve skips this type. When this is selected, SignatureFunction
	 *  must be set to the UFunction defining the delegate's parameter/return
	 *  signature. */
	Delegate
};

/**
 * Declaration of a single input or output pin on a camera node.
 *
 * Nodes declare their pins by overriding GetPinDeclarations(). The editor reads these
 * declarations to generate visual pins in the node graph, and the runtime uses them to
 * allocate and resolve data in the RuntimeDataBlock.
 */
USTRUCT(BlueprintType)
struct COMPOSABLECAMERASYSTEM_API FComposableCameraNodePinDeclaration
{
	GENERATED_BODY()

	/** Programmatic name of the pin (used in Get/SetPinValue calls and serialized connections). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pin")
	FName PinName;

	/** Display name shown in the editor graph. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pin")
	FText DisplayName;

	/** Whether this is an input or output pin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pin")
	EComposableCameraPinDirection Direction = EComposableCameraPinDirection::Input;

	/** Data type carried by this pin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pin")
	EComposableCameraPinType PinType = EComposableCameraPinType::Float;

	/** For PinType == Struct: the specific USTRUCT type. Ignored for other pin types. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pin")
	TObjectPtr<UScriptStruct> StructType = nullptr;

	/** For PinType == Enum: the specific UEnum the pin represents. Ignored for other
	 *  pin types. The data block always stores this pin's value as a normalized
	 *  int64; this metadata is used at write time to narrow-cast into the actual
	 *  backing property (uint8 / int32 / int64) and by the editor to render the
	 *  enum's display names. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pin")
	TObjectPtr<UEnum> EnumType = nullptr;

	/** For PinType == Delegate: the UFunction defining the delegate signature
	 *  (parameter types and return type). Extracted from the FDelegateProperty's
	 *  SignatureFunction at declaration time. The editor uses this to emit a
	 *  PC_Delegate pin with the correct MemberReference, and the K2 compiler
	 *  validates that wired Custom Events match the signature. Ignored for other
	 *  pin types. */
	UPROPERTY()
	TObjectPtr<UFunction> SignatureFunction = nullptr;

	/** Whether this input is required. If true, the editor shows an error when the pin
	 *  is neither wired nor exposed and has no default value. Ignored for output pins. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pin")
	bool bRequired = false;

	/** Class-level default for whether this pin is exposed as a wire on the
	 *  graph node when a freshly-placed instance has no per-instance override.
	 *  When false, new instances start out as Details-only (no graph wire, not
	 *  exposable as a parameter); the user can still flip it on per-instance
	 *  via the Details panel. Same channel as toggling it off on a pin that
	 *  defaulted to true. The per-instance toggle lives on
	 *  FComposableCameraPinOverride::bAsPin and, when present, supersedes
	 *  this default. Defaults to true so existing pin declarations keep their
	 *  current behavior with no asset migration. Ignored for output pins. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pin")
	bool bDefaultAsPin = true;

	/** Default value for input pins when unwired and not exposed. Stored as serialized string. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pin")
	FString DefaultValueString;

	/** Tooltip shown in the editor when hovering over this pin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pin")
	FText Tooltip;
};

/**
 * Per-asset authoring override for a single pin declared by a camera node class.
 *
 * Pin declarations come from C++ (GetPinDeclarations) and define the *shape* of a
 * node's interface. Names, types, directions, required flags, C++-level default
 * values. But the authoring experience demands two things the class-level decl
 * can't express per-instance:
 *
 *   1. A **user-editable default value** for inputs that are not driven by a
 *      wire. Two instances of the same node class in the same camera type asset
 *      should be able to carry different default strengths / offsets / etc.
 *
 *   2. A **per-instance toggle** for whether a pin should be exposed as a wire
 *      in the graph at all. When bAsPin is false, the pin is Details-only (a
 *      constant set once, at author time) and does not appear on the graph
 *      node. When true, the pin renders on the graph node and can be wired or
 *      exposed as an ExposedParameter.
 *
 * Overrides are stored per-(node-instance, pin-name). Pins that never get an
 * override use the C++ declaration's defaults for both fields (bAsPin =
 * Decl.bDefaultAsPin, DefaultValueString = the class-level default). Storing
 * overrides as a sparse array indexed by PinName means adding a new pin
 * declaration in C++ doesn't require any asset migration. The new pin
 * simply defaults to (Decl.bDefaultAsPin, class default).
 *
 * The source of truth lives on UComposableCameraTypeAsset::NodePinOverrides
 * (parallel array to NodeTemplates). The editor graph node also caches the
 * overrides in a Transient field for fast read during AllocateDefaultPins /
 * Details customization; the round-trip is handled in the same sync/rebuild
 * phases that handle ExposedParameters.
 */
USTRUCT()
struct COMPOSABLECAMERASYSTEM_API FComposableCameraPinOverride
{
	GENERATED_BODY()

	/** Which declared pin this override applies to. Matches
	 *  FComposableCameraNodePinDeclaration::PinName on the node class. */
	UPROPERTY()
	FName PinName;

	/** Whether this pin is exposed as a wire on the graph node. When false, the
	 *  pin is Details-only and does not appear on the graph. It cannot be
	 *  wired, and it cannot be exposed as a camera parameter. The Details panel
	 *  still shows the editable default value, which is used directly as the
	 *  constant input at runtime.
	 *
	 *  Invariant: toggling bAsPin from true to false on a pin that is currently
	 *  wired must break the wire (transactional / undoable), and on a pin that
	 *  is currently exposed as a camera parameter must also auto-unexpose it.
	 *  See ExposePinAsParameter / UnexposePinParameter for the exposure side. */
	UPROPERTY()
	bool bAsPin = true;

	/** True when DefaultValueOverride below should be used in place of the C++
	 *  declaration's class-level default. A separate flag (rather than sniffing
	 *  "DefaultValueOverride is empty") is required to distinguish "user
	 *  deliberately set an empty string" from "user never touched this pin". */
	UPROPERTY()
	bool bHasDefaultOverride = false;

	/** Per-asset override of the pin's default value. Serialized as a string in
	 *  the same format as FComposableCameraNodePinDeclaration::DefaultValueString,
	 *  so the same parser can be used at runtime. Ignored when
	 *  bHasDefaultOverride is false. */
	UPROPERTY()
	FString DefaultValueOverride;
};

/**
 * Per-node-template container for pin overrides, forming a parallel array to
 * UComposableCameraTypeAsset::NodeTemplates. Each entry holds the sparse list of
 * overrides for the corresponding node instance. A wrapper struct (rather than
 * TArray<TArray<->) is used because Unreal's reflection does not support
 * nested TArray properties directly.
 */
USTRUCT()
struct COMPOSABLECAMERASYSTEM_API FComposableCameraNodeTemplatePinOverrides
{
	GENERATED_BODY()

	/** Sparse list of pin overrides for this node instance. Pins without an
	 *  entry here use their C++ declaration defaults (bAsPin =
	 *  Decl.bDefaultAsPin, DefaultValueString from the class). */
	UPROPERTY()
	TArray<FComposableCameraPinOverride> Overrides;
};

/**
 * Describes a data-pin connection between two nodes in a camera type asset.
 */
USTRUCT()
struct COMPOSABLECAMERASYSTEM_API FComposableCameraPinConnection
{
	GENERATED_BODY()

	/** Index of the source node in the camera type's NodeTemplates array. */
	UPROPERTY()
	int32 SourceNodeIndex = INDEX_NONE;

	/** Name of the output pin on the source node. */
	UPROPERTY()
	FName SourcePinName;

	/** Index of the target node in the camera type's NodeTemplates array. */
	UPROPERTY()
	int32 TargetNodeIndex = INDEX_NONE;

	/** Name of the input pin on the target node. */
	UPROPERTY()
	FName TargetPinName;
};

/**
 * Describes a wire between an internal-variable graph node (Get or Set) and
 * a camera/compute node pin.
 *
 * For a Get variable node: CameraNodeIndex/CameraPinName identify a node's
 * input pin that reads the variable's current value.
 *
 * For a Set variable node: CameraNodeIndex/CameraPinName identify a node's
 * output pin whose value is written into the variable when the source
 * node executes.
 *
 * The "Camera" prefix on CameraNodeIndex / CameraPinName is a legacy naming
 * artifact from before variable nodes could live on the compute chain. The
 * index space depends on this connection's bIsComputeChain flag:
 *
 *   - bIsComputeChain == false: CameraNodeIndex indexes NodeTemplates.
 *   - bIsComputeChain == true:  CameraNodeIndex indexes ComputeNodeTemplates.
 *
 * The field names are preserved for serialization compatibility. The owning
 * record also carries a legacy bIsComputeChain flag for old Set-variable
 * records, but Get-variable data wires are connection-scoped because one
 * variable can be read by both chains.
 */
USTRUCT()
struct COMPOSABLECAMERASYSTEM_API FComposableCameraVariablePinConnection
{
	GENERATED_BODY()

	/** Index of the node endpoint. Indexes NodeTemplates for camera-chain
	 *  records, ComputeNodeTemplates for compute-chain records. The "Camera"
	 *  prefix is preserved for serialization compatibility. */
	UPROPERTY()
	int32 CameraNodeIndex = INDEX_NONE;

	/** Name of the pin on the node (input pin for Get, output pin for Set). */
	UPROPERTY()
	FName CameraPinName;

	/** True when CameraNodeIndex indexes ComputeNodeTemplates; false when it
	 *  indexes NodeTemplates. Stored per connection so copied Get-variable nodes
	 *  can round-trip independently on the camera and BeginPlay compute chains. */
	UPROPERTY()
	bool bIsComputeChain = false;
};

/**
 * Editor-only record describing a single internal-variable graph node instance.
 *
 * Multiple Get/Set nodes can exist for the same underlying variable. Each is
 * tracked here by its own FGuid so the editor can round-trip the graph layout
 * and the wires connecting it to camera nodes.
 */
USTRUCT()
struct COMPOSABLECAMERASYSTEM_API FComposableCameraVariableNodeRecord
{
	GENERATED_BODY()

	/** Matches UEdGraphNode::NodeGuid of the variable graph node. */
	UPROPERTY()
	FGuid NodeGuid;

	/**
	 * Stable identity of the internal variable this node points at, matching
	 * FComposableCameraInternalVariable::VariableGuid on the owning type asset.
	 * Survives variable renames so Get/Set nodes follow the variable across edits.
	 * May be invalid on records saved before the GUID migration; in that case
	 * RebuildFromTypeAsset falls back to VariableName.
	 */
	UPROPERTY()
	FGuid VariableGuid;

	/** Legacy / display fallback name of the internal variable. Authoritative
	 *  identity is VariableGuid; this is kept as a debug aid and legacy fallback. */
	UPROPERTY()
	FName VariableName;

	/** True for Set nodes, false for Get nodes. */
	UPROPERTY()
	bool bIsSetter = false;

	/** Legacy node-level chain classification. For Set-variable nodes this
	 *  still identifies which exec chain owns the node. For Get-variable nodes,
	 *  each connection's bIsComputeChain is authoritative because a pure getter
	 *  can feed camera and compute consumers independently.
	 *
	 *  Defaults to false for migration safety: records saved before this field
	 *  existed deserialize as camera-chain, matching v1 behavior. */
	UPROPERTY()
	bool bIsComputeChain = false;

	/** Serialized position on the graph canvas. */
	UPROPERTY()
	FVector2D Position = FVector2D::ZeroVector;

	/** Node endpoints this variable node is wired to. The index space of
	 *  each connection's CameraNodeIndex depends on the connection's own
	 *  bIsComputeChain flag. */
	UPROPERTY()
	TArray<FComposableCameraVariablePinConnection> Connections;
};

/**
 * Tag for entries in the serialized execution chain.
 *
 * The execution chain is a linear sequence of operations the camera runs each
 * frame: camera nodes do the actual pose computation, and internal-variable
 * Set operations write scratch values between camera nodes. See
 * FComposableCameraExecEntry.
 */
UENUM()
enum class EComposableCameraExecEntryType : uint8
{
	/** Execute a camera node by its index in NodeTemplates. */
	CameraNode,

	/** Execute an internal-variable Set operation: copy the source camera node's
	 *  output pin into the internal variable identified by VariableGuid. */
	SetVariable
};

/**
 * A single entry in the full execution chain serialized by the editor.
 *
 * The editor walks the exec-pin chain in the visual graph (starting from the
 * Start sentinel's ExecOut) and records each step here. Camera-node steps
 * resolve to a camera node index; Set-variable steps capture the variable
 * GUID and the source pin that feeds the Set node's Value input.
 *
 * The runtime consumes the full chain to interleave camera node execution
 * with scratch-variable writes. The older TypeAsset::ExecutionOrder array is
 * kept as a camera-node-only projection of this chain for quick runtime
 * iteration and backwards compatibility with code paths that don't care about
 * Set operations.
 */
USTRUCT()
struct COMPOSABLECAMERASYSTEM_API FComposableCameraExecEntry
{
	GENERATED_BODY()

	/** Which kind of step this entry represents. */
	UPROPERTY()
	EComposableCameraExecEntryType EntryType = EComposableCameraExecEntryType::CameraNode;

	/** Node index in the chain-local template array. Used when EntryType ==
	 *  CameraNode (the node to execute), or as the source node of a
	 *  SetVariable entry (the node whose output pin feeds the Set node's
	 *  Value input).
	 *
	 *  For entries in FullExecChain: indexes NodeTemplates.
	 *  For entries in ComputeFullExecChain: indexes ComputeNodeTemplates.
	 *
	 *  The "Camera" prefix is preserved for serialization compatibility. */
	UPROPERTY()
	int32 CameraNodeIndex = INDEX_NONE;

	/** Stable identity of the internal variable being written. Used when
	 *  EntryType == SetVariable. */
	UPROPERTY()
	FGuid VariableGuid;

	/** Cached runtime name of the internal variable, matching
	 *  FComposableCameraInternalVariable::VariableName. The editor populates
	 *  this during SyncToTypeAsset by resolving VariableGuid against the type
	 *  asset's variable arrays. The runtime uses this to index into
	 *  FComposableCameraRuntimeDataBlock::InternalVariableOffsets without a
	 *  GUIDame lookup. Used when EntryType == SetVariable. */
	UPROPERTY()
	FName VariableName;

	/** Name of the output pin on CameraNodeIndex's node that supplies
	 *  the value being written into the variable. Used when EntryType ==
	 *  SetVariable. */
	UPROPERTY()
	FName SourcePinName;

	/** Byte size of the variable's data slot. Pre-computed from the variable's
	 *  EComposableCameraPinType at sync time so the runtime can do a raw memcpy
	 *  from the source output pin offset to the variable offset without a
	 *  type-dispatch. Used when EntryType == SetVariable.
	 *
	 *  Sentinel value `StructSlotSentinel` (`TNumericLimits<int32>::Max()`)
	 *  means "the variable is a non-POD struct. Its slot lives in the
	 *  RuntimeDataBlock's `StructSlots` pool, not the byte `Storage` pool, so
	 *  the byte-size value does not apply". The runtime SetVariable handler
	 *  passes this verbatim into `RuntimeDataBlock::CopySlot`, which dispatches
	 *  on the source/target offsets' storage class (struct slot vs Storage) and
	 *  ignores the size argument when the struct branch fires. The non-zero
	 *  sentinel is required so the existing `<= 0` early-out in the runtime
	 *  handler (which guards against editor failures setting size=0 for POD
	 *  variables) does not silently swallow non-POD struct writes. */
	UPROPERTY()
	int32 VariableSlotSize = 0;

	/** Sentinel for `VariableSlotSize` indicating "this variable lives in
	 *  RuntimeDataBlock::StructSlots". See field doc above. */
	static constexpr int32 StructSlotSentinel = TNumericLimits<int32>::Max();
};

/**
 * Key identifying a specific pin on a specific node instance.
 * Used as a map key in the RuntimeDataBlock for offset lookups.
 */
USTRUCT()
struct FComposableCameraPinKey
{
	GENERATED_BODY()

	UPROPERTY()
	int32 NodeIndex = INDEX_NONE;

	UPROPERTY()
	FName PinName;

	bool operator==(const FComposableCameraPinKey& Other) const
	{
		return NodeIndex == Other.NodeIndex && PinName == Other.PinName;
	}

	friend uint32 GetTypeHash(const FComposableCameraPinKey& Key)
	{
		return HashCombine(GetTypeHash(Key.NodeIndex), GetTypeHash(Key.PinName));
	}
};

/**
 * Whether a USTRUCT can be safely stored in the byte-array RuntimeDataBlock /
 * ParameterBlock storage path. "Safe" means: copying via FMemory::Memcpy /
 * FProperty::CopyCompleteValue produces a value identical to a deep copy, and
 * the struct's destruction is a no-op (no heap-owned storage to leak).
 *
 * Used by:
 *   - TryMapPropertyToPinType (auto-discovery of exposable struct UPROPERTYs)
 *   - GetPinTypeSize / GetPinTypeAlignment (RuntimeDataBlock layout)
 *   - SetParameterBlockValue (CustomThunk struct write filter)
 *   - ApplyStringValue (string -> struct import path)
 *   - UComposableCameraTypeAsset::Build (authoring-time validation)
 *
 * Decision logic. Strict whitelist, no reflection-layout heuristics:
 *   1. Engine math fast-path (FVector / FRotator / FTransform / etc.) ->safe.
 *   2. `STRUCT_IsPlainOldData` flag set->safe (caller opted in via
 *      `template<> struct TStructOpsTypeTraits<MyStruct> :
 *       TStructOpsTypeTraitsBase2<MyStruct> { enum { WithIsPlainOldData = true }; };`).
 *   3. Anything else->unsafe. Caller must route through `FInstancedStruct`
 *      / the typed slot pool.
 *
 * Why this is strict by design: a UPROPERTY-walk-and-validate approach (the
 * previous implementation) cannot see non-UPROPERTY members. UE allows native
 * C++ members in a USTRUCT body - `FString`, `TArray`, `UObject*`, custom
 * dtors. That the reflection iterator skips entirely. Such members appearing
 * as hidden leading / interior members shift visible-member offsets so the
 * compiler-generated layout looks "consistent" through reflection eyes; even
 * a trailing-padding sanity check fails to catch them when the hidden
 * member's size is exactly absorbed by alignment slack. Concrete bite:
 *
 *   USTRUCT()
 *   struct FBad
 *   {
 *       GENERATED_BODY();
 *       UPROPERTY() float A;
 *       FString Hidden;        // NOT UPROPERTY
 *   };
 *
 * The previous walk reported `A` at offset 0 (size 4); trailing-padding
 * sanity check passes if the struct's natural alignment (8 from FString)
 * absorbs the FString into the padded end. Function returned true. Downstream
 * memcpy'd FString bytes. Shallow copy + dtor leak + GC blindness.
 *
 * The strict whitelist removes the entire failure surface: any user-defined
 * USTRUCT that wants bytewise transport must explicitly opt into
 * `STRUCT_IsPlainOldData` (a one-line trait template), confirming under the
 * author's responsibility that the struct is genuinely POD. Everything else
 * routes through `FInstancedStruct`, which uses `CopyScriptStruct` (per-
 * property `operator=` via FProperty graph) and is correct for any USTRUCT
 * regardless of hidden members. The cost is one extra heap allocation per
 * non-POD slot at activation + slightly slower per-frame copy. Both
 * negligible for typical camera pin counts and dwarfed by the cost of
 * silently-corrupt memcpy crashes the strict path prevents.
 */
inline bool IsBytewiseSafeStruct(const UScriptStruct* Struct)
{
	if (!Struct)
	{
		return false;
	}
	// Fast-path the engine math structs and FFloatInterval. These are guaranteed
	// POD by construction; calling them out explicitly bypasses any quirk in the
	// flag-based check below (UE 5.6 LWC FVector / FRotator do not have
	// STRUCT_IsPlainOldData set even though they're trivially copyable, and the
	// TStructOpsTypeTraitsBase2<FVector>::WithCopy=true flag does not imply
	// "non-POD" -- it just means there's a custom operator= which happens to be
	// a memcpy-equivalent default).
	if (Struct == TBaseStructure<FVector>::Get()
		|| Struct == TBaseStructure<FVector2D>::Get()
		|| Struct == TBaseStructure<FVector4>::Get()
		|| Struct == TBaseStructure<FRotator>::Get()
		|| Struct == TBaseStructure<FTransform>::Get()
		|| Struct == TBaseStructure<FFloatInterval>::Get())
	{
		return true;
	}
	// Strict opt-in. Any user-defined USTRUCT that wants bytewise transport
	// must explicitly set WithIsPlainOldData=true in its TStructOpsTypeTraits
	// specialization. Reflection-based heuristics were removed because
	// non-UPROPERTY members are invisible to the walk and the previous
	// trailing-padding sanity check could not catch hidden interior or
	// leading members. FInstancedStruct handles every other USTRUCT
	// correctly via CopyScriptStruct.
	return (Struct->StructFlags & STRUCT_IsPlainOldData) != 0;
}

/**
 * Attempt to map an FProperty (from UClass reflection) to an EComposableCameraPinType.
 *
 * Returns true if the property type has a direct pin-type mapping. Returns false for
 * unsupported types (arrays, maps, sets, Instanced object properties, FString, etc.).
 *
 * For Enum-typed properties (`FEnumProperty` for `enum class`, or `FByteProperty` whose
 * IntPropertyEnum is set), OutEnumType receives the backing UEnum*. Generic Struct
 * properties pass iff `IsBytewiseSafeStruct` returns true (POD-like) -- non-POD
 * structs are rejected until typed storage lands. Both metadata outputs are cleared
 * on entry.
 *
 * Used by DeclareSubobjectPins to auto-discover exposable sub-properties of an
 * Instanced UObject, and by ApplySubobjectPinValues to dispatch typed reads.
 */
inline bool TryMapPropertyToPinType(const FProperty* Property, EComposableCameraPinType& OutPinType, UScriptStruct*& OutStructType, UEnum*& OutEnumType, UFunction** OutSignatureFunction = nullptr)
{
	OutStructType = nullptr;
	OutEnumType = nullptr;
	if (OutSignatureFunction) { *OutSignatureFunction = nullptr; }

	if (Property->IsA<FBoolProperty>())       { OutPinType = EComposableCameraPinType::Bool;      return true; }
	if (Property->IsA<FIntProperty>())         { OutPinType = EComposableCameraPinType::Int32;     return true; }
	if (Property->IsA<FFloatProperty>())       { OutPinType = EComposableCameraPinType::Float;     return true; }
	if (Property->IsA<FDoubleProperty>())      { OutPinType = EComposableCameraPinType::Double;    return true; }
	if (Property->IsA<FNameProperty>())        { OutPinType = EComposableCameraPinType::Name;      return true; }

	// C++ `enum class` properties. Reflected as FEnumProperty wrapping a numeric
	// underlying property. The enum object tells us the display names + value set;
	// the underlying property tells us the actual storage width (uint8 / int32 /
	// int64). We normalize to int64 in the data block and narrow-cast on write.
	if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
	{
		OutPinType = EComposableCameraPinType::Enum;
		OutEnumType = EnumProp->GetEnum();
		return OutEnumType != nullptr;
	}

	// Legacy `UENUM() TEnumAsByte<E>`-style properties. Reflected as FByteProperty
	// with an attached UEnum*. Plain FByteProperty without an enum is rejected (we
	// don't expose raw byte pins).
	if (const FByteProperty* ByteProp = CastField<FByteProperty>(Property))
	{
		if (UEnum* Enum = ByteProp->GetIntPropertyEnum())
		{
			OutPinType = EComposableCameraPinType::Enum;
			OutEnumType = Enum;
			return true;
		}
		return false;
	}

	if (const FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		UScriptStruct* Struct = StructProp->Struct;
		if      (Struct == TBaseStructure<FVector2D>::Get())  { OutPinType = EComposableCameraPinType::Vector2D;  return true; }
		else if (Struct == TBaseStructure<FVector>::Get())    { OutPinType = EComposableCameraPinType::Vector3D;  return true; }
		else if (Struct == TBaseStructure<FVector4>::Get())   { OutPinType = EComposableCameraPinType::Vector4;   return true; }
		else if (Struct == TBaseStructure<FRotator>::Get())   { OutPinType = EComposableCameraPinType::Rotator;   return true; }
		else if (Struct == TBaseStructure<FTransform>::Get()) { OutPinType = EComposableCameraPinType::Transform; return true; }
		// Generic struct: any USTRUCT is acceptable. POD structs land in the
		// byte-array Storage path (memcpy via ReadValue<T>); non-POD structs
		// (FString / FText / TArray / object refs / delegates inside the
		// struct) land in the typed FInstancedStruct slot pool with proper
		// ctor / dtor / GC traversal -- the dispatch happens at the storage
		// layer (RuntimeDataBlock::ReadValue<T> + WriteValue<T>'s
		// `if constexpr` branch) and the BuildRuntimeDataLayout AllocateSlot
		// helper.
		if (Struct)
		{
			OutPinType = EComposableCameraPinType::Struct;
			OutStructType = Struct;
			return true;
		}
		return false;
	}

	if (const FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Property))
	{
		// Instanced subobjects are not pin-mappable (they carry lifecycle/state).
		if (Property->HasAnyPropertyFlags(CPF_InstancedReference))
		{
			return false;
		}
		// Soft / weak / lazy object properties have a different memory layout than
		// raw UObject*/AActor*, so they are NOT safe to drive through the Actor /
		// Object pin types. Plain object/TObjectPtr refs and class refs are
		// accepted because the auto-resolver writes through FObjectPropertyBase.
		// In UE 5.6, TObjectPtr-backed fields are represented through
		// FObjectProperty; the old FObjectPtrProperty name is deprecated.
		if (!Property->IsA<FObjectProperty>() && !Property->IsA<FClassProperty>())
		{
			return false;
		}
		if (ObjProp->PropertyClass && ObjProp->PropertyClass->IsChildOf(AActor::StaticClass()))
		{
			OutPinType = EComposableCameraPinType::Actor;
			return true;
		}
		OutPinType = EComposableCameraPinType::Object;
		return true;
	}

	// Single-cast dynamic delegates (DECLARE_DYNAMIC_DELEGATE*). Only accepted when
	// the caller opts in by passing OutSignatureFunction. Auto-discovery paths
	// (subobject pins, per-frame auto-resolve) default to nullptr, causing delegates
	// to be silently skipped. This is intentional: delegates are bound once at
	// activation, not per-frame, and subobject delegates are an unusual pattern
	// better left to explicit GetPinDeclarations overrides.
	if (const FDelegateProperty* DelegateProp = CastField<FDelegateProperty>(Property))
	{
		if (!OutSignatureFunction) { return false; }
		OutPinType = EComposableCameraPinType::Delegate;
		*OutSignatureFunction = DelegateProp->SignatureFunction;
		return *OutSignatureFunction != nullptr;
	}

	// FStrProperty / FText / arrays / maps / sets are intentionally rejected. The
	// data block is POD-only (memcpy transport), and these carry heap-owned storage
	// or non-trivial layout.
	return false;
}

/**
 * Returns the size in bytes of a given pin type.
 * For Struct types, returns 0. Caller must query StructType->GetStructureSize().
 */
inline int32 GetPinTypeSize(EComposableCameraPinType PinType, UScriptStruct* StructType = nullptr)
{
	switch (PinType)
	{
	case EComposableCameraPinType::Bool:      return sizeof(bool);
	case EComposableCameraPinType::Int32:     return sizeof(int32);
	case EComposableCameraPinType::Float:     return sizeof(float);
	case EComposableCameraPinType::Double:    return sizeof(double);
	case EComposableCameraPinType::Vector2D:  return sizeof(FVector2D);
	case EComposableCameraPinType::Vector3D:  return sizeof(FVector);
	case EComposableCameraPinType::Vector4:   return sizeof(FVector4);
	case EComposableCameraPinType::Rotator:   return sizeof(FRotator);
	case EComposableCameraPinType::Transform: return sizeof(FTransform);
	case EComposableCameraPinType::Actor:     return sizeof(AActor*);
	case EComposableCameraPinType::Object:    return sizeof(UObject*);
	case EComposableCameraPinType::Name:      return sizeof(FName);
	case EComposableCameraPinType::Enum:      return sizeof(int64); // always normalized to int64 in the block
	case EComposableCameraPinType::Delegate:  return 0; // delegates don't live in the data block
	case EComposableCameraPinType::Struct:
		return IsBytewiseSafeStruct(StructType) ? StructType->GetStructureSize() : 0;
	default:
		return 0;
	}
}

/**
 * Resolve the value to store in `FComposableCameraExecEntry::VariableSlotSize`
 * for a SetVariable exec entry whose target variable has the given (PinType,
 * StructType).
 *
 * For POD pin types (including bytewise-safe USTRUCTs like FVector / FRotator
 * / FTransform / user POD structs), returns the byte size to copy at runtime.
 *
 * For non-POD struct pin types, returns `FComposableCameraExecEntry::StructSlotSentinel`.
 * Returning 0 here would cause the runtime SetVariable handler's `<= 0` early
 * out to silently skip every Set on a non-POD struct variable. The sentinel
 * passes that gate; the runtime then routes to `RuntimeDataBlock::CopySlot`
 * which dispatches on storage class via `IsStructSlotOffset` and ignores the
 * byte-size argument when the struct branch fires.
 *
 * Used by editor-side exec-chain construction (`UComposableCameraNodeGraph::
 * BuildVariableLookup`) so the same dispatch rule lands on every SetVariable
 * entry regardless of which graph phase recorded it.
 */
inline int32 GetVariableSlotSize(EComposableCameraPinType PinType, UScriptStruct* StructType = nullptr)
{
	if (PinType == EComposableCameraPinType::Struct
		&& StructType != nullptr
		&& !IsBytewiseSafeStruct(StructType))
	{
		return FComposableCameraExecEntry::StructSlotSentinel;
	}
	return GetPinTypeSize(PinType, StructType);
}

/**
 * Returns the alignment requirement of a given pin type.
 */
inline int32 GetPinTypeAlignment(EComposableCameraPinType PinType, UScriptStruct* StructType = nullptr)
{
	switch (PinType)
	{
	case EComposableCameraPinType::Bool:      return alignof(bool);
	case EComposableCameraPinType::Int32:     return alignof(int32);
	case EComposableCameraPinType::Float:     return alignof(float);
	case EComposableCameraPinType::Double:    return alignof(double);
	case EComposableCameraPinType::Vector2D:  return alignof(FVector2D);
	case EComposableCameraPinType::Vector3D:  return alignof(FVector);
	case EComposableCameraPinType::Vector4:   return alignof(FVector4);
	case EComposableCameraPinType::Rotator:   return alignof(FRotator);
	case EComposableCameraPinType::Transform: return alignof(FTransform);
	case EComposableCameraPinType::Actor:     return alignof(AActor*);
	case EComposableCameraPinType::Object:    return alignof(UObject*);
	case EComposableCameraPinType::Name:      return alignof(FName);
	case EComposableCameraPinType::Enum:      return alignof(int64);
	case EComposableCameraPinType::Delegate:  return 1; // no data block allocation
	case EComposableCameraPinType::Struct:
		return IsBytewiseSafeStruct(StructType) ? StructType->GetMinAlignment() : 1;
	default:
		return 1;
	}
}
