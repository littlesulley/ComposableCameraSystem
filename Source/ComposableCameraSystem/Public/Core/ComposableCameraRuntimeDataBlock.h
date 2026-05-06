// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Concepts/StaticStructProvider.h"
#include "GameFramework/Actor.h"
#include "Nodes/ComposableCameraNodePinTypes.h"
#include "StructUtils/InstancedStruct.h"
#include "Templates/Models.h"
#include "ComposableCameraRuntimeDataBlock.generated.h"

class FReferenceCollector;

/**
 * Flat, contiguous memory block that holds all pin output values, exposed parameter values,
 * internal variable values, and per-instance input pin default values for a single camera
 * instance at runtime.
 *
 * The layout is computed once from the camera type asset's pin declarations, connections,
 * exposed parameters, internal variables, and per-instance pin overrides. All access is
 * offset-based for performance.
 *
 * Memory layout:
 *   [Output pin slots][Exposed parameter slots][Per-instance default slots][Internal variable slots]
 *
 * Each slot is aligned to the type's natural alignment. The per-instance default slots
 * mirror the authoring-layer FComposableCameraPinOverride::DefaultValueOverride values
 * (see Nodes/ComposableCameraNodePinTypes.h) pre-parsed into typed bytes so the per-frame
 * resolution path in TryResolveInputPin is a pure pointer lookup — no string parsing
 * on the hot path.
 */
USTRUCT()
struct COMPOSABLECAMERASYSTEM_API FComposableCameraRuntimeDataBlock
{
	GENERATED_BODY()

	/** Raw storage buffer. Allocated once during camera instantiation. POD pin
	 *  values live here at real byte offsets; non-POD struct values live in
	 *  StructSlots below at synthetic offsets >= StructSlotsOffsetBase. */
	TArray<uint8> Storage;

	/** Typed storage for non-POD struct slots (ExposedParameter / ExposedVariable /
	 *  InternalVariable / OutputPin / DefaultValue per-instance override of a
	 *  USTRUCT containing FString / FText / TArray / TMap / TSet / object refs /
	 *  delegates -- anything `IsBytewiseSafeStruct` rejects).
	 *
	 *  Each entry is owned (proper ctor / dtor) and GC-walked via
	 *  AddPropertyReferencesWithStructARO in AddReferencedObjects. The various
	 *  offset tables below (OutputPinOffsets, ExposedParameterOffsets, etc.)
	 *  store synthetic offsets >= StructSlotsOffsetBase for non-POD struct
	 *  entries; the dispatch in ReadValue / WriteValue / TryResolveInputPin
	 *  detects this and routes to StructSlots[Offset - StructSlotsOffsetBase]
	 *  instead of memcpying out of Storage.
	 *
	 *  Activation-time: BuildRuntimeDataLayout pre-allocates one slot per
	 *  non-POD struct entry via InitializeAs(StructType). Per-frame copy-in
	 *  (ApplyParameterBlock, WriteOutputPin, CopySlot) reuses the slot's
	 *  existing memory via CopyScriptStruct -- bounded one-time alloc when
	 *  embedded FString members grow, no alloc when they fit existing capacity. */
	TArray<FInstancedStruct> StructSlots;

	/** Synthetic offsets >= this value index into StructSlots; offsets < this
	 *  value are real byte offsets into Storage. INT32_MAX/2 is well outside
	 *  any plausible Storage size and leaves the same headroom for synthetic
	 *  range, so collisions are impossible without TotalSize crossing 1 GiB. */
	static constexpr int32 StructSlotsOffsetBase = TNumericLimits<int32>::Max() / 2;

	/** True if Offset addresses a non-POD struct slot in StructSlots. */
	FORCEINLINE bool IsStructSlotOffset(int32 Offset) const
	{
		return Offset >= StructSlotsOffsetBase;
	}

	/** Convert a synthetic offset to its StructSlots index. */
	FORCEINLINE int32 GetStructSlotIndex(int32 Offset) const
	{
		return Offset - StructSlotsOffsetBase;
	}

	/** Reserve a fresh struct slot pre-initialized for StructType, returning
	 *  the synthetic offset that should be stored in the relevant offset table
	 *  (ExposedParameterOffsets / OutputPinOffsets / etc.). Called once per
	 *  non-POD struct entry by BuildRuntimeDataLayout. */
	int32 RegisterStructSlot(const UScriptStruct* StructType)
	{
		const int32 Index = StructSlots.Emplace();
		if (StructType)
		{
			StructSlots[Index].InitializeAs(StructType);
		}
		return StructSlotsOffsetBase + Index;
	}

	/** Lookup: (NodeIndex, PinName) for OUTPUT pins → byte offset in Storage. */
	TMap<FComposableCameraPinKey, int32> OutputPinOffsets;

	/** Lookup: ExposedParameterName → byte offset in Storage. */
	TMap<FName, int32> ExposedParameterOffsets;

	/** Lookup: InternalVariableName → byte offset in Storage. */
	TMap<FName, int32> InternalVariableOffsets;

	/** Object-reference slots mirrored from raw storage for explicit GC collection. */
	TMap<int32, TObjectPtr<AActor>> ActorReferenceSlots;

	/** Object-reference slots mirrored from raw storage for explicit GC collection. */
	TMap<int32, TObjectPtr<UObject>> ObjectReferenceSlots;

	/**
	 * Connection table: for each input pin, the offset of its source data.
	 * Key = (TargetNodeIndex, TargetPinName), Value = offset in Storage where the
	 * source output pin wrote its data.
	 */
	TMap<FComposableCameraPinKey, int32> InputPinSourceOffsets;

	/**
	 * Exposure table: for each exposed input pin, the offset of the parameter slot.
	 * Key = (TargetNodeIndex, TargetPinName), Value = offset of the exposed parameter in Storage.
	 */
	TMap<FComposableCameraPinKey, int32> ExposedInputPinOffsets;

	/**
	 * Per-instance default-value table: for each input pin that has an authored
	 * FComposableCameraPinOverride::DefaultValueOverride (see
	 * Nodes/ComposableCameraNodePinTypes.h), the offset of the slot holding the
	 * pre-parsed typed bytes. Key = (TargetNodeIndex, PinName), Value = offset
	 * in Storage.
	 *
	 * This is ranked below InputPinSourceOffsets (wired) and ExposedInputPinOffsets
	 * (exposed-as-parameter) by TryResolveInputPin. It exists so that per-frame
	 * default resolution is a pointer-lookup / memcpy instead of a string-parse,
	 * honoring the "no hot-path allocations" rule.
	 *
	 * Pins without an authored override are simply absent from this map; their
	 * default is resolved by the node's own class-level fallback (e.g. a UPROPERTY
	 * on the node template) when TryResolveInputPin returns false.
	 */
	TMap<FComposableCameraPinKey, int32> DefaultValueOffsets;

	/** Total allocated size. */
	int32 TotalSize = 0;

	/** Read a typed value from the storage at the given byte offset.
	 *  POD path: memcpy out of Storage.
	 *  Non-POD struct path (T is a USTRUCT and Offset >= StructSlotsOffsetBase):
	 *  CopyScriptStruct out of the typed slot in StructSlots. */
	template<typename T>
	T ReadValue(int32 Offset) const
	{
		if constexpr (TModels_V<CStaticStructProvider, T>)
		{
			if (IsStructSlotOffset(Offset))
			{
				const int32 Index = GetStructSlotIndex(Offset);
				check(StructSlots.IsValidIndex(Index));
				const FInstancedStruct& Slot = StructSlots[Index];
				check(Slot.IsValid() && Slot.GetScriptStruct() == T::StaticStruct());
				T Result;
				T::StaticStruct()->CopyScriptStruct(&Result, Slot.GetMemory());
				return Result;
			}
		}
		check(Offset >= 0 && Offset + static_cast<int32>(sizeof(T)) <= Storage.Num());
		T Result;
		FMemory::Memcpy(&Result, Storage.GetData() + Offset, sizeof(T));
		return Result;
	}

	/** Write a typed value to the storage at the given byte offset.
	 *  POD path: memcpy into Storage.
	 *  Non-POD struct path: CopyScriptStruct into the existing struct slot's
	 *  owned memory -- no allocation unless an embedded FString grows beyond
	 *  its existing capacity (see TechDoc.md §7.2 alloc characteristic). */
	template<typename T>
	void WriteValue(int32 Offset, const T& Value)
	{
		if constexpr (TModels_V<CStaticStructProvider, T>)
		{
			if (IsStructSlotOffset(Offset))
			{
				const int32 Index = GetStructSlotIndex(Offset);
				check(StructSlots.IsValidIndex(Index));
				FInstancedStruct& Slot = StructSlots[Index];
				check(Slot.IsValid() && Slot.GetScriptStruct() == T::StaticStruct());
				T::StaticStruct()->CopyScriptStruct(Slot.GetMutableMemory(), &Value);
				return;
			}
		}
		check(Offset >= 0 && Offset + static_cast<int32>(sizeof(T)) <= Storage.Num());
		FMemory::Memcpy(Storage.GetData() + Offset, &Value, sizeof(T));
		RefreshReferenceSlot(Offset);
	}

	/** Read a value for a specific output pin. */
	template<typename T>
	T ReadOutputPin(int32 NodeIndex, FName PinName) const
	{
		const FComposableCameraPinKey Key{ NodeIndex, PinName };
		const int32* Offset = OutputPinOffsets.Find(Key);
		check(Offset);
		return ReadValue<T>(*Offset);
	}

	/** Write a value for a specific output pin. */
	template<typename T>
	void WriteOutputPin(int32 NodeIndex, FName PinName, const T& Value)
	{
		const FComposableCameraPinKey Key{ NodeIndex, PinName };
		const int32* Offset = OutputPinOffsets.Find(Key);
		check(Offset);
		WriteValue<T>(*Offset, Value);
	}

	/**
	 * Resolve an input pin's value. Checks in order:
	 * 1. Wired connection (InputPinSourceOffsets)
	 * 2. Exposed parameter (ExposedInputPinOffsets)
	 * 3. Per-instance default override (DefaultValueOffsets) — authoring-layer
	 *    FComposableCameraPinOverride::DefaultValueOverride, pre-parsed by
	 *    BuildRuntimeDataLayout.
	 * 4. Returns false if none of the above are found. Callers with a class-level
	 *    fallback (e.g. a UPROPERTY on the node template) should read it in the
	 *    false branch; see UComposableCameraFieldOfViewNode::OnTickNode for the
	 *    canonical pattern.
	 */
	template<typename T>
	bool TryResolveInputPin(int32 NodeIndex, FName PinName, T& OutValue) const
	{
		const FComposableCameraPinKey Key{ NodeIndex, PinName };

		// 1. Check wired connection
		if (const int32* SourceOffset = InputPinSourceOffsets.Find(Key))
		{
			OutValue = ReadValue<T>(*SourceOffset);
			return true;
		}

		// 2. Check exposed parameter
		if (const int32* ParamOffset = ExposedInputPinOffsets.Find(Key))
		{
			OutValue = ReadValue<T>(*ParamOffset);
			return true;
		}

		// 3. Check per-instance default override (authored in Details on the graph node)
		if (const int32* DefaultOffset = DefaultValueOffsets.Find(Key))
		{
			OutValue = ReadValue<T>(*DefaultOffset);
			return true;
		}

		return false;
	}

	/** Resolve an input pin to its source slot offset using the same three-tier
	 *  priority as TryResolveInputPin (wired -> exposed -> per-instance default),
	 *  but without copying the value out -- useful for non-templated paths
	 *  (auto-resolve Struct case, struct subobject pin dispatch) that need to
	 *  decide between byte storage and FInstancedStruct slot at runtime.
	 *  Returns true and writes OutOffset when a slot is found. */
	bool ResolveInputPinOffset(int32 NodeIndex, FName PinName, int32& OutOffset) const
	{
		const FComposableCameraPinKey Key{ NodeIndex, PinName };
		if (const int32* SourceOffset = InputPinSourceOffsets.Find(Key))
		{
			OutOffset = *SourceOffset;
			return true;
		}
		if (const int32* ParamOffset = ExposedInputPinOffsets.Find(Key))
		{
			OutOffset = *ParamOffset;
			return true;
		}
		if (const int32* DefaultOffset = DefaultValueOffsets.Find(Key))
		{
			OutOffset = *DefaultOffset;
			return true;
		}
		return false;
	}

	/** Read an internal variable by name. */
	template<typename T>
	T ReadInternalVariable(FName VariableName) const
	{
		const int32* Offset = InternalVariableOffsets.Find(VariableName);
		check(Offset);
		return ReadValue<T>(*Offset);
	}

	/** Write an internal variable by name. */
	template<typename T>
	void WriteInternalVariable(FName VariableName, const T& Value)
	{
		const int32* Offset = InternalVariableOffsets.Find(VariableName);
		check(Offset);
		WriteValue<T>(*Offset, Value);
	}

	/** Check if a specific internal variable exists. */
	bool HasInternalVariable(FName VariableName) const
	{
		return InternalVariableOffsets.Contains(VariableName);
	}

	/** Copy raw bytes from one slot to another within the same storage.
	 *  Used by the exec-chain SetVariable dispatch to transfer a source node's
	 *  output pin value into an internal variable slot without knowing the
	 *  concrete C++ type at compile time.
	 *
	 *  Three cases: both POD (memcpy), both non-POD struct (CopyScriptStruct
	 *  through the slot's owned memory, the struct types must match), or
	 *  mismatched -- the layout builder must never emit a connection between
	 *  pins of different storage classes, so a mismatch is a bug. */
	void CopySlot(int32 SourceOffset, int32 TargetOffset, int32 NumBytes)
	{
		const bool bSourceIsStruct = IsStructSlotOffset(SourceOffset);
		const bool bTargetIsStruct = IsStructSlotOffset(TargetOffset);
		check(bSourceIsStruct == bTargetIsStruct);

		if (bSourceIsStruct)
		{
			const int32 SrcIndex = GetStructSlotIndex(SourceOffset);
			const int32 DstIndex = GetStructSlotIndex(TargetOffset);
			check(StructSlots.IsValidIndex(SrcIndex));
			check(StructSlots.IsValidIndex(DstIndex));
			const FInstancedStruct& Src = StructSlots[SrcIndex];
			FInstancedStruct& Dst = StructSlots[DstIndex];
			check(Src.IsValid() && Dst.IsValid());
			check(Src.GetScriptStruct() == Dst.GetScriptStruct());
			Dst.GetScriptStruct()->CopyScriptStruct(Dst.GetMutableMemory(), Src.GetMemory());
			return;
		}

		check(NumBytes > 0);
		check(SourceOffset >= 0 && SourceOffset + NumBytes <= Storage.Num());
		check(TargetOffset >= 0 && TargetOffset + NumBytes <= Storage.Num());
		FMemory::Memcpy(Storage.GetData() + TargetOffset, Storage.GetData() + SourceOffset, NumBytes);
		RefreshReferenceSlot(TargetOffset);
	}

	/** Direct access to the FInstancedStruct backing a non-POD struct slot.
	 *  Used by auto-resolve / subobject-pin code paths whose dispatch happens
	 *  on a runtime EComposableCameraPinType value (not a compile-time T) --
	 *  the templated ReadValue<T> path is preferred when T is known. Asserts
	 *  the offset is in fact a struct slot. */
	const FInstancedStruct& GetStructSlotChecked(int32 Offset) const
	{
		check(IsStructSlotOffset(Offset));
		const int32 Index = GetStructSlotIndex(Offset);
		check(StructSlots.IsValidIndex(Index));
		return StructSlots[Index];
	}

	FInstancedStruct& GetStructSlotMutableChecked(int32 Offset)
	{
		check(IsStructSlotOffset(Offset));
		const int32 Index = GetStructSlotIndex(Offset);
		check(StructSlots.IsValidIndex(Index));
		return StructSlots[Index];
	}

	void RegisterReferenceSlot(EComposableCameraPinType PinType, int32 Offset);
	void RefreshReferenceSlot(int32 Offset);
	void RefreshAllReferenceSlots();
	void AddReferencedObjects(FReferenceCollector& Collector);

	/** Check if storage has been allocated. */
	bool IsValid() const
	{
		return TotalSize > 0 && Storage.Num() > 0;
	}

	/** Zero-initialize all storage. Called at allocation time. */
	void ZeroInitialize()
	{
		if (Storage.Num() > 0)
		{
			FMemory::Memzero(Storage.GetData(), Storage.Num());
			RefreshAllReferenceSlots();
		}
	}
};
