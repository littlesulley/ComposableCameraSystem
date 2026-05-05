// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Nodes/ComposableCameraNodePinTypes.h"
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

	/** Raw storage buffer. Allocated once during camera instantiation. */
	TArray<uint8> Storage;

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

	/** Read a typed value from the storage at the given byte offset. */
	template<typename T>
	T ReadValue(int32 Offset) const
	{
		check(Offset >= 0 && Offset + static_cast<int32>(sizeof(T)) <= Storage.Num());
		T Result;
		FMemory::Memcpy(&Result, Storage.GetData() + Offset, sizeof(T));
		return Result;
	}

	/** Write a typed value to the storage at the given byte offset. */
	template<typename T>
	void WriteValue(int32 Offset, const T& Value)
	{
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
	 *  concrete C++ type at compile time. */
	void CopySlot(int32 SourceOffset, int32 TargetOffset, int32 NumBytes)
	{
		check(NumBytes > 0);
		check(SourceOffset >= 0 && SourceOffset + NumBytes <= Storage.Num());
		check(TargetOffset >= 0 && TargetOffset + NumBytes <= Storage.Num());
		FMemory::Memcpy(Storage.GetData() + TargetOffset, Storage.GetData() + SourceOffset, NumBytes);
		RefreshReferenceSlot(TargetOffset);
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
