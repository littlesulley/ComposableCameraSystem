// Copyright Sulley. All rights reserved.

#include "DataAssets/ComposableCameraTypeAsset.h"

#include "ComposableCameraSystemModule.h"
#include "Core/ComposableCameraParameterBlock.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "Nodes/ComposableCameraComputeNodeBase.h"
#include "Transitions/ComposableCameraTransitionBase.h"
#include "UObject/UnrealType.h"

namespace ComposableCameraTypeAssetPrivate
{
	/**
	 * Parse a variable's InitialValueString into a destination byte span using
	 * the same parser the DataTable row activation path uses.
	 *
	 * We route through FComposableCameraParameterBlock::ApplyStringValue +
	 * CopyRawTo rather than duplicating the type-dispatching parser: this
	 * guarantees that any value authored in a DataTable row, a K2 node default,
	 * or an InitialValueString is interpreted identically.
	 *
	 * No-ops on empty InitialValueString, unsupported types, or destination
	 * size mismatch. Never partially writes — either the whole typed value
	 * lands in Dest or nothing does.
	 */
	static void ApplyInitialValueToSlot(
		const FComposableCameraInternalVariable& Var,
		uint8* Dest,
		int32 DestSize,
		const TCHAR* OwnerName)
	{
		if (Var.InitialValueString.IsEmpty() || Dest == nullptr || DestSize <= 0)
		{
			return;
		}

		FComposableCameraParameterBlock Scratch;
		FString ParseError;
		const bool bOk = FComposableCameraParameterBlock::ApplyStringValue(
			Scratch,
			Var.VariableName,
			Var.VariableType,
			Var.StructType,
			Var.EnumType,
			Var.InitialValueString,
			&ParseError);

		if (!bOk)
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("ApplyInitialValueToSlot: [%s] variable '%s' initial value parse failed (%s). Slot left zero-initialized."),
				OwnerName, *Var.VariableName.ToString(), *ParseError);
			return;
		}

		// CopyRawTo is a no-op on size mismatch, so a stale InitialValueString
		// from a previous type never corrupts an unrelated slot.
		Scratch.CopyRawTo(Var.VariableName, Dest, DestSize);
	}
}

FString UComposableCameraTypeAsset::GetExposedParameterDefaultValue(
	const FComposableCameraExposedParameter& Param) const
{
	// Validate target node index.
	if (!NodeTemplates.IsValidIndex(Param.TargetNodeIndex))
	{
		return FString();
	}

	const UComposableCameraCameraNodeBase* Node = NodeTemplates[Param.TargetNodeIndex];
	if (!Node)
	{
		return FString();
	}

	// Check per-instance override first.
	if (NodePinOverrides.IsValidIndex(Param.TargetNodeIndex))
	{
		for (const FComposableCameraPinOverride& Override : NodePinOverrides[Param.TargetNodeIndex].Overrides)
		{
			if (Override.PinName == Param.TargetPinName && Override.bHasDefaultOverride)
			{
				return Override.DefaultValueOverride;
			}
		}
	}

	// Fall back to the class-level pin declaration default.
	TArray<FComposableCameraNodePinDeclaration> Pins;
	Node->GatherAllPinDeclarations(Pins);
	for (const FComposableCameraNodePinDeclaration& Decl : Pins)
	{
		if (Decl.PinName == Param.TargetPinName)
		{
			return Decl.DefaultValueString;
		}
	}

	return FString();
}

FComposableCameraRuntimeDataBlock UComposableCameraTypeAsset::BuildRuntimeDataLayout() const
{
	FComposableCameraRuntimeDataBlock DataBlock;

	// Phase 1: Gather all output pin declarations from all nodes.
	// Each output pin gets a slot in storage.
	int32 CurrentOffset = 0;

	auto AlignOffset = [&CurrentOffset](int32 Alignment)
	{
		if (Alignment > 1)
		{
			CurrentOffset = Align(CurrentOffset, Alignment);
		}
	};

	// --- Output pin slots ---
	for (int32 NodeIdx = 0; NodeIdx < NodeTemplates.Num(); ++NodeIdx)
	{
		const UComposableCameraCameraNodeBase* Node = NodeTemplates[NodeIdx];
		if (!Node)
		{
			continue;
		}

		TArray<FComposableCameraNodePinDeclaration> Pins;
		Node->GatherAllPinDeclarations(Pins);

		for (const FComposableCameraNodePinDeclaration& Pin : Pins)
		{
			if (Pin.Direction != EComposableCameraPinDirection::Output)
			{
				continue;
			}

			const int32 Size = GetPinTypeSize(Pin.PinType, Pin.StructType);
			const int32 Align = GetPinTypeAlignment(Pin.PinType, Pin.StructType);

			if (Size <= 0)
			{
				UE_LOG(LogComposableCameraSystem, Warning,
					TEXT("BuildRuntimeDataLayout: Output pin '%s' on node %d has zero size. Skipping."),
					*Pin.PinName.ToString(), NodeIdx);
				continue;
			}

			AlignOffset(Align);

			FComposableCameraPinKey Key;
			Key.NodeIndex = NodeIdx;
			Key.PinName = Pin.PinName;
			DataBlock.OutputPinOffsets.Add(Key, CurrentOffset);

			CurrentOffset += Size;
		}
	}

	// --- Compute node output pin slots ---
	//
	// Compute nodes share the same OutputPinOffsets map as camera nodes, but
	// live in an offset index space: the runtime NodeIndex used for each
	// compute node's FComposableCameraPinKey is NodeTemplates.Num() + ComputeIdx.
	// This keeps output pin keys unique across the two chains without teaching
	// the key struct to disambiguate which chain a node belongs to, and it
	// matches the duplication order the activation path uses in
	// OnTypeAssetCameraConstructed (camera nodes first, compute nodes after).
	const int32 ComputeNodeIndexBase = NodeTemplates.Num();
	for (int32 ComputeIdx = 0; ComputeIdx < ComputeNodeTemplates.Num(); ++ComputeIdx)
	{
		const UComposableCameraComputeNodeBase* Node = ComputeNodeTemplates[ComputeIdx];
		if (!Node)
		{
			continue;
		}

		TArray<FComposableCameraNodePinDeclaration> Pins;
		Node->GatherAllPinDeclarations(Pins);

		for (const FComposableCameraNodePinDeclaration& Pin : Pins)
		{
			if (Pin.Direction != EComposableCameraPinDirection::Output)
			{
				continue;
			}

			const int32 Size = GetPinTypeSize(Pin.PinType, Pin.StructType);
			const int32 Align = GetPinTypeAlignment(Pin.PinType, Pin.StructType);

			if (Size <= 0)
			{
				UE_LOG(LogComposableCameraSystem, Warning,
					TEXT("BuildRuntimeDataLayout: Output pin '%s' on compute node %d has zero size. Skipping."),
					*Pin.PinName.ToString(), ComputeIdx);
				continue;
			}

			AlignOffset(Align);

			FComposableCameraPinKey Key;
			Key.NodeIndex = ComputeNodeIndexBase + ComputeIdx;
			Key.PinName = Pin.PinName;
			DataBlock.OutputPinOffsets.Add(Key, CurrentOffset);

			CurrentOffset += Size;
		}
	}

	// --- Exposed parameter slots ---
	for (const FComposableCameraExposedParameter& Param : ExposedParameters)
	{
		const int32 Size = GetPinTypeSize(Param.PinType, Param.StructType);
		const int32 Align = GetPinTypeAlignment(Param.PinType, Param.StructType);

		if (Size <= 0)
		{
			continue;
		}

		AlignOffset(Align);
		DataBlock.ExposedParameterOffsets.Add(Param.ParameterName, CurrentOffset);
		CurrentOffset += Size;
	}

	// --- Per-instance default override slots ---
	//
	// For each input pin on each node template that has an authored
	// FComposableCameraPinOverride::DefaultValueOverride (bHasDefaultOverride == true),
	// allocate a storage slot. The slot will be seeded later in this function,
	// once Storage has been allocated, by parsing the override string through
	// the shared parser and memcpy'ing the typed bytes into the slot.
	//
	// This is the 3rd priority in TryResolveInputPin, ranking below wired
	// connections and exposed parameters but above the node's class-level
	// fallback. The override data lives on the parallel NodePinOverrides array
	// (see EditorDesignDoc §4 "Per-Instance Pin Overrides"). Legacy assets saved
	// before NodePinOverrides existed have an empty array and are silently
	// skipped — existing nodes keep working via their UPROPERTY fallback path
	// (e.g. UComposableCameraFieldOfViewNode reads FieldOfView directly when
	// GetInputPinValue returns zero).
	//
	// Slots are allocated even for pins that are also wired or exposed; those
	// slots are harmless waste (~ a few bytes per node) and the priority
	// ordering in TryResolveInputPin ensures they're never read. Keeping the
	// allocation loop unconditional also keeps BuildRuntimeDataLayout
	// independent of which connections exist at author time.
	const bool bHasNodePinOverrides = (NodePinOverrides.Num() == NodeTemplates.Num());
	if (bHasNodePinOverrides)
	{
		for (int32 NodeIdx = 0; NodeIdx < NodeTemplates.Num(); ++NodeIdx)
		{
			const UComposableCameraCameraNodeBase* Node = NodeTemplates[NodeIdx];
			if (!Node)
			{
				continue;
			}

			const TArray<FComposableCameraPinOverride>& Overrides =
				NodePinOverrides[NodeIdx].Overrides;
			if (Overrides.Num() == 0)
			{
				continue;
			}

			TArray<FComposableCameraNodePinDeclaration> Pins;
			Node->GatherAllPinDeclarations(Pins);

			for (const FComposableCameraPinOverride& Override : Overrides)
			{
				if (!Override.bHasDefaultOverride)
				{
					continue;
				}

				const FComposableCameraNodePinDeclaration* Decl = Pins.FindByPredicate(
					[&](const FComposableCameraNodePinDeclaration& P)
					{
						return P.PinName == Override.PinName
							&& P.Direction == EComposableCameraPinDirection::Input;
					});
				if (!Decl)
				{
					// Override references a pin that no longer exists on the node
					// class (e.g. a C++ pin was renamed or removed). Skip silently
					// — the stale entry is harmless, and the next sync will drop it
					// via the normal round-trip path.
					continue;
				}

				const int32 Size = GetPinTypeSize(Decl->PinType, Decl->StructType);
				const int32 Align = GetPinTypeAlignment(Decl->PinType, Decl->StructType);
				if (Size <= 0)
				{
					continue;
				}

				AlignOffset(Align);
				FComposableCameraPinKey Key;
				Key.NodeIndex = NodeIdx;
				Key.PinName = Override.PinName;
				DataBlock.DefaultValueOffsets.Add(Key, CurrentOffset);
				CurrentOffset += Size;
			}
		}
	}

	// --- Compute node per-instance default override slots ---
	//
	// Mirrors the camera-node override slot allocation above, against the
	// parallel ComputeNodePinOverrides / ComputeNodeTemplates arrays and using
	// the ComputeNodeIndexBase offset index space so FComposableCameraPinKey
	// stays unique across chains.
	const bool bHasComputeNodePinOverrides =
		(ComputeNodePinOverrides.Num() == ComputeNodeTemplates.Num());
	if (bHasComputeNodePinOverrides)
	{
		for (int32 ComputeIdx = 0; ComputeIdx < ComputeNodeTemplates.Num(); ++ComputeIdx)
		{
			const UComposableCameraComputeNodeBase* Node = ComputeNodeTemplates[ComputeIdx];
			if (!Node)
			{
				continue;
			}

			const TArray<FComposableCameraPinOverride>& Overrides =
				ComputeNodePinOverrides[ComputeIdx].Overrides;
			if (Overrides.Num() == 0)
			{
				continue;
			}

			TArray<FComposableCameraNodePinDeclaration> Pins;
			Node->GatherAllPinDeclarations(Pins);

			for (const FComposableCameraPinOverride& Override : Overrides)
			{
				if (!Override.bHasDefaultOverride)
				{
					continue;
				}

				const FComposableCameraNodePinDeclaration* Decl = Pins.FindByPredicate(
					[&](const FComposableCameraNodePinDeclaration& P)
					{
						return P.PinName == Override.PinName
							&& P.Direction == EComposableCameraPinDirection::Input;
					});
				if (!Decl)
				{
					continue;
				}

				const int32 Size = GetPinTypeSize(Decl->PinType, Decl->StructType);
				const int32 Align = GetPinTypeAlignment(Decl->PinType, Decl->StructType);
				if (Size <= 0)
				{
					continue;
				}

				AlignOffset(Align);
				FComposableCameraPinKey Key;
				Key.NodeIndex = ComputeNodeIndexBase + ComputeIdx;
				Key.PinName = Override.PinName;
				DataBlock.DefaultValueOffsets.Add(Key, CurrentOffset);
				CurrentOffset += Size;
			}
		}
	}

	// --- Internal variable slots ---
	for (const FComposableCameraInternalVariable& Var : InternalVariables)
	{
		const int32 Size = GetPinTypeSize(Var.VariableType, Var.StructType);
		const int32 Align = GetPinTypeAlignment(Var.VariableType, Var.StructType);

		if (Size <= 0)
		{
			continue;
		}

		AlignOffset(Align);
		DataBlock.InternalVariableOffsets.Add(Var.VariableName, CurrentOffset);
		CurrentOffset += Size;
	}

	// --- Exposed variable slots ---
	//
	// ExposedVariables share the InternalVariableOffsets map. From the runtime's
	// point of view (Get/Set variable graph nodes, parameter block application,
	// per-frame reads) there is no difference between "internal" and "exposed"
	// — the only distinction is that ApplyParameterBlock consults the caller's
	// block for exposed variables while internal variables get their initial
	// value purely from InitialValueString. Unifying the offset map keeps node
	// read/write code oblivious to the split.
	//
	// Name uniqueness across InternalVariables / ExposedVariables / ExposedParameters
	// is enforced in Build() above, but we still defensively skip on collision
	// here so a stale asset load with a duplicate name doesn't silently corrupt
	// the layout.
	for (const FComposableCameraInternalVariable& Var : ExposedVariables)
	{
		const int32 Size = GetPinTypeSize(Var.VariableType, Var.StructType);
		const int32 Align = GetPinTypeAlignment(Var.VariableType, Var.StructType);

		if (Size <= 0)
		{
			continue;
		}

		if (DataBlock.InternalVariableOffsets.Contains(Var.VariableName))
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("BuildRuntimeDataLayout: [%s] exposed variable '%s' collides with an existing internal variable of the same name. Skipping — fix the duplicate in the type asset."),
				*GetName(), *Var.VariableName.ToString());
			continue;
		}

		AlignOffset(Align);
		DataBlock.InternalVariableOffsets.Add(Var.VariableName, CurrentOffset);
		CurrentOffset += Size;
	}

	// Allocate storage.
	DataBlock.TotalSize = CurrentOffset;
	DataBlock.Storage.SetNumZeroed(CurrentOffset);

	// --- Seed per-instance default override slots ---
	//
	// Now that Storage exists, walk NodePinOverrides a second time and parse
	// each override string through FComposableCameraParameterBlock::ApplyStringValue
	// (the same parser the DataTable row path and internal-variable initial-value
	// path use) and memcpy the typed bytes into the slot we allocated above.
	//
	// Parse failures log once per activation and leave the slot zero-initialized
	// (the initial state after SetNumZeroed) so a stale string from a previous
	// type can't corrupt an unrelated slot.
	if (bHasNodePinOverrides)
	{
		for (int32 NodeIdx = 0; NodeIdx < NodeTemplates.Num(); ++NodeIdx)
		{
			const UComposableCameraCameraNodeBase* Node = NodeTemplates[NodeIdx];
			if (!Node)
			{
				continue;
			}

			const TArray<FComposableCameraPinOverride>& Overrides =
				NodePinOverrides[NodeIdx].Overrides;
			if (Overrides.Num() == 0)
			{
				continue;
			}

			TArray<FComposableCameraNodePinDeclaration> Pins;
			Node->GatherAllPinDeclarations(Pins);

			for (const FComposableCameraPinOverride& Override : Overrides)
			{
				if (!Override.bHasDefaultOverride)
				{
					continue;
				}

				FComposableCameraPinKey Key;
				Key.NodeIndex = NodeIdx;
				Key.PinName = Override.PinName;
				const int32* Offset = DataBlock.DefaultValueOffsets.Find(Key);
				if (!Offset)
				{
					// Slot wasn't allocated (declaration not found, zero size);
					// nothing to seed.
					continue;
				}

				const FComposableCameraNodePinDeclaration* Decl = Pins.FindByPredicate(
					[&](const FComposableCameraNodePinDeclaration& P)
					{
						return P.PinName == Override.PinName
							&& P.Direction == EComposableCameraPinDirection::Input;
					});
				if (!Decl)
				{
					// Can't happen in practice because slot allocation above
					// uses the same predicate, but guard anyway so a future
					// refactor of either loop can't produce an out-of-bounds
					// write if they drift apart.
					continue;
				}

				const int32 Size = GetPinTypeSize(Decl->PinType, Decl->StructType);
				if (Size <= 0)
				{
					continue;
				}

				FComposableCameraParameterBlock Scratch;
				FString ParseError;
				const bool bOk = FComposableCameraParameterBlock::ApplyStringValue(
					Scratch,
					Override.PinName,
					Decl->PinType,
					Decl->StructType,
					Decl->EnumType,
					Override.DefaultValueOverride,
					&ParseError);
				if (!bOk)
				{
					UE_LOG(LogComposableCameraSystem, Warning,
						TEXT("BuildRuntimeDataLayout: [%s] per-instance default for pin '%s' on node %d failed to parse (%s). Slot left zero-initialized."),
						*GetName(), *Override.PinName.ToString(), NodeIdx, *ParseError);
					continue;
				}

				// CopyRawTo is a no-op on size mismatch, so a stale override
				// string from a previous type never corrupts the slot.
				Scratch.CopyRawTo(
					Override.PinName,
					DataBlock.Storage.GetData() + *Offset,
					Size);
			}
		}
	}

	// --- Seed compute node per-instance default override slots ---
	//
	// Mirrors the camera-node seed loop above. Walks ComputeNodePinOverrides
	// with the ComputeNodeIndexBase offset index space so the pin key lookup
	// matches the slots allocated earlier in the layout pass.
	if (bHasComputeNodePinOverrides)
	{
		for (int32 ComputeIdx = 0; ComputeIdx < ComputeNodeTemplates.Num(); ++ComputeIdx)
		{
			const UComposableCameraComputeNodeBase* Node = ComputeNodeTemplates[ComputeIdx];
			if (!Node)
			{
				continue;
			}

			const TArray<FComposableCameraPinOverride>& Overrides =
				ComputeNodePinOverrides[ComputeIdx].Overrides;
			if (Overrides.Num() == 0)
			{
				continue;
			}

			TArray<FComposableCameraNodePinDeclaration> Pins;
			Node->GatherAllPinDeclarations(Pins);

			for (const FComposableCameraPinOverride& Override : Overrides)
			{
				if (!Override.bHasDefaultOverride)
				{
					continue;
				}

				FComposableCameraPinKey Key;
				Key.NodeIndex = ComputeNodeIndexBase + ComputeIdx;
				Key.PinName = Override.PinName;
				const int32* Offset = DataBlock.DefaultValueOffsets.Find(Key);
				if (!Offset)
				{
					continue;
				}

				const FComposableCameraNodePinDeclaration* Decl = Pins.FindByPredicate(
					[&](const FComposableCameraNodePinDeclaration& P)
					{
						return P.PinName == Override.PinName
							&& P.Direction == EComposableCameraPinDirection::Input;
					});
				if (!Decl)
				{
					continue;
				}

				const int32 Size = GetPinTypeSize(Decl->PinType, Decl->StructType);
				if (Size <= 0)
				{
					continue;
				}

				FComposableCameraParameterBlock Scratch;
				FString ParseError;
				const bool bOk = FComposableCameraParameterBlock::ApplyStringValue(
					Scratch,
					Override.PinName,
					Decl->PinType,
					Decl->StructType,
					Decl->EnumType,
					Override.DefaultValueOverride,
					&ParseError);
				if (!bOk)
				{
					UE_LOG(LogComposableCameraSystem, Warning,
						TEXT("BuildRuntimeDataLayout: [%s] per-instance default for pin '%s' on compute node %d failed to parse (%s). Slot left zero-initialized."),
						*GetName(), *Override.PinName.ToString(), ComputeIdx, *ParseError);
					continue;
				}

				Scratch.CopyRawTo(
					Override.PinName,
					DataBlock.Storage.GetData() + *Offset,
					Size);
			}
		}
	}

	// Phase 2: Build the connection and exposure mappings for input pins.

	// Wired connections: input pin → source output pin offset
	for (const FComposableCameraPinConnection& Conn : PinConnections)
	{
		FComposableCameraPinKey SourceKey;
		SourceKey.NodeIndex = Conn.SourceNodeIndex;
		SourceKey.PinName = Conn.SourcePinName;

		const int32* SourceOffset = DataBlock.OutputPinOffsets.Find(SourceKey);
		if (!SourceOffset)
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("BuildRuntimeDataLayout: Connection source pin '%s' on node %d not found."),
				*Conn.SourcePinName.ToString(), Conn.SourceNodeIndex);
			continue;
		}

		FComposableCameraPinKey TargetKey;
		TargetKey.NodeIndex = Conn.TargetNodeIndex;
		TargetKey.PinName = Conn.TargetPinName;
		DataBlock.InputPinSourceOffsets.Add(TargetKey, *SourceOffset);
	}

	// Compute-chain wired connections. ComputePinConnections stores indices
	// in the compute node index space (0..ComputeNodeTemplates.Num()-1) on
	// both endpoints; the schema prevents cross-chain data wires so we never
	// see a camera-node endpoint on either side of one of these. Offset both
	// endpoints into the runtime NodeIndex space (NodeTemplates.Num() + idx)
	// so the lookups hit the compute output pin slots allocated above.
	for (const FComposableCameraPinConnection& Conn : ComputePinConnections)
	{
		FComposableCameraPinKey SourceKey;
		SourceKey.NodeIndex = ComputeNodeIndexBase + Conn.SourceNodeIndex;
		SourceKey.PinName = Conn.SourcePinName;

		const int32* SourceOffset = DataBlock.OutputPinOffsets.Find(SourceKey);
		if (!SourceOffset)
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("BuildRuntimeDataLayout: Compute connection source pin '%s' on compute node %d not found."),
				*Conn.SourcePinName.ToString(), Conn.SourceNodeIndex);
			continue;
		}

		FComposableCameraPinKey TargetKey;
		TargetKey.NodeIndex = ComputeNodeIndexBase + Conn.TargetNodeIndex;
		TargetKey.PinName = Conn.TargetPinName;
		DataBlock.InputPinSourceOffsets.Add(TargetKey, *SourceOffset);
	}

	// Exposed parameters: input pin → parameter slot offset
	for (const FComposableCameraExposedParameter& Param : ExposedParameters)
	{
		const int32* ParamOffset = DataBlock.ExposedParameterOffsets.Find(Param.ParameterName);
		if (!ParamOffset)
		{
			continue;
		}

		FComposableCameraPinKey TargetKey;
		TargetKey.NodeIndex = Param.TargetNodeIndex;
		TargetKey.PinName = Param.TargetPinName;
		DataBlock.ExposedInputPinOffsets.Add(TargetKey, *ParamOffset);
	}

	// Variable getter (Get) nodes: consumer input pin → variable storage offset.
	//
	// Get variable graph nodes are pure editor constructs with no runtime
	// identity — they don't appear in NodeTemplates or ComputeNodeTemplates
	// and never execute.  When a consumer node's input pin is wired to a Get
	// node's output in the graph editor, the consumer should read directly
	// from the variable's InternalVariableOffsets slot (the same slot that
	// ApplyParameterBlock / initial-value seeding writes to).  The getter
	// node is transparent at runtime.
	//
	// Set variable nodes don't need this treatment: they are handled via
	// SetVariable exec chain entries that copy a source node's output pin
	// value into the variable's slot at execution time.
	for (const FComposableCameraVariableNodeRecord& Record : VariableNodes)
	{
		if (Record.bIsSetter)
		{
			continue;
		}

		// Resolve the variable name for offset lookup.  VariableGuid is the
		// authoritative identity, but InternalVariableOffsets is keyed by
		// VariableName.  Walk InternalVariables + ExposedVariables to find
		// the canonical name via GUID, falling back to Record.VariableName.
		FName ResolvedName = Record.VariableName;
		if (Record.VariableGuid.IsValid())
		{
			auto FindByGuid = [&](const TArray<FComposableCameraInternalVariable>& Vars) -> FName
			{
				for (const FComposableCameraInternalVariable& V : Vars)
				{
					if (V.VariableGuid == Record.VariableGuid)
					{
						return V.VariableName;
					}
				}
				return NAME_None;
			};

			FName Found = FindByGuid(InternalVariables);
			if (Found.IsNone())
			{
				Found = FindByGuid(ExposedVariables);
			}
			if (!Found.IsNone())
			{
				ResolvedName = Found;
			}
		}

		const int32* VarOffset = DataBlock.InternalVariableOffsets.Find(ResolvedName);
		if (!VarOffset)
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("BuildRuntimeDataLayout: [%s] Get variable node for '%s' — no InternalVariableOffset found. Connections from this getter will not resolve."),
				*GetName(), *ResolvedName.ToString());
			continue;
		}

		// For each consumer connection, map the consumer's input pin
		// directly to the variable's storage slot.
		for (const FComposableCameraVariablePinConnection& Conn : Record.Connections)
		{
			if (Conn.CameraNodeIndex == INDEX_NONE)
			{
				continue;
			}

			const int32 RuntimeNodeIndex = Record.bIsComputeChain
				? (ComputeNodeIndexBase + Conn.CameraNodeIndex)
				: Conn.CameraNodeIndex;

			FComposableCameraPinKey TargetKey;
			TargetKey.NodeIndex = RuntimeNodeIndex;
			TargetKey.PinName = Conn.CameraPinName;

			// If this consumer pin is also covered by an ExposedParameter,
			// the exposed parameter's slot takes semantic priority.  This
			// prevents stale VariableNodes records — left behind when the
			// user exposes a pin (which breaks the wire but doesn't rebuild
			// VariableNodes until the next SyncToTypeAsset) — from
			// shadowing the exposed parameter via the InputPinSourceOffsets
			// priority-1 check in TryResolveInputPin.
			if (DataBlock.ExposedInputPinOffsets.Contains(TargetKey))
			{
				continue;
			}

			DataBlock.InputPinSourceOffsets.Add(TargetKey, *VarOffset);
		}
	}

	return DataBlock;
}

void UComposableCameraTypeAsset::ApplyParameterBlock(
	FComposableCameraRuntimeDataBlock& DataBlock,
	const FComposableCameraParameterBlock& Parameters) const
{
	using namespace ComposableCameraTypeAssetPrivate;

	// --- Exposed parameters ---
	// Caller value only: the default lives on the node's pin (resolved by
	// GetExposedParameterDefaultValue at the caller site — K2 node, DataTable
	// row). By the time we get here, either the ParameterBlock has an entry
	// or the slot stays zeroed.
	for (const FComposableCameraExposedParameter& Param : ExposedParameters)
	{
		const int32* Offset = DataBlock.ExposedParameterOffsets.Find(Param.ParameterName);
		if (!Offset)
		{
			continue;
		}

		const int32 Size = GetPinTypeSize(Param.PinType, Param.StructType);
		if (Size <= 0)
		{
			continue;
		}

		// Try to copy from the caller's parameter block.
		const int32 Copied = Parameters.CopyRawTo(
			Param.ParameterName,
			DataBlock.Storage.GetData() + *Offset,
			Size);

		if (Copied == 0 && Param.bRequired)
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("ApplyParameterBlock: Required parameter '%s' was not provided by the caller."),
				*Param.ParameterName.ToString());
		}
	}

	// --- Internal variables ---
	// Caller cannot reach these; the only initial-value source is the variable's
	// own InitialValueString. We apply it here (at instantiation) rather than in
	// BuildRuntimeDataLayout so the data block layout step stays a pure offset
	// computation, and so the initial-value parse failure path logs exactly
	// once per activation instead of once per layout rebuild.
	for (const FComposableCameraInternalVariable& Var : InternalVariables)
	{
		const int32* Offset = DataBlock.InternalVariableOffsets.Find(Var.VariableName);
		if (!Offset)
		{
			continue;
		}

		const int32 Size = GetPinTypeSize(Var.VariableType, Var.StructType);
		if (Size <= 0)
		{
			continue;
		}

		ApplyInitialValueToSlot(
			Var,
			DataBlock.Storage.GetData() + *Offset,
			Size,
			*GetName());
	}

	// --- Exposed variables ---
	// Caller-provided value wins. If the caller didn't supply one, fall back to
	// the variable's InitialValueString so the author's default in the Details
	// panel is honored. This mirrors the DataTable row path's node-pin-default
	// fallback for exposed parameters.
	for (const FComposableCameraInternalVariable& Var : ExposedVariables)
	{
		const int32* Offset = DataBlock.InternalVariableOffsets.Find(Var.VariableName);
		if (!Offset)
		{
			continue;
		}

		const int32 Size = GetPinTypeSize(Var.VariableType, Var.StructType);
		if (Size <= 0)
		{
			continue;
		}

		uint8* Dest = DataBlock.Storage.GetData() + *Offset;

		const int32 Copied = Parameters.CopyRawTo(Var.VariableName, Dest, Size);

		if (Copied == 0)
		{
			// Caller didn't supply a value — apply the authored initial value.
			ApplyInitialValueToSlot(Var, Dest, Size, *GetName());
		}
	}
}

void UComposableCameraTypeAsset::ApplyDelegateBindings(
	AComposableCameraCameraBase* Camera,
	const FComposableCameraParameterBlock& Parameters) const
{
	if (!Camera || Parameters.DelegateValues.Num() == 0)
	{
		return;
	}

	for (const FComposableCameraExposedParameter& Param : ExposedParameters)
	{
		if (Param.PinType != EComposableCameraPinType::Delegate)
		{
			continue;
		}

		const FScriptDelegate* SourceDelegate = Parameters.DelegateValues.Find(Param.ParameterName);
		if (!SourceDelegate)
		{
			if (Param.bRequired)
			{
				UE_LOG(LogComposableCameraSystem, Warning,
					TEXT("ApplyDelegateBindings: Required delegate parameter '%s' was not provided by the caller."),
					*Param.ParameterName.ToString());
			}
			continue;
		}

		if (!Camera->CameraNodes.IsValidIndex(Param.TargetNodeIndex))
		{
			continue;
		}

		UComposableCameraCameraNodeBase* Node = Camera->CameraNodes[Param.TargetNodeIndex];
		if (!Node)
		{
			continue;
		}

		// Find the FDelegateProperty on the node by the pin's target name and write
		// the bound delegate directly into the node's UPROPERTY via reflection.
		FProperty* Prop = Node->GetClass()->FindPropertyByName(Param.TargetPinName);
		FDelegateProperty* DelegateProp = CastField<FDelegateProperty>(Prop);
		if (!DelegateProp)
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("ApplyDelegateBindings: Could not find FDelegateProperty '%s' on node class '%s'."),
				*Param.TargetPinName.ToString(),
				*Node->GetClass()->GetName());
			continue;
		}

		FScriptDelegate* DestDelegate = DelegateProp->GetPropertyValuePtr_InContainer(Node);
		if (DestDelegate)
		{
			*DestDelegate = *SourceDelegate;
		}
	}
}

#if WITH_EDITOR
void UComposableCameraTypeAsset::Build(bool bLogResult)
{
	BuildMessages.Reset();
	BuildStatus = EComposableCameraBuildStatus::Success;

	// Check: empty node chain.
	if (NodeTemplates.Num() == 0)
	{
		FComposableCameraBuildMessage Msg;
		Msg.Severity = 1; // Warning
		Msg.Message = FText::FromString(TEXT("Camera type has no nodes."));
		BuildMessages.Add(Msg);
		BuildStatus = EComposableCameraBuildStatus::SuccessWithWarnings;
	}

	// Check: null node templates.
	for (int32 i = 0; i < NodeTemplates.Num(); ++i)
	{
		if (!NodeTemplates[i])
		{
			FComposableCameraBuildMessage Msg;
			Msg.Severity = 2; // Error
			Msg.Message = FText::Format(
				FText::FromString(TEXT("Node at index {0} is null.")),
				FText::AsNumber(i));
			Msg.NodeIndex = i;
			BuildMessages.Add(Msg);
			BuildStatus = EComposableCameraBuildStatus::Failed;
		}
	}

	// Check: duplicate exposed parameter names.
	{
		TSet<FName> SeenNames;
		for (const FComposableCameraExposedParameter& Param : ExposedParameters)
		{
			if (SeenNames.Contains(Param.ParameterName))
			{
				FComposableCameraBuildMessage Msg;
				Msg.Severity = 2;
				Msg.Message = FText::Format(
					FText::FromString(TEXT("Duplicate exposed parameter name: '{0}'.")),
					FText::FromName(Param.ParameterName));
				BuildMessages.Add(Msg);
				BuildStatus = EComposableCameraBuildStatus::Failed;
			}
			SeenNames.Add(Param.ParameterName);
		}
	}

	// Check: duplicate internal variable names.
	{
		TSet<FName> SeenNames;
		for (const FComposableCameraInternalVariable& Var : InternalVariables)
		{
			if (SeenNames.Contains(Var.VariableName))
			{
				FComposableCameraBuildMessage Msg;
				Msg.Severity = 2;
				Msg.Message = FText::Format(
					FText::FromString(TEXT("Duplicate internal variable name: '{0}'.")),
					FText::FromName(Var.VariableName));
				BuildMessages.Add(Msg);
				BuildStatus = EComposableCameraBuildStatus::Failed;
			}
			SeenNames.Add(Var.VariableName);
		}
	}

	// Check: duplicate exposed variable names.
	{
		TSet<FName> SeenNames;
		for (const FComposableCameraInternalVariable& Var : ExposedVariables)
		{
			if (SeenNames.Contains(Var.VariableName))
			{
				FComposableCameraBuildMessage Msg;
				Msg.Severity = 2;
				Msg.Message = FText::Format(
					FText::FromString(TEXT("Duplicate exposed variable name: '{0}'.")),
					FText::FromName(Var.VariableName));
				BuildMessages.Add(Msg);
				BuildStatus = EComposableCameraBuildStatus::Failed;
			}
			SeenNames.Add(Var.VariableName);
		}
	}

	// Check: cross-set name uniqueness across ExposedParameters / InternalVariables
	// / ExposedVariables.
	//
	// All three collections share the runtime's FName keyspace inside the data
	// block — ExposedParameters land in ExposedParameterOffsets, both variable
	// kinds land in InternalVariableOffsets, and the caller's ParameterBlock is
	// indexed by name only. A collision between any two of these would either
	// overwrite a slot (ExposedVariable vs InternalVariable, caught defensively
	// in BuildRuntimeDataLayout) or silently misroute a caller value (exposed
	// parameter vs exposed variable with the same name would both try to read
	// the caller's block under identical keys). Flag it here at author time so
	// the user catches it in the editor rather than at runtime.
	{
		TMap<FName, const TCHAR*> NameSources; // FName → "source category label"
		auto ClaimName = [&](FName Name, const TCHAR* Category)
		{
			if (Name.IsNone())
			{
				return;
			}
			if (const TCHAR* const* ExistingCategory = NameSources.Find(Name))
			{
				FComposableCameraBuildMessage Msg;
				Msg.Severity = 2;
				Msg.Message = FText::Format(
					FText::FromString(TEXT("Name collision: '{0}' is declared as both a {1} and a {2}. Each exposed parameter, internal variable, and exposed variable must have a unique name.")),
					FText::FromName(Name),
					FText::FromString(*ExistingCategory),
					FText::FromString(Category));
				BuildMessages.Add(Msg);
				BuildStatus = EComposableCameraBuildStatus::Failed;
			}
			else
			{
				NameSources.Add(Name, Category);
			}
		};

		for (const FComposableCameraExposedParameter& Param : ExposedParameters)
		{
			ClaimName(Param.ParameterName, TEXT("exposed parameter"));
		}
		for (const FComposableCameraInternalVariable& Var : InternalVariables)
		{
			ClaimName(Var.VariableName, TEXT("internal variable"));
		}
		for (const FComposableCameraInternalVariable& Var : ExposedVariables)
		{
			ClaimName(Var.VariableName, TEXT("exposed variable"));
		}
	}

	// Check: valid indices only.
	for (const FComposableCameraPinConnection& Conn : PinConnections)
	{
		if (Conn.SourceNodeIndex < 0 || Conn.SourceNodeIndex >= NodeTemplates.Num()
			|| Conn.TargetNodeIndex < 0 || Conn.TargetNodeIndex >= NodeTemplates.Num())
		{
			FComposableCameraBuildMessage Msg;
			Msg.Severity = 2;
			Msg.Message = FText::FromString(TEXT("Connection references out-of-bounds node index."));
			BuildMessages.Add(Msg);
			BuildStatus = EComposableCameraBuildStatus::Failed;
		}
	}

	// Check: exposed + wired conflict (safety net).
	{
		TSet<FComposableCameraPinKey> WiredInputs;
		for (const FComposableCameraPinConnection& Conn : PinConnections)
		{
			FComposableCameraPinKey Key;
			Key.NodeIndex = Conn.TargetNodeIndex;
			Key.PinName = Conn.TargetPinName;
			WiredInputs.Add(Key);
		}

		for (const FComposableCameraExposedParameter& Param : ExposedParameters)
		{
			FComposableCameraPinKey Key;
			Key.NodeIndex = Param.TargetNodeIndex;
			Key.PinName = Param.TargetPinName;

			if (WiredInputs.Contains(Key))
			{
				FComposableCameraBuildMessage Msg;
				Msg.Severity = 2;
				Msg.Message = FText::Format(
					FText::FromString(TEXT("Pin '{0}' on node {1} is both wired and exposed as parameter '{2}'. A pin cannot be both.")),
					FText::FromName(Param.TargetPinName),
					FText::AsNumber(Param.TargetNodeIndex),
					FText::FromName(Param.ParameterName));
				Msg.NodeIndex = Param.TargetNodeIndex;
				Msg.PinName = Param.TargetPinName;
				BuildMessages.Add(Msg);
				BuildStatus = EComposableCameraBuildStatus::Failed;
			}
		}
	}

	// Check: unwired required input pins.
	{
		// Collect all input pins that have a data source: direct wire,
		// exposed parameter, variable Get node connection, or per-instance
		// default override.
		TSet<FComposableCameraPinKey> ResolvedInputs;

		// 1. Direct camera-node-to-camera-node data wires.
		for (const FComposableCameraPinConnection& Conn : PinConnections)
		{
			FComposableCameraPinKey Key;
			Key.NodeIndex = Conn.TargetNodeIndex;
			Key.PinName = Conn.TargetPinName;
			ResolvedInputs.Add(Key);
		}

		// 2. Exposed parameters (promoted input pins).
		for (const FComposableCameraExposedParameter& Param : ExposedParameters)
		{
			FComposableCameraPinKey Key;
			Key.NodeIndex = Param.TargetNodeIndex;
			Key.PinName = Param.TargetPinName;
			ResolvedInputs.Add(Key);
		}

		// 3. Variable Get node connections — a Get node's output wired to a
		//    camera node's input pin feeds data from the variable.
		for (const FComposableCameraVariableNodeRecord& VarRec : VariableNodes)
		{
			if (VarRec.bIsSetter || VarRec.bIsComputeChain)
			{
				continue;
			}
			for (const FComposableCameraVariablePinConnection& VarConn : VarRec.Connections)
			{
				FComposableCameraPinKey Key;
				Key.NodeIndex = VarConn.CameraNodeIndex;
				Key.PinName = VarConn.CameraPinName;
				ResolvedInputs.Add(Key);
			}
		}

		// 4. Per-instance default overrides set in the Details panel.
		for (int32 OverrideIdx = 0; OverrideIdx < NodePinOverrides.Num(); ++OverrideIdx)
		{
			for (const FComposableCameraPinOverride& Override : NodePinOverrides[OverrideIdx].Overrides)
			{
				if (Override.bHasDefaultOverride)
				{
					FComposableCameraPinKey Key;
					Key.NodeIndex = OverrideIdx;
					Key.PinName = Override.PinName;
					ResolvedInputs.Add(Key);
				}
			}
		}

		for (int32 NodeIdx = 0; NodeIdx < NodeTemplates.Num(); ++NodeIdx)
		{
			const UComposableCameraCameraNodeBase* Node = NodeTemplates[NodeIdx];
			if (!Node)
			{
				continue;
			}

			TArray<FComposableCameraNodePinDeclaration> Pins;
			Node->GatherAllPinDeclarations(Pins);

			for (const FComposableCameraNodePinDeclaration& Pin : Pins)
			{
				if (Pin.Direction != EComposableCameraPinDirection::Input)
				{
					continue;
				}

				FComposableCameraPinKey Key;
				Key.NodeIndex = NodeIdx;
				Key.PinName = Pin.PinName;

				if (!ResolvedInputs.Contains(Key))
				{
					// When the pin declaration leaves DefaultValueString empty,
					// check whether the node class has an EditAnywhere UPROPERTY
					// with the same name. If so, the UPROPERTY's C++ initializer
					// serves as the implicit default and the node reads directly
					// from its member variable — no warning needed.
					const bool bHasDefaultValue = !Pin.DefaultValueString.IsEmpty();
					bool bHasPropertyDefault = false;
					if (!bHasDefaultValue)
					{
						const FProperty* Prop = Node->GetClass()->FindPropertyByName(Pin.PinName);
						bHasPropertyDefault = Prop && Prop->HasAnyPropertyFlags(CPF_Edit);
					}

					if (Pin.bRequired && !bHasDefaultValue && !bHasPropertyDefault)
					{
						FComposableCameraBuildMessage Msg;
						Msg.Severity = 2;
						Msg.Message = FText::Format(
							FText::FromString(TEXT("Required input pin '{0}' on node {1} ({2}) is not connected, not exposed, and has no default value.")),
							FText::FromString(Pin.PinName.ToString()),
							FText::AsNumber(NodeIdx),
							FText::FromString(Node->GetClass()->GetName()));
						Msg.NodeIndex = NodeIdx;
						Msg.PinName = Pin.PinName;
						BuildMessages.Add(Msg);
						BuildStatus = EComposableCameraBuildStatus::Failed;
					}
					else if (Pin.bRequired && (bHasDefaultValue || bHasPropertyDefault))
					{
						// Required pin with a static fallback (declaration
						// DefaultValueString or same-named EditAnywhere
						// UPROPERTY), but no wire / exposure / variable-get /
						// per-instance default override. The asset still runs
						// — the runtime resolves the pin from whichever
						// fallback is present — but the author declared
						// `bRequired = true` to signal "this value matters",
						// and relying on an implicit fallback usually means
						// the author *intended* to supply a value and just
						// forgot. A warning gives the inline badge path
						// something to surface without blocking saves the
						// way an Error would.
						FComposableCameraBuildMessage Msg;
						Msg.Severity = 1;
						Msg.Message = FText::Format(
							FText::FromString(TEXT("Required input pin '{0}' on node {1} ({2}) has no connection, exposure, or per-instance override. It will use the pin declaration's default or the node's same-named UPROPERTY. Wire it, expose it as a parameter, or set a default override to make the intent explicit.")),
							FText::FromString(Pin.PinName.ToString()),
							FText::AsNumber(NodeIdx),
							FText::FromString(Node->GetClass()->GetDisplayNameText().ToString()));
						Msg.NodeIndex = NodeIdx;
						Msg.PinName = Pin.PinName;
						BuildMessages.Add(Msg);
						if (BuildStatus == EComposableCameraBuildStatus::Success)
						{
							BuildStatus = EComposableCameraBuildStatus::SuccessWithWarnings;
						}
					}
					else if (!Pin.bRequired && !bHasDefaultValue && !bHasPropertyDefault)
					{
						FComposableCameraBuildMessage Msg;
						Msg.Severity = 1;
						Msg.Message = FText::Format(
							FText::FromString(TEXT("Optional input pin '{0}' on node {1} ({2}) has no connection and no default value. Will use zero.")),
							FText::FromString(Pin.PinName.ToString()),
							FText::AsNumber(NodeIdx),
							FText::FromString(Node->GetClass()->GetDisplayNameText().ToString()));
						Msg.NodeIndex = NodeIdx;
						Msg.PinName = Pin.PinName;
						BuildMessages.Add(Msg);
						if (BuildStatus == EComposableCameraBuildStatus::Success)
						{
							BuildStatus = EComposableCameraBuildStatus::SuccessWithWarnings;
						}
					}
				}
			}
		}
	}

	if (bLogResult)
	{
		UE_LOG(LogComposableCameraSystem, Log, TEXT("Build complete for '%s': %s (%d messages)."),
			*GetName(),
			BuildStatus == EComposableCameraBuildStatus::Success ? TEXT("Success") :
			BuildStatus == EComposableCameraBuildStatus::SuccessWithWarnings ? TEXT("Warnings") : TEXT("Failed"),
			BuildMessages.Num());
	}
}

void UComposableCameraTypeAsset::EnsureInternalVariableGuids()
{
	bool bDirtied = false;
	for (FComposableCameraInternalVariable& Var : InternalVariables)
	{
		if (!Var.VariableGuid.IsValid())
		{
			Var.VariableGuid = FGuid::NewGuid();
			bDirtied = true;
		}
	}
	if (bDirtied)
	{
		// Legacy migration — don't dirty the package on load; the save will
		// pick up the new GUIDs the next time the user edits the asset.
	}
}

void UComposableCameraTypeAsset::EnsureExposedVariableGuids()
{
	// Mirror of EnsureInternalVariableGuids() — ExposedVariables share the
	// same struct type and the same GUID-based identity rules (editor graph
	// nodes resolve them by VariableGuid primary, VariableName fallback), so
	// they need the exact same migration pass.
	bool bDirtied = false;
	for (FComposableCameraInternalVariable& Var : ExposedVariables)
	{
		if (!Var.VariableGuid.IsValid())
		{
			Var.VariableGuid = FGuid::NewGuid();
			bDirtied = true;
		}
	}
	if (bDirtied)
	{
		// Same rationale as the internal variant — PostLoad migration should
		// not mark the package dirty; the next user edit will persist the new
		// GUIDs naturally.
	}
}

FName UComposableCameraTypeAsset::MakeUniqueExposedName(FName BaseName, FName NameAlreadyOwned) const
{
	// Empty / None names cannot be made unique — bail out and let the caller
	// decide what to do (typically they shouldn't be calling us with NAME_None
	// in the first place, but defending against it keeps the helper safe).
	if (BaseName.IsNone())
	{
		return BaseName;
	}

	// Build the set of currently-used names across all three collections.
	// The cross-set uniqueness invariant is enforced at Build() time, but
	// PostLoad migration may be running on legacy assets where it has not
	// yet been satisfied — that's exactly why this helper exists.
	TSet<FName> UsedNames;
	UsedNames.Reserve(ExposedParameters.Num() + ExposedVariables.Num() + InternalVariables.Num());

	for (const FComposableCameraExposedParameter& Param : ExposedParameters)
	{
		if (!Param.ParameterName.IsNone() && Param.ParameterName != NameAlreadyOwned)
		{
			UsedNames.Add(Param.ParameterName);
		}
	}
	for (const FComposableCameraInternalVariable& Var : ExposedVariables)
	{
		if (!Var.VariableName.IsNone() && Var.VariableName != NameAlreadyOwned)
		{
			UsedNames.Add(Var.VariableName);
		}
	}
	for (const FComposableCameraInternalVariable& Var : InternalVariables)
	{
		if (!Var.VariableName.IsNone() && Var.VariableName != NameAlreadyOwned)
		{
			UsedNames.Add(Var.VariableName);
		}
	}

	// Fast path: BaseName is already free.
	if (!UsedNames.Contains(BaseName))
	{
		return BaseName;
	}

	// Suffix path: try BaseName_2, BaseName_3, ... until we find a free slot.
	// Start at 2 because the unsuffixed BaseName is conceptually "_1" and is
	// what the user typed; matching the convention humans use when naming
	// duplicates by hand.
	const FString BaseString = BaseName.ToString();
	for (int32 Suffix = 2; Suffix < TNumericLimits<int32>::Max(); ++Suffix)
	{
		const FName Candidate(*FString::Printf(TEXT("%s_%d"), *BaseString, Suffix));
		if (!UsedNames.Contains(Candidate))
		{
			return Candidate;
		}
	}

	// Pathological — couldn't find a free name in 2 billion tries. Return the
	// base and let downstream validation flag it. This branch is unreachable
	// in any sane authoring scenario.
	return BaseName;
}

bool UComposableCameraTypeAsset::DeduplicateExposedNames()
{
	// We walk all three collections in a fixed order and track every name
	// we've already seen. The first occurrence of any name is preserved
	// untouched; subsequent occurrences get suffixed.
	//
	// Order matters: ExposedParameters → ExposedVariables → InternalVariables.
	// ExposedParameters come first because they're directly user-facing on K2
	// nodes — preserving their authored names preserves any existing K2 node
	// override-pin selections (UK2Node_ActivateComposableCamera::UserOverrideNames
	// keys on these names).
	TSet<FName> SeenNames;
	SeenNames.Reserve(ExposedParameters.Num() + ExposedVariables.Num() + InternalVariables.Num());
	bool bAnyRenamed = false;

	auto Suffix = [&SeenNames](FName BaseName) -> FName
	{
		// Local mirror of the MakeUniqueExposedName suffix loop, but keyed on
		// SeenNames (the names we've already committed to during this pass)
		// rather than the live asset state. This avoids the case where two
		// duplicates of the same name would both look free against the asset
		// (because we haven't renamed either yet) and end up colliding with
		// each other after the rename.
		const FString BaseString = BaseName.ToString();
		for (int32 SuffixIndex = 2; SuffixIndex < TNumericLimits<int32>::Max(); ++SuffixIndex)
		{
			const FName Candidate(*FString::Printf(TEXT("%s_%d"), *BaseString, SuffixIndex));
			if (!SeenNames.Contains(Candidate))
			{
				return Candidate;
			}
		}
		return BaseName;
	};

	for (FComposableCameraExposedParameter& Param : ExposedParameters)
	{
		if (Param.ParameterName.IsNone())
		{
			continue;
		}
		if (SeenNames.Contains(Param.ParameterName))
		{
			const FName OldName = Param.ParameterName;
			const FName NewName = Suffix(OldName);
			Param.ParameterName = NewName;
			SeenNames.Add(NewName);
			bAnyRenamed = true;
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("[%s] Duplicate exposed parameter name '%s' renamed to '%s' during PostLoad migration."),
				*GetName(), *OldName.ToString(), *NewName.ToString());
		}
		else
		{
			SeenNames.Add(Param.ParameterName);
		}
	}

	for (FComposableCameraInternalVariable& Var : ExposedVariables)
	{
		if (Var.VariableName.IsNone())
		{
			continue;
		}
		if (SeenNames.Contains(Var.VariableName))
		{
			const FName OldName = Var.VariableName;
			const FName NewName = Suffix(OldName);
			Var.VariableName = NewName;
			SeenNames.Add(NewName);
			bAnyRenamed = true;
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("[%s] Duplicate exposed variable name '%s' renamed to '%s' during PostLoad migration."),
				*GetName(), *OldName.ToString(), *NewName.ToString());
		}
		else
		{
			SeenNames.Add(Var.VariableName);
		}
	}

	for (FComposableCameraInternalVariable& Var : InternalVariables)
	{
		if (Var.VariableName.IsNone())
		{
			continue;
		}
		if (SeenNames.Contains(Var.VariableName))
		{
			const FName OldName = Var.VariableName;
			const FName NewName = Suffix(OldName);
			Var.VariableName = NewName;
			SeenNames.Add(NewName);
			bAnyRenamed = true;
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("[%s] Duplicate internal variable name '%s' renamed to '%s' during PostLoad migration."),
				*GetName(), *OldName.ToString(), *NewName.ToString());
		}
		else
		{
			SeenNames.Add(Var.VariableName);
		}
	}

	return bAnyRenamed;
}

void UComposableCameraTypeAsset::PostLoad()
{
	Super::PostLoad();

	// Migrate any legacy InternalVariable / ExposedVariable entries that
	// pre-date the VariableGuid field. Existing editor graphs still reference
	// variables by FName via UComposableCameraVariableGraphNode::VariableName;
	// the editor's RebuildFromTypeAsset will copy the migrated GUIDs over to
	// those nodes on the next rebuild pass.
	EnsureInternalVariableGuids();
	EnsureExposedVariableGuids();

	// Migrate any legacy assets that contain duplicate names across the
	// ExposedParameters / ExposedVariables / InternalVariables collections.
	// The cross-set uniqueness invariant is now enforced at expose-time
	// (UComposableCameraNodeGraphNode::ExposePinAsParameter routes through
	// MakeUniqueExposedName) but pre-guard assets may still contain
	// collisions, which would otherwise produce phantom duplicate pins on
	// any K2 ActivateComposableCamera node referencing this asset.
	// Don't dirty the package — same rationale as the EnsureGuids passes.
	DeduplicateExposedNames();
}

void UComposableCameraTypeAsset::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	// Whenever the user touches InternalVariables / ExposedVariables — add,
	// duplicate, paste — make sure every entry has a valid GUID. Without
	// this, newly-added variables would share an invalid GUID and the
	// editor's variable graph nodes would all look identical under
	// GUID-based identity.
	const FName PropertyName = PropertyChangedEvent.PropertyChain.GetActiveMemberNode()
		? PropertyChangedEvent.PropertyChain.GetActiveMemberNode()->GetValue()->GetFName()
		: NAME_None;

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UComposableCameraTypeAsset, InternalVariables))
	{
		EnsureInternalVariableGuids();
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UComposableCameraTypeAsset, ExposedVariables))
	{
		EnsureExposedVariableGuids();
	}

	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}
#endif

FPrimaryAssetId UComposableCameraTypeAsset::GetPrimaryAssetId() const
{
	return FPrimaryAssetId("ComposableCameraType", GetFName());
}
