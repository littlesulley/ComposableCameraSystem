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

		// CopyRawTo is a no-op on PinType / exact-size mismatch, so a stale
		// InitialValueString from a previous type never corrupts an unrelated
		// slot. Pass the variable's currently-expected PinType so a row whose
		// authored value was the right SIZE but the wrong TYPE (e.g., Int32
		// 4 B serialized into what is now a Float 4 B slot) also gets rejected
		// instead of silently delivering wrong-shape bytes.
		Scratch.CopyRawTo(Var.VariableName, Dest, DestSize, Var.VariableType);
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

	auto RegisterReferenceSlot = [&DataBlock](EComposableCameraPinType PinType, int32 Offset)
	{
		DataBlock.RegisterReferenceSlot(PinType, Offset);
	};

	// Slot allocation helper -- returns the offset to record in the relevant
	// offset table (OutputPinOffsets / ExposedParameterOffsets / etc.), or
	// INDEX_NONE if the pin's storage size is zero (caller decides whether to
	// log + skip).
	//
	// Two storage classes converge here:
	//   * POD (or pin types whose size is known constant): bytes live in
	//     Storage at a real offset advanced by `CurrentOffset`. Reference
	//     mirror tracking happens via RegisterReferenceSlot for Actor / Object
	//     pins.
	//   * non-POD struct (USTRUCT failing IsBytewiseSafeStruct): owned typed
	//     memory lives in StructSlots; the returned synthetic offset is
	//     >= StructSlotsOffsetBase and is rejected by the byte-storage check
	//     in ReadValue / WriteValue / RegisterReferenceSlot, redirecting
	//     reads / writes / GC to the FInstancedStruct slot.
	auto AllocateSlot = [&](EComposableCameraPinType PinType, UScriptStruct* StructType) -> int32
	{
		if (PinType == EComposableCameraPinType::Struct
			&& StructType != nullptr
			&& !IsBytewiseSafeStruct(StructType))
		{
			const int32 StructOffset = DataBlock.RegisterStructSlot(StructType);
			// Record the struct slot's shape too. Templated `ReadValue<T>` /
			// `WriteValue<T>` short-circuit struct-slot offsets via the
			// `IsStructSlotOffset` early-return when T itself is a USTRUCT;
			// this entry catches the OPPOSITE mistake — a non-struct T
			// (`float` / `bool` / etc.) accessing a struct-slot offset.
			// The recorded size is 0 (sentinel — typed struct slots don't
			// have a meaningful byte size in the Storage sense) which
			// makes `Shape->Size != sizeof(T)` reliably trip on any T
			// other than... well, no T has sizeof 0. Always rejects.
			// Record StructType too — the templated read/write path's
			// struct-slot branch verifies T::StaticStruct() == StructType
			// before CopyScriptStruct so a stale offset table cannot drive
			// a wrong-shape T into the slot's typed memory.
			DataBlock.SlotShapes.Add(StructOffset,
				FComposableCameraRuntimeDataBlock::FSlotShape{ PinType, /*Size=*/0, StructType });
			return StructOffset;
		}

		const int32 Size = GetPinTypeSize(PinType, StructType);
		const int32 Align = GetPinTypeAlignment(PinType, StructType);
		if (Size <= 0)
		{
			return INDEX_NONE;
		}
		AlignOffset(Align);
		const int32 Offset = CurrentOffset;
		DataBlock.RegisterReferenceSlot(PinType, Offset);
		// Record shape for the templated read/write path's strict
		// validation. Stored as PinType+Size — readers compare against
		// `ExpectedPinTypeFor<T>()` and `sizeof(T)`. For POD struct slots
		// (FVector / FRotator / user POD USTRUCTs that pass
		// IsBytewiseSafeStruct) we additionally record StructType so a
		// same-size cross-struct read (e.g. `ReadValue<FCustom12B>` against
		// an `FVector` slot) is refused by the symmetric struct-type check
		// in ReadValue / WriteValue.
		UScriptStruct* RecordStructType =
			(PinType == EComposableCameraPinType::Struct) ? StructType : nullptr;
		DataBlock.SlotShapes.Add(Offset,
			FComposableCameraRuntimeDataBlock::FSlotShape{ PinType, Size, RecordStructType });
		CurrentOffset += Size;
		return Offset;
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

			const int32 Offset = AllocateSlot(Pin.PinType, Pin.StructType);
			if (Offset == INDEX_NONE)
			{
				UE_LOG(LogComposableCameraSystem, Warning,
					TEXT("BuildRuntimeDataLayout: Output pin '%s' on node %d has zero size. Skipping."),
					*Pin.PinName.ToString(), NodeIdx);
				continue;
			}

			FComposableCameraPinKey Key;
			Key.NodeIndex = NodeIdx;
			Key.PinName = Pin.PinName;
			DataBlock.OutputPinOffsets.Add(Key, Offset);
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

			const int32 Offset = AllocateSlot(Pin.PinType, Pin.StructType);
			if (Offset == INDEX_NONE)
			{
				UE_LOG(LogComposableCameraSystem, Warning,
					TEXT("BuildRuntimeDataLayout: Output pin '%s' on compute node %d has zero size. Skipping."),
					*Pin.PinName.ToString(), ComputeIdx);
				continue;
			}

			FComposableCameraPinKey Key;
			Key.NodeIndex = ComputeNodeIndexBase + ComputeIdx;
			Key.PinName = Pin.PinName;
			DataBlock.OutputPinOffsets.Add(Key, Offset);
		}
	}

	// --- Exposed parameter slots ---
	for (const FComposableCameraExposedParameter& Param : ExposedParameters)
	{
		const int32 Offset = AllocateSlot(Param.PinType, Param.StructType);
		if (Offset == INDEX_NONE)
		{
			continue;
		}
		DataBlock.ExposedParameterOffsets.Add(Param.ParameterName, Offset);
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

				const int32 Offset = AllocateSlot(Decl->PinType, Decl->StructType);
				if (Offset == INDEX_NONE)
				{
					continue;
				}
				FComposableCameraPinKey Key;
				Key.NodeIndex = NodeIdx;
				Key.PinName = Override.PinName;
				DataBlock.DefaultValueOffsets.Add(Key, Offset);
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

				const int32 Offset = AllocateSlot(Decl->PinType, Decl->StructType);
				if (Offset == INDEX_NONE)
				{
					continue;
				}
				FComposableCameraPinKey Key;
				Key.NodeIndex = ComputeNodeIndexBase + ComputeIdx;
				Key.PinName = Override.PinName;
				DataBlock.DefaultValueOffsets.Add(Key, Offset);
			}
		}
	}

	// --- Internal variable slots ---
	for (const FComposableCameraInternalVariable& Var : InternalVariables)
	{
		const int32 Offset = AllocateSlot(Var.VariableType, Var.StructType);
		if (Offset == INDEX_NONE)
		{
			continue;
		}
		DataBlock.InternalVariableOffsets.Add(Var.VariableName, Offset);
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
		if (DataBlock.InternalVariableOffsets.Contains(Var.VariableName))
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("BuildRuntimeDataLayout: [%s] exposed variable '%s' collides with an existing internal variable of the same name. Skipping — fix the duplicate in the type asset."),
				*GetName(), *Var.VariableName.ToString());
			continue;
		}

		const int32 Offset = AllocateSlot(Var.VariableType, Var.StructType);
		if (Offset == INDEX_NONE)
		{
			continue;
		}
		DataBlock.InternalVariableOffsets.Add(Var.VariableName, Offset);
	}

	// Allocate storage.
	DataBlock.TotalSize = CurrentOffset;
	DataBlock.Storage.SetNumZeroed(CurrentOffset);

	// SeedSlot helper -- shared by camera and compute per-instance default
	// override seeding. Parses ValueString through the central ApplyStringValue
	// parser into a scratch ParameterBlock, then copies into the slot that
	// matches the offset's storage class (POD bytes vs FInstancedStruct).
	auto SeedSlot = [&DataBlock, this](
		int32 Offset,
		EComposableCameraPinType PinType,
		UScriptStruct* StructType,
		UEnum* EnumType,
		FName Name,
		const FString& ValueString,
		int32 NodeIdx,
		const TCHAR* NodeKind)
	{
		FComposableCameraParameterBlock Scratch;
		FString ParseError;
		const bool bOk = FComposableCameraParameterBlock::ApplyStringValue(
			Scratch, Name, PinType, StructType, EnumType, ValueString, &ParseError);
		if (!bOk)
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("BuildRuntimeDataLayout: [%s] per-instance default for pin '%s' on %s node %d failed to parse (%s). Slot left zero-initialized."),
				*GetName(), *Name.ToString(), NodeKind, NodeIdx, *ParseError);
			return;
		}

		if (DataBlock.IsStructSlotOffset(Offset))
		{
			// Non-POD struct: parser produced an FInstancedStruct in
			// Scratch.StructValues; CopyScriptStruct into the destination
			// slot's owned memory (which BuildRuntimeDataLayout already
			// InitializeAs'd to the right struct type via RegisterStructSlot).
			if (FInstancedStruct* Src = Scratch.StructValues.Find(Name))
			{
				FInstancedStruct& Dst = DataBlock.GetStructSlotMutableChecked(Offset);
				check(Dst.IsValid() && Dst.GetScriptStruct() == Src->GetScriptStruct());
				Dst.GetScriptStruct()->CopyScriptStruct(Dst.GetMutableMemory(), Src->GetMemory());
			}
			return;
		}

		// POD path: CopyRawTo is a no-op on PinType / exact-size mismatch,
		// so a stale override string from a previous type never corrupts
		// the slot. Pass the pin's CURRENT PinType so a row whose value
		// happens to match the slot size but not the type (e.g., Int32 vs
		// Float, both 4 B) is rejected too.
		const int32 Size = GetPinTypeSize(PinType, StructType);
		if (Size <= 0)
		{
			return;
		}
		Scratch.CopyRawTo(Name, DataBlock.Storage.GetData() + Offset, Size, PinType);
		DataBlock.RefreshReferenceSlot(Offset);
	};

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

				SeedSlot(*Offset, Decl->PinType, Decl->StructType, Decl->EnumType,
					Override.PinName, Override.DefaultValueOverride, NodeIdx, TEXT("camera"));
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

				SeedSlot(*Offset, Decl->PinType, Decl->StructType, Decl->EnumType,
					Override.PinName, Override.DefaultValueOverride, ComputeIdx, TEXT("compute"));
			}
		}
	}

	// Phase 2: Build the connection and exposure mappings for input pins.
	//
	// Every Add into InputPinSourceOffsets / ExposedInputPinOffsets is type-
	// validated against BOTH source and target pin declarations. The previous
	// behavior validated only that the source offset existed, so a stale asset
	// (saved before a pin was renamed / retyped), a hand-edited asset, or a
	// schema-bypass code path could wire a Float source into an Actor target —
	// the runtime would then read sizeof(AActor*) bytes from a 4-byte float
	// slot and dereference garbage as a UObject pointer, crashing inside
	// CopySlot's struct/POD discrimination check or at the first AActor::*
	// call.
	//
	// Per-node pin declarations are cached on first lookup because the same
	// target node typically receives several wired/exposed/variable inputs;
	// repeating GatherAllPinDeclarations once per Add would scale poorly on
	// graphs with high pin density. Cache key is the runtime NodeIndex space
	// (NodeTemplates < ComputeNodeIndexBase ≤ ComputeNodeTemplates).
	//
	// Storage uses TUniquePtr<TArray<...>> so the inner TArray's address stays
	// stable across cache growth — TMap::Add can rehash and move its values,
	// but the heap-allocated TArray that TUniquePtr owns does not. Without
	// this indirection, holding a pointer into one cached node's pin array
	// across a FindPinDecl call for a second node could deref reallocated
	// memory.
	TMap<int32, TUniquePtr<TArray<FComposableCameraNodePinDeclaration>>> NodePinCache;

	auto GetNodePinDecls = [&](int32 RuntimeNodeIndex) -> const TArray<FComposableCameraNodePinDeclaration>*
	{
		if (TUniquePtr<TArray<FComposableCameraNodePinDeclaration>>* Cached = NodePinCache.Find(RuntimeNodeIndex))
		{
			return Cached->Get();
		}
		TUniquePtr<TArray<FComposableCameraNodePinDeclaration>> Pins =
			MakeUnique<TArray<FComposableCameraNodePinDeclaration>>();
		if (RuntimeNodeIndex >= ComputeNodeIndexBase)
		{
			const int32 ComputeIdx = RuntimeNodeIndex - ComputeNodeIndexBase;
			if (!ComputeNodeTemplates.IsValidIndex(ComputeIdx) || !ComputeNodeTemplates[ComputeIdx])
			{
				return nullptr;
			}
			ComputeNodeTemplates[ComputeIdx]->GatherAllPinDeclarations(*Pins);
		}
		else
		{
			if (!NodeTemplates.IsValidIndex(RuntimeNodeIndex) || !NodeTemplates[RuntimeNodeIndex])
			{
				return nullptr;
			}
			NodeTemplates[RuntimeNodeIndex]->GatherAllPinDeclarations(*Pins);
		}
		const TArray<FComposableCameraNodePinDeclaration>* RawPtr = Pins.Get();
		NodePinCache.Add(RuntimeNodeIndex, MoveTemp(Pins));
		return RawPtr;
	};

	auto FindPinDecl = [&](int32 RuntimeNodeIndex, FName PinName, EComposableCameraPinDirection Dir)
		-> const FComposableCameraNodePinDeclaration*
	{
		const TArray<FComposableCameraNodePinDeclaration>* Pins = GetNodePinDecls(RuntimeNodeIndex);
		if (!Pins)
		{
			return nullptr;
		}
		return Pins->FindByPredicate([&](const FComposableCameraNodePinDeclaration& P)
		{
			return P.PinName == PinName && P.Direction == Dir;
		});
	};

	// Type-compatibility predicate. PinType must match exactly; for Struct
	// pins the StructType must match; for Enum pins the EnumType must match.
	// Other carrier metadata (DisplayName, Required, DefaultValue, Tooltip,
	// SignatureFunction) is irrelevant — only what determines storage layout
	// and runtime read/write width matters here.
	auto ArePinTypesCompatible = [](
		EComposableCameraPinType TypeA, const UScriptStruct* StructA, const UEnum* EnumA,
		EComposableCameraPinType TypeB, const UScriptStruct* StructB, const UEnum* EnumB) -> bool
	{
		if (TypeA != TypeB)
		{
			return false;
		}
		if (TypeA == EComposableCameraPinType::Struct && StructA != StructB)
		{
			return false;
		}
		if (TypeA == EComposableCameraPinType::Enum && EnumA != EnumB)
		{
			return false;
		}
		return true;
	};

	// Wired connections: input pin → source output pin offset.
	for (const FComposableCameraPinConnection& Conn : PinConnections)
	{
		FComposableCameraPinKey SourceKey;
		SourceKey.NodeIndex = Conn.SourceNodeIndex;
		SourceKey.PinName = Conn.SourcePinName;

		const int32* SourceOffset = DataBlock.OutputPinOffsets.Find(SourceKey);
		if (!SourceOffset)
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("BuildRuntimeDataLayout: [%s] Connection source pin '%s' on node %d not found. Skipping wire."),
				*GetName(), *Conn.SourcePinName.ToString(), Conn.SourceNodeIndex);
			continue;
		}

		const FComposableCameraNodePinDeclaration* SourceDecl =
			FindPinDecl(Conn.SourceNodeIndex, Conn.SourcePinName, EComposableCameraPinDirection::Output);
		const FComposableCameraNodePinDeclaration* TargetDecl =
			FindPinDecl(Conn.TargetNodeIndex, Conn.TargetPinName, EComposableCameraPinDirection::Input);
		if (!SourceDecl || !TargetDecl)
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("BuildRuntimeDataLayout: [%s] Connection target pin '%s' on node %d not declared (source pin '%s' on node %d). Skipping wire."),
				*GetName(), *Conn.TargetPinName.ToString(), Conn.TargetNodeIndex,
				*Conn.SourcePinName.ToString(), Conn.SourceNodeIndex);
			continue;
		}
		if (!ArePinTypesCompatible(
			SourceDecl->PinType, SourceDecl->StructType, SourceDecl->EnumType,
			TargetDecl->PinType, TargetDecl->StructType, TargetDecl->EnumType))
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("BuildRuntimeDataLayout: [%s] Connection pin-type mismatch: source '%s' on node %d (PinType=%d) → target '%s' on node %d (PinType=%d). Skipping wire to prevent runtime read of incompatible-shape bytes."),
				*GetName(),
				*Conn.SourcePinName.ToString(), Conn.SourceNodeIndex, static_cast<int32>(SourceDecl->PinType),
				*Conn.TargetPinName.ToString(), Conn.TargetNodeIndex, static_cast<int32>(TargetDecl->PinType));
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
		const int32 RuntimeSourceIdx = ComputeNodeIndexBase + Conn.SourceNodeIndex;
		const int32 RuntimeTargetIdx = ComputeNodeIndexBase + Conn.TargetNodeIndex;

		FComposableCameraPinKey SourceKey;
		SourceKey.NodeIndex = RuntimeSourceIdx;
		SourceKey.PinName = Conn.SourcePinName;

		const int32* SourceOffset = DataBlock.OutputPinOffsets.Find(SourceKey);
		if (!SourceOffset)
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("BuildRuntimeDataLayout: [%s] Compute connection source pin '%s' on compute node %d not found. Skipping wire."),
				*GetName(), *Conn.SourcePinName.ToString(), Conn.SourceNodeIndex);
			continue;
		}

		const FComposableCameraNodePinDeclaration* SourceDecl =
			FindPinDecl(RuntimeSourceIdx, Conn.SourcePinName, EComposableCameraPinDirection::Output);
		const FComposableCameraNodePinDeclaration* TargetDecl =
			FindPinDecl(RuntimeTargetIdx, Conn.TargetPinName, EComposableCameraPinDirection::Input);
		if (!SourceDecl || !TargetDecl)
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("BuildRuntimeDataLayout: [%s] Compute connection target pin '%s' on compute node %d not declared (source pin '%s' on compute node %d). Skipping wire."),
				*GetName(), *Conn.TargetPinName.ToString(), Conn.TargetNodeIndex,
				*Conn.SourcePinName.ToString(), Conn.SourceNodeIndex);
			continue;
		}
		if (!ArePinTypesCompatible(
			SourceDecl->PinType, SourceDecl->StructType, SourceDecl->EnumType,
			TargetDecl->PinType, TargetDecl->StructType, TargetDecl->EnumType))
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("BuildRuntimeDataLayout: [%s] Compute connection pin-type mismatch: source '%s' on compute node %d (PinType=%d) → target '%s' on compute node %d (PinType=%d). Skipping wire."),
				*GetName(),
				*Conn.SourcePinName.ToString(), Conn.SourceNodeIndex, static_cast<int32>(SourceDecl->PinType),
				*Conn.TargetPinName.ToString(), Conn.TargetNodeIndex, static_cast<int32>(TargetDecl->PinType));
			continue;
		}

		FComposableCameraPinKey TargetKey;
		TargetKey.NodeIndex = RuntimeTargetIdx;
		TargetKey.PinName = Conn.TargetPinName;
		DataBlock.InputPinSourceOffsets.Add(TargetKey, *SourceOffset);
	}

	// Exposed parameters: input pin → parameter slot offset.
	//
	// Source side here is the FComposableCameraExposedParameter record itself
	// — it carries its own (PinType, StructType, EnumType) mirrored from the
	// pin it was originally exposed from. Validate the mirror still matches
	// the target node's current pin declaration so a parameter that was
	// exposed before its underlying pin's type was changed in C++ doesn't
	// silently route bytes of one shape into a slot of another shape.
	for (const FComposableCameraExposedParameter& Param : ExposedParameters)
	{
		const int32* ParamOffset = DataBlock.ExposedParameterOffsets.Find(Param.ParameterName);
		if (!ParamOffset)
		{
			continue;
		}

		const FComposableCameraNodePinDeclaration* TargetDecl =
			FindPinDecl(Param.TargetNodeIndex, Param.TargetPinName, EComposableCameraPinDirection::Input);
		if (!TargetDecl)
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("BuildRuntimeDataLayout: [%s] Exposed parameter '%s' targets undeclared pin '%s' on node %d. Skipping exposure."),
				*GetName(), *Param.ParameterName.ToString(),
				*Param.TargetPinName.ToString(), Param.TargetNodeIndex);
			continue;
		}
		if (!ArePinTypesCompatible(
			Param.PinType, Param.StructType, Param.EnumType,
			TargetDecl->PinType, TargetDecl->StructType, TargetDecl->EnumType))
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("BuildRuntimeDataLayout: [%s] Exposed parameter '%s' (PinType=%d) shape no longer matches target pin '%s' on node %d (PinType=%d). Skipping exposure — re-expose the pin to refresh."),
				*GetName(), *Param.ParameterName.ToString(), static_cast<int32>(Param.PinType),
				*Param.TargetPinName.ToString(), Param.TargetNodeIndex, static_cast<int32>(TargetDecl->PinType));
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

		// Resolve the variable name for offset lookup AND grab the variable
		// record itself for type validation. VariableGuid is the authoritative
		// identity, but InternalVariableOffsets is keyed by VariableName. Walk
		// InternalVariables + ExposedVariables to find the canonical name via
		// GUID, falling back to Record.VariableName.
		FName ResolvedName = Record.VariableName;
		const FComposableCameraInternalVariable* ResolvedVar = nullptr;
		auto FindVarByGuid = [&](const TArray<FComposableCameraInternalVariable>& Vars)
			-> const FComposableCameraInternalVariable*
		{
			for (const FComposableCameraInternalVariable& V : Vars)
			{
				if (V.VariableGuid == Record.VariableGuid)
				{
					return &V;
				}
			}
			return nullptr;
		};
		auto FindVarByName = [&](const TArray<FComposableCameraInternalVariable>& Vars)
			-> const FComposableCameraInternalVariable*
		{
			for (const FComposableCameraInternalVariable& V : Vars)
			{
				if (V.VariableName == ResolvedName)
				{
					return &V;
				}
			}
			return nullptr;
		};
		if (Record.VariableGuid.IsValid())
		{
			ResolvedVar = FindVarByGuid(InternalVariables);
			if (!ResolvedVar)
			{
				ResolvedVar = FindVarByGuid(ExposedVariables);
			}
			if (ResolvedVar)
			{
				ResolvedName = ResolvedVar->VariableName;
			}
		}
		// GUID resolution failed (legacy record / GUID lost) — fall back to
		// name lookup so we still get the type metadata for validation.
		if (!ResolvedVar)
		{
			ResolvedVar = FindVarByName(InternalVariables);
		}
		if (!ResolvedVar)
		{
			ResolvedVar = FindVarByName(ExposedVariables);
		}

		const int32* VarOffset = DataBlock.InternalVariableOffsets.Find(ResolvedName);
		if (!VarOffset)
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("BuildRuntimeDataLayout: [%s] Get variable node for '%s' — no InternalVariableOffset found. Connections from this getter will not resolve."),
				*GetName(), *ResolvedName.ToString());
			continue;
		}

		// For each consumer connection, map the consumer's input pin directly
		// to the variable's storage slot, but only if the consumer pin's type
		// matches the variable's type. A type mismatch (variable was retyped
		// after the Get node was wired) would route variable bytes of one
		// shape into a consumer slot of another shape — same crash class as
		// the wired-connection mismatch above.
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
			// the exposed parameter's slot takes semantic priority. This
			// prevents stale VariableNodes records — left behind when the
			// user exposes a pin (which breaks the wire but doesn't rebuild
			// VariableNodes until the next SyncToTypeAsset) — from
			// shadowing the exposed parameter via the InputPinSourceOffsets
			// priority-1 check in TryResolveInputPin.
			if (DataBlock.ExposedInputPinOffsets.Contains(TargetKey))
			{
				continue;
			}

			const FComposableCameraNodePinDeclaration* TargetDecl =
				FindPinDecl(RuntimeNodeIndex, Conn.CameraPinName, EComposableCameraPinDirection::Input);
			if (!TargetDecl)
			{
				UE_LOG(LogComposableCameraSystem, Warning,
					TEXT("BuildRuntimeDataLayout: [%s] Get variable '%s' wired to undeclared pin '%s' on node %d. Skipping wire."),
					*GetName(), *ResolvedName.ToString(),
					*Conn.CameraPinName.ToString(), Conn.CameraNodeIndex);
				continue;
			}
			if (ResolvedVar && !ArePinTypesCompatible(
				ResolvedVar->VariableType, ResolvedVar->StructType, ResolvedVar->EnumType,
				TargetDecl->PinType, TargetDecl->StructType, TargetDecl->EnumType))
			{
				UE_LOG(LogComposableCameraSystem, Warning,
					TEXT("BuildRuntimeDataLayout: [%s] Get variable '%s' (VariableType=%d) shape mismatches consumer pin '%s' on node %d (PinType=%d). Skipping wire — fix the variable type or rewire the consumer."),
					*GetName(), *ResolvedName.ToString(), static_cast<int32>(ResolvedVar->VariableType),
					*Conn.CameraPinName.ToString(), Conn.CameraNodeIndex, static_cast<int32>(TargetDecl->PinType));
				continue;
			}

			DataBlock.InputPinSourceOffsets.Add(TargetKey, *VarOffset);
		}
	}

	// SetVariable exec entries: validate source-output-pin type vs target-
	// variable type. Without this pass, a stale entry whose source pin's
	// type no longer matches the variable's type reaches CopySlot at
	// runtime; the POD memcpy branch reads VariableSlotSize bytes from the
	// source offset regardless of how many bytes actually live there. Float
	// source → Actor variable (8B target size) reads 4 bytes past the float
	// slot, then RefreshReferenceSlot reinterprets the resulting 8 bytes as
	// AActor* and registers the garbage pointer with the GC mirror — next
	// GC sweep can crash. Validate once at activation; the runtime check is
	// a single TSet::Contains per entry per tick.
	auto ValidateSetVariableEntries = [&](
		const TArray<FComposableCameraExecEntry>& Chain,
		bool bIsComputeChain,
		TSet<int32>& OutInvalidIndices,
		const TCHAR* ChainKind)
	{
		for (int32 EntryIdx = 0; EntryIdx < Chain.Num(); ++EntryIdx)
		{
			const FComposableCameraExecEntry& Entry = Chain[EntryIdx];
			if (Entry.EntryType != EComposableCameraExecEntryType::SetVariable)
			{
				continue;
			}
			// Entries that the runtime already short-circuits on (no source
			// node wired, no variable name, zero size) need no validation —
			// the existing `<= 0` early-out covers them. Note: the
			// StructSlotSentinel value (TNumericLimits<int32>::Max()) is
			// positive, so non-POD struct variables still get validated here.
			if (Entry.CameraNodeIndex == INDEX_NONE
				|| Entry.VariableName.IsNone()
				|| Entry.VariableSlotSize <= 0)
			{
				continue;
			}

			const int32 RuntimeSourceIdx = bIsComputeChain
				? (ComputeNodeIndexBase + Entry.CameraNodeIndex)
				: Entry.CameraNodeIndex;
			const FComposableCameraNodePinDeclaration* SourceDecl = FindPinDecl(
				RuntimeSourceIdx, Entry.SourcePinName, EComposableCameraPinDirection::Output);

			// Variable lookup by name (matches the runtime
			// InternalVariableOffsets keying). Walk InternalVariables first,
			// then ExposedVariables — same order BuildVariableLookup uses.
			const FComposableCameraInternalVariable* Var = nullptr;
			for (const FComposableCameraInternalVariable& V : InternalVariables)
			{
				if (V.VariableName == Entry.VariableName)
				{
					Var = &V;
					break;
				}
			}
			if (!Var)
			{
				for (const FComposableCameraInternalVariable& V : ExposedVariables)
				{
					if (V.VariableName == Entry.VariableName)
					{
						Var = &V;
						break;
					}
				}
			}

			if (!SourceDecl)
			{
				UE_LOG(LogComposableCameraSystem, Warning,
					TEXT("BuildRuntimeDataLayout: [%s] %s SetVariable entry %d: source pin '%s' on node %d not declared. Disabling entry."),
					*GetName(), ChainKind, EntryIdx,
					*Entry.SourcePinName.ToString(), Entry.CameraNodeIndex);
				OutInvalidIndices.Add(EntryIdx);
				continue;
			}
			if (!Var)
			{
				UE_LOG(LogComposableCameraSystem, Warning,
					TEXT("BuildRuntimeDataLayout: [%s] %s SetVariable entry %d: variable '%s' not found. Disabling entry."),
					*GetName(), ChainKind, EntryIdx, *Entry.VariableName.ToString());
				OutInvalidIndices.Add(EntryIdx);
				continue;
			}
			if (!ArePinTypesCompatible(
				SourceDecl->PinType, SourceDecl->StructType, SourceDecl->EnumType,
				Var->VariableType, Var->StructType, Var->EnumType))
			{
				UE_LOG(LogComposableCameraSystem, Warning,
					TEXT("BuildRuntimeDataLayout: [%s] %s SetVariable entry %d: source pin '%s' on node %d (PinType=%d) → variable '%s' (VariableType=%d) — type mismatch. Disabling entry to prevent runtime cross-slot read of %d bytes from a %d-byte source."),
					*GetName(), ChainKind, EntryIdx,
					*Entry.SourcePinName.ToString(), Entry.CameraNodeIndex, static_cast<int32>(SourceDecl->PinType),
					*Entry.VariableName.ToString(), static_cast<int32>(Var->VariableType),
					Entry.VariableSlotSize, GetPinTypeSize(SourceDecl->PinType, SourceDecl->StructType));
				OutInvalidIndices.Add(EntryIdx);
				continue;
			}

			// Type passed, now also check the serialized SlotSize matches the
			// variable's CURRENT type-derived size. Types can match while
			// SlotSize is stale: if the variable was originally Transform
			// (SlotSize=48) and later retyped to Float (SlotSize=4), the
			// editor sync MAY persist the entry with stale SlotSize=48 if the
			// SyncToTypeAsset path didn't re-walk this entry. CopySlot's POD
			// branch then reads 48 bytes from a 4-byte Float source slot,
			// overflowing into adjacent storage — corrupts the next slot
			// (which might be an Actor/Object reference whose mirror gets
			// updated by RefreshReferenceSlot on a subsequent tick). The
			// `<= 0` early-out doesn't help because the stale size is
			// positive. Use `GetVariableSlotSize` (returns the
			// `StructSlotSentinel` for non-POD struct, real bytes for POD)
			// so this check works uniformly across all variable types.
			const int32 ExpectedSlotSize = GetVariableSlotSize(Var->VariableType, Var->StructType);
			if (Entry.VariableSlotSize != ExpectedSlotSize)
			{
				UE_LOG(LogComposableCameraSystem, Warning,
					TEXT("BuildRuntimeDataLayout: [%s] %s SetVariable entry %d: variable '%s' currently expects SlotSize=%d but exec entry serialized SlotSize=%d (stale — variable was retyped without re-syncing the asset). Disabling entry to prevent runtime cross-slot memcpy. Re-save the asset to refresh."),
					*GetName(), ChainKind, EntryIdx,
					*Entry.VariableName.ToString(), ExpectedSlotSize, Entry.VariableSlotSize);
				OutInvalidIndices.Add(EntryIdx);
			}
		}
	};

	ValidateSetVariableEntries(
		FullExecChain, /*bIsComputeChain=*/false,
		DataBlock.InvalidSetVariableExecEntries, TEXT("camera"));
	ValidateSetVariableEntries(
		ComputeFullExecChain, /*bIsComputeChain=*/true,
		DataBlock.InvalidSetVariableComputeExecEntries, TEXT("compute"));

	return DataBlock;
}

void UComposableCameraTypeAsset::ApplyParameterBlock(
	FComposableCameraRuntimeDataBlock& DataBlock,
	const FComposableCameraParameterBlock& Parameters) const
{
	using namespace ComposableCameraTypeAssetPrivate;

	// Slot-write helpers that dispatch on the runtime offset's storage class
	// (POD bytes vs FInstancedStruct slot). The three roles (ExposedParameter,
	// InternalVariable, ExposedVariable) converge through these so non-POD
	// struct support stays uniform across them.
	auto CopyParamIntoSlot = [&DataBlock, &Parameters](
		FName Name,
		int32 Offset,
		EComposableCameraPinType PinType,
		UScriptStruct* StructType) -> bool
	{
		if (DataBlock.IsStructSlotOffset(Offset))
		{
			const FInstancedStruct* Src = Parameters.StructValues.Find(Name);
			if (!Src || !Src->IsValid())
			{
				return false;
			}
			FInstancedStruct& Dst = DataBlock.GetStructSlotMutableChecked(Offset);
			if (!Dst.IsValid() || Dst.GetScriptStruct() != Src->GetScriptStruct())
			{
				// Type mismatch (asset edit since activation, or a stale
				// caller value) -- skip silently rather than corrupt the slot.
				return false;
			}
			Dst.GetScriptStruct()->CopyScriptStruct(Dst.GetMutableMemory(), Src->GetMemory());
			return true;
		}

		const int32 Size = GetPinTypeSize(PinType, StructType);
		if (Size <= 0)
		{
			return false;
		}
		// Strict CopyRawTo: only succeeds when caller's parameter PinType
		// matches the slot's PinType AND Data.Num() == Size exactly. Stale
		// caller values that have the right name but the wrong shape are
		// rejected — runtime falls back to whatever the slot already holds
		// (zero-init or initial-value seed).
		const int32 Copied = Parameters.CopyRawTo(Name, DataBlock.Storage.GetData() + Offset, Size, PinType);
		if (Copied > 0)
		{
			DataBlock.RefreshReferenceSlot(Offset);
			return true;
		}
		return false;
	};

	auto SeedFromInitialValue = [&DataBlock, this](
		const FComposableCameraInternalVariable& Var,
		int32 Offset)
	{
		if (DataBlock.IsStructSlotOffset(Offset))
		{
			// Non-POD: parse InitialValueString through the central parser
			// (which produces an FInstancedStruct in Scratch.StructValues),
			// then CopyScriptStruct into the destination slot's owned memory.
			FComposableCameraParameterBlock Scratch;
			FString ParseError;
			const bool bOk = FComposableCameraParameterBlock::ApplyStringValue(
				Scratch, Var.VariableName, Var.VariableType, Var.StructType,
				Var.EnumType, Var.InitialValueString, &ParseError);
			if (!bOk)
			{
				UE_LOG(LogComposableCameraSystem, Warning,
					TEXT("ApplyParameterBlock: [%s] InitialValueString for variable '%s' failed to parse (%s). Slot left default-initialized."),
					*GetName(), *Var.VariableName.ToString(), *ParseError);
				return;
			}
			if (FInstancedStruct* Src = Scratch.StructValues.Find(Var.VariableName))
			{
				FInstancedStruct& Dst = DataBlock.GetStructSlotMutableChecked(Offset);
				check(Dst.IsValid() && Dst.GetScriptStruct() == Src->GetScriptStruct());
				Dst.GetScriptStruct()->CopyScriptStruct(Dst.GetMutableMemory(), Src->GetMemory());
			}
			return;
		}

		const int32 Size = GetPinTypeSize(Var.VariableType, Var.StructType);
		if (Size <= 0)
		{
			return;
		}
		ApplyInitialValueToSlot(Var, DataBlock.Storage.GetData() + Offset, Size, *GetName());
		DataBlock.RefreshReferenceSlot(Offset);
	};

	// --- Exposed parameters ---
	// Caller value only: the default lives on the node's pin (resolved by
	// GetExposedParameterDefaultValue at the caller site — K2 node, DataTable
	// row). By the time we get here, either the ParameterBlock has an entry
	// or the slot stays zero / default-initialized.
	for (const FComposableCameraExposedParameter& Param : ExposedParameters)
	{
		const int32* Offset = DataBlock.ExposedParameterOffsets.Find(Param.ParameterName);
		if (!Offset)
		{
			continue;
		}

		const bool bSupplied = CopyParamIntoSlot(
			Param.ParameterName, *Offset, Param.PinType, Param.StructType);

		if (!bSupplied && Param.bRequired)
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
		SeedFromInitialValue(Var, *Offset);
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

		const bool bSupplied = CopyParamIntoSlot(
			Var.VariableName, *Offset, Var.VariableType, Var.StructType);
		if (!bSupplied)
		{
			SeedFromInitialValue(Var, *Offset);
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
		if (!DestDelegate)
		{
			continue;
		}

		// Verify the source delegate's bound function signature matches the
		// target FDelegateProperty's expected signature before assigning.
		// FScriptDelegate carries (UObject* Object, FName FunctionName) only
		// — no signature record — so a stale BP / mistyped C++ caller can
		// install a delegate whose UFunction has a different parameter
		// layout than the target. If left unchecked, the eventual `Execute`
		// call walks the wrong parameter frame: ProcessEvent reads garbage
		// args / corrupts callee stack / asserts. Resolve the source's
		// UFunction by name on its bound UObject and compare against
		// `DelegateProp->SignatureFunction` via `IsSignatureCompatibleWith`
		// (the same predicate UE itself uses for delegate binding
		// validation). On mismatch, leave the target unbound and log so
		// the user can correct the wiring.
		auto IsDelegateSignatureCompatible = [&]() -> bool
		{
			// `FScriptDelegate::GetUObject()` is const-qualified and returns
			// `const UObject*`; `FindFunction` is const on UObject so we
			// can resolve through the const pointer. We don't need to
			// mutate the bound object here, only inspect its class.
			const UObject* BoundObj = SourceDelegate->GetUObject();
			if (!BoundObj)
			{
				// Empty/cleared delegate is fine — assigning a default-
				// constructed FScriptDelegate is the documented way to
				// "unbind" the target.
				return true;
			}
			UFunction* SourceFunc = BoundObj->FindFunction(SourceDelegate->GetFunctionName());
			if (!SourceFunc)
			{
				UE_LOG(LogComposableCameraSystem, Warning,
					TEXT("ApplyDelegateBindings: Source delegate '%s' bound to '%s::%s' but the function does not exist on the bound object — leaving target unbound."),
					*Param.ParameterName.ToString(),
					*BoundObj->GetClass()->GetName(),
					*SourceDelegate->GetFunctionName().ToString());
				return false;
			}
			if (!DelegateProp->SignatureFunction)
			{
				// Unusual — target FDelegateProperty has no signature
				// recorded. Refuse rather than guess.
				UE_LOG(LogComposableCameraSystem, Warning,
					TEXT("ApplyDelegateBindings: Target FDelegateProperty '%s' on node class '%s' has no SignatureFunction — leaving target unbound."),
					*Param.TargetPinName.ToString(),
					*Node->GetClass()->GetName());
				return false;
			}
			if (!SourceFunc->IsSignatureCompatibleWith(DelegateProp->SignatureFunction))
			{
				UE_LOG(LogComposableCameraSystem, Warning,
					TEXT("ApplyDelegateBindings: Source delegate '%s' (bound to '%s::%s') signature does not match target FDelegateProperty '%s' on node class '%s' (expected '%s'). Leaving target unbound to prevent ProcessEvent parameter-frame mismatch."),
					*Param.ParameterName.ToString(),
					*BoundObj->GetClass()->GetName(),
					*SourceDelegate->GetFunctionName().ToString(),
					*Param.TargetPinName.ToString(),
					*Node->GetClass()->GetName(),
					*DelegateProp->SignatureFunction->GetName());
				return false;
			}
			return true;
		};

		if (IsDelegateSignatureCompatible())
		{
			*DestDelegate = *SourceDelegate;
		}
		else
		{
			// Clear the target so a stale binding from a previous activation
			// doesn't survive the rejected new binding.
			DestDelegate->Unbind();
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

	// Check: Struct pin entries must have a non-null StructType. Both POD and
	// non-POD struct pin types are now supported by the runtime -- POD goes
	// through the byte-array Storage path, non-POD through the typed
	// FInstancedStruct slot pool (see TechDoc.md §7.2 "ParameterBlock /
	// RuntimeDataBlock POD-vs-typed dispatch"). What still fails is a Struct
	// pin type with no StructType set: the layout pass cannot register a
	// slot, the parser cannot parse a value, and the runtime would silently
	// drop the field. Flag those at authoring time.
	{
		auto ValidateStructTypeOrFlag = [this](
			EComposableCameraPinType PinType,
			UScriptStruct* StructType,
			FName Name,
			const TCHAR* Kind)
		{
			if (PinType != EComposableCameraPinType::Struct)
			{
				return;
			}
			if (!StructType)
			{
				FComposableCameraBuildMessage Msg;
				Msg.Severity = 2;
				Msg.Message = FText::Format(
					FText::FromString(TEXT("{0} '{1}' has Struct pin type but no StructType set.")),
					FText::FromString(Kind),
					FText::FromName(Name));
				BuildMessages.Add(Msg);
				BuildStatus = EComposableCameraBuildStatus::Failed;
			}
		};

		for (const FComposableCameraExposedParameter& Param : ExposedParameters)
		{
			ValidateStructTypeOrFlag(Param.PinType, Param.StructType, Param.ParameterName,
				TEXT("Exposed parameter"));
		}
		for (const FComposableCameraInternalVariable& Var : ExposedVariables)
		{
			ValidateStructTypeOrFlag(Var.VariableType, Var.StructType, Var.VariableName,
				TEXT("Exposed variable"));
		}
		for (const FComposableCameraInternalVariable& Var : InternalVariables)
		{
			ValidateStructTypeOrFlag(Var.VariableType, Var.StructType, Var.VariableName,
				TEXT("Internal variable"));
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

	// Subclass extension point — append any validation from the concrete asset
	// class (e.g. UComposableCameraPatchTypeAsset checks node Patch compatibility).
	// Each appended message's severity rolls BuildStatus up toward the more
	// severe category so the editor banner / per-node badges reflect it.
	{
		TArray<FComposableCameraBuildMessage> ExtraMessages;
		ValidateAdditional(ExtraMessages);
		for (const FComposableCameraBuildMessage& Msg : ExtraMessages)
		{
			BuildMessages.Add(Msg);
			if (Msg.Severity >= 2) // Error
			{
				BuildStatus = EComposableCameraBuildStatus::Failed;
			}
			else if (Msg.Severity == 1 && BuildStatus == EComposableCameraBuildStatus::Success) // Warning
			{
				BuildStatus = EComposableCameraBuildStatus::SuccessWithWarnings;
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
