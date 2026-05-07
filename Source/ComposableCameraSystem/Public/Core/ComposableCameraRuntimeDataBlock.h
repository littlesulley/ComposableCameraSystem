// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraSystemModule.h" // LogComposableCameraSystem (used by inline TryCopySlot)
#include "Concepts/StaticStructProvider.h"
#include "GameFramework/Actor.h"
#include "Nodes/ComposableCameraNodePinTypes.h"
#include "StructUtils/InstancedStruct.h"
#include "Templates/Models.h"
#include <type_traits>
#include "ComposableCameraRuntimeDataBlock.generated.h"

class FReferenceCollector;

namespace UE::ComposableCameras::Private
{
	/** Always-false dependent constant — needed because `static_assert(false, …)`
	 *  in a discarded `if constexpr` branch fires unconditionally on some
	 *  compilers (DR2518 only mandates per-instantiation evaluation in C++23).
	 *  The dependent form defers evaluation until the enclosing template is
	 *  actually instantiated for the unsupported T. */
	template<typename> inline constexpr bool always_false_v = false;

	/** Compile-time map from a templated value type T to the
	 *  `EComposableCameraPinType` the data block tracks for that type.
	 *  Used by `ReadValue<T>` / `WriteValue<T>` to compare against the
	 *  recorded `FSlotShape::PinType` for a given offset and refuse
	 *  cross-shape access. The order of `if constexpr` branches matters:
	 *  `AActor*` matches both AActor and UObject base checks, so the
	 *  AActor branch must come first to map it to `Actor`, not `Object`.
	 *
	 *  Unsupported T (e.g., `uint32`, raw `enum class`, user 4 B POD without
	 *  `TModels_V<CStaticStructProvider>`) `static_assert`s instead of
	 *  silently degrading to `Float`. The earlier Float-fallback let any
	 *  4-byte T pass the downstream `Shape->Size` check against a `Float`
	 *  slot — `ReadValue<uint32>(FloatOffset)` would silently reinterpret
	 *  the float bytes as `uint32`, same class of type-pun the slot-shape
	 *  work was meant to close. Forcing a compile error makes the caller
	 *  pick a supported T (`int32` for signed 32-bit, `int64` for enums —
	 *  which is the canonical enum storage width — etc.). */
	template<typename T>
	constexpr EComposableCameraPinType ExpectedPinTypeFor()
	{
		if constexpr (std::is_same_v<T, bool>)         return EComposableCameraPinType::Bool;
		else if constexpr (std::is_same_v<T, int32>)   return EComposableCameraPinType::Int32;
		else if constexpr (std::is_same_v<T, float>)   return EComposableCameraPinType::Float;
		else if constexpr (std::is_same_v<T, double>)  return EComposableCameraPinType::Double;
		else if constexpr (std::is_same_v<T, FVector2D>) return EComposableCameraPinType::Vector2D;
		else if constexpr (std::is_same_v<T, FVector>)   return EComposableCameraPinType::Vector3D;
		else if constexpr (std::is_same_v<T, FVector4>)  return EComposableCameraPinType::Vector4;
		else if constexpr (std::is_same_v<T, FRotator>)  return EComposableCameraPinType::Rotator;
		else if constexpr (std::is_same_v<T, FTransform>) return EComposableCameraPinType::Transform;
		else if constexpr (std::is_same_v<T, FName>)     return EComposableCameraPinType::Name;
		else if constexpr (std::is_same_v<T, int64>)     return EComposableCameraPinType::Enum;
		else if constexpr (std::is_pointer_v<T>
			&& std::is_base_of_v<AActor, std::remove_pointer_t<T>>)        return EComposableCameraPinType::Actor;
		else if constexpr (std::is_pointer_v<T>
			&& std::is_base_of_v<UObject, std::remove_pointer_t<T>>)       return EComposableCameraPinType::Object;
		else if constexpr (TModels_V<CStaticStructProvider, T>)            return EComposableCameraPinType::Struct;
		else
		{
			static_assert(always_false_v<T>,
				"ExpectedPinTypeFor<T>: T is not a supported pin storage type. "
				"Use one of: bool / int32 / float / double / FVector{2D,3D,4} / "
				"FRotator / FTransform / FName / int64 (for enums — narrow at the "
				"call site) / AActor*-derived / UObject*-derived / a USTRUCT.");
			return EComposableCameraPinType::Float; // unreachable; shuts up the compiler
		}
	}
}

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

	/** Per-slot shape metadata: pin type + size in bytes. Populated at
	 *  layout time by `AllocateSlot` for every byte-storage slot AND every
	 *  struct-slot synthetic offset, then consulted by `ReadValue<T>` and
	 *  `WriteValue<T>` to refuse cross-shape access before the memcpy
	 *  fires.
	 *
	 *  The earlier ref-slot directionality guard (eleventh-pass P0) only
	 *  caught one axis of the cross-shape problem — UObject-pointer T
	 *  vs ref-slot membership. It did not catch cross-shape access where
	 *  neither side involves a ref slot, e.g.
	 *
	 *    `WriteValue<FVector>(FloatPinOffset, V)`   // 12 B into 4 B
	 *    `WriteValue<FTransform>(BoolPinOffset, T)` // 64 B into 1 B
	 *    `WriteValue<double>(IntVarOffset, D)`      // 8 B into 4 B
	 *
	 *  Each of these passes the `Offset + sizeof(T) <= Storage.Num()`
	 *  bounds check (the slot is not the LAST slot in Storage), runs the
	 *  oversized memcpy, and clobbers adjacent slots' bytes. If any
	 *  clobbered adjacent slot is an Object/Actor reference slot, a
	 *  later read of that slot reads polluted bytes as a UObject pointer
	 *  and the IsA virtual call dereferences garbage memory. Strict
	 *  shape match (`PinType == ExpectedFor<T>` AND `Size == sizeof(T)`)
	 *  blocks every cross-shape direction at the first templated access. */
	struct FSlotShape
	{
		EComposableCameraPinType PinType = EComposableCameraPinType::Float;
		int32 Size = 0;

		/** Only meaningful when `PinType == Struct`. The exact `UScriptStruct`
		 *  the slot was allocated for. Templated `ReadValue<T>` /
		 *  `WriteValue<T>` must verify `T::StaticStruct() == StructType`
		 *  before letting `CopyScriptStruct` touch the slot — the struct-
		 *  slot offset table can deliver a wrong-shape T if it ever
		 *  desyncs (stale type asset, hand-edited connection, asset saved
		 *  before validation existed), and `CopyScriptStruct` walks T's
		 *  property layout against whatever bytes the slot actually holds,
		 *  which is heap corruption / GC pollution territory under
		 *  non-POD struct fields (FString operator=, UObject ref
		 *  overwrites, embedded TArray copy). The previous-pass `check()`
		 *  guards before this field were debug-only safety nets that
		 *  stripped in Shipping; this metadata promotes the same check
		 *  to a real return-on-mismatch in every build. */
		TObjectPtr<UScriptStruct> StructType = nullptr;
	};
	TMap<int32, FSlotShape> SlotShapes;

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

	/** Indices into the type asset's `FullExecChain` whose `SetVariable`
	 *  entries failed type validation at activation time and must be skipped
	 *  by the runtime SetVariable handler.
	 *
	 *  Without this gate, a stale exec entry whose source pin's type no
	 *  longer matches its target variable (variable retyped after the Set
	 *  node was wired, asset saved before validation existed, etc.) would
	 *  reach `CopySlot(SourceOffset, VarOffset, VariableSlotSize)` — and the
	 *  POD memcpy branch reads `VariableSlotSize` bytes from `SourceOffset`
	 *  regardless of how many bytes actually live in the source slot. For a
	 *  Float source (4B) → Actor variable (8B), the memcpy reads 4 bytes
	 *  past the float slot, then `RefreshReferenceSlot` reinterprets those 8
	 *  bytes as `AActor*` and registers the resulting garbage pointer with
	 *  the GC mirror.  GC can crash on the next sweep.
	 *
	 *  Entries land in this set during `BuildRuntimeDataLayout` Phase 2's
	 *  validation pass; the runtime check is one `TSet::Contains(EntryIdx)`
	 *  per SetVariable entry per tick, with the typical empty set folding to
	 *  a hashed-empty-bucket fast path. */
	TSet<int32> InvalidSetVariableExecEntries;

	/** Same as `InvalidSetVariableExecEntries`, but for the type asset's
	 *  `ComputeFullExecChain` — the parallel exec chain that runs at
	 *  `ExecuteBeginPlay` time over compute nodes. */
	TSet<int32> InvalidSetVariableComputeExecEntries;

	/** Total allocated size. */
	int32 TotalSize = 0;

	/** Read a typed value from the storage at the given byte offset.
	 *  POD path: memcpy out of Storage.
	 *  Non-POD struct path (T is a USTRUCT and Offset >= StructSlotsOffsetBase):
	 *  CopyScriptStruct out of the typed slot in StructSlots.
	 *
	 *  UObject-derived pointer T (e.g. `UCurveFloat*`, `AActor*`,
	 *  `UComposableCameraTypeAsset*`): after the byte-storage memcpy, the
	 *  resolved pointer is checked against `T::StaticClass()` via `IsA`.
	 *  Mismatch → returns nullptr. The data block stores object/actor
	 *  pointers class-erased (every Object/Actor pin collapses to a single
	 *  `EComposableCameraPinType::Object` or `Actor`), so a stale asset, a
	 *  hand-edited connection, or a Blueprint-wildcard mismatch could
	 *  deliver a wrong-class instance into the slot. The
	 *  `AssignObjectPropertyChecked` helper in `ResolveAllInputPins`
	 *  guards the auto-resolve UPROPERTY-write path; this branch
	 *  symmetrically guards the explicit-template-read path used by node
	 *  authors calling `GetInputPinValue<UCurveFloat*>` directly — without
	 *  it, the wrong-class pointer flows back as a typed `T` and the next
	 *  member access reads a vtable / fields of a different layout and
	 *  crashes. Returning nullptr surfaces the error as a clean nullcheck
	 *  failure on the caller side. */
	template<typename T>
	T ReadValue(int32 Offset) const
	{
		if constexpr (TModels_V<CStaticStructProvider, T>)
		{
			if (IsStructSlotOffset(Offset))
			{
				// Strict shape gate before CopyScriptStruct — refuse on any
				// of: unknown offset, wrong PinType, wrong StructType. The
				// prior `check()`s were debug-only safety nets that stripped
				// in Shipping, leaving a stale offset table free to point T
				// at a wrong-struct slot and run T's property layout walk
				// against bytes of a different struct (heap corruption / GC
				// pollution). Returning `T{}` surfaces the error as a clean
				// zero-init on the caller side.
				const FSlotShape* Shape = SlotShapes.Find(Offset);
				if (!Shape
					|| Shape->PinType != EComposableCameraPinType::Struct
					|| Shape->StructType != T::StaticStruct())
				{
					return T{};
				}

				const int32 Index = GetStructSlotIndex(Offset);
				if (!StructSlots.IsValidIndex(Index))
				{
					return T{};
				}
				const FInstancedStruct& Slot = StructSlots[Index];
				if (!Slot.IsValid() || Slot.GetScriptStruct() != T::StaticStruct())
				{
					return T{};
				}
				T Result;
				T::StaticStruct()->CopyScriptStruct(&Result, Slot.GetMemory());
				return Result;
			}
		}
		// Slot-shape strict validation. Looks up `FSlotShape{PinType, Size}`
		// recorded at layout time and refuses any cross-shape access:
		//
		//   * Unknown offset (no shape record) → refuse: caller is
		//     accessing a slot the layout never allocated.
		//   * PinType mismatch (e.g., `T = AActor*` against a Float slot,
		//     or `T = float` against an Object slot) → refuse: would
		//     either misinterpret bytes or pollute the GC mirror via
		//     RefreshReferenceSlot.
		//   * Size mismatch (e.g., `T = FVector` 12 B against a Float
		//     slot 4 B, or `T = FTransform` against a Bool slot) →
		//     refuse: would memcpy past the slot and clobber adjacent
		//     storage.
		//
		// All three cases short-circuit before any memory read so a
		// caller writing `GetInputPinValue<FVector>("FloatPin")` (typo or
		// stale pin name) gets `T{}` (zero FVector) rather than poisoned
		// adjacent bytes interpreted as a vector.
		{
			const FSlotShape* Shape = SlotShapes.Find(Offset);
			constexpr EComposableCameraPinType ExpectedType =
				UE::ComposableCameras::Private::ExpectedPinTypeFor<T>();
			if (!Shape || Shape->PinType != ExpectedType
				|| Shape->Size != static_cast<int32>(sizeof(T)))
			{
				return T{};
			}
			// Symmetric struct-type defense for POD byte-storage struct
			// slots (FVector / FRotator / user POD USTRUCTs). Same-size
			// cross-struct reads (e.g. T = FCustom12B against an FVector
			// slot) would otherwise pass the size + PinType check; the
			// recorded StructType pins down the exact USTRUCT identity.
			if constexpr (ExpectedType == EComposableCameraPinType::Struct)
			{
				if (Shape->StructType != T::StaticStruct())
				{
					return T{};
				}
			}
		}

		// Hard byte-bounds check before the memcpy — `check()` strips in
		// Shipping, leaving an out-of-range read free to walk past the end
		// of `Storage` and read whatever lives in adjacent heap memory.
		// In normal operation `SlotShapes` is built in lockstep with
		// `Storage`, so `Offset + sizeof(T) <= Storage.Num()` is implied
		// by the shape match — but a stale layout (asset edited to shrink
		// a slot, future code path that mutates `Storage` without
		// updating `SlotShapes`, public-member mutation by a debug tool)
		// could leave a recorded shape pointing past the actual buffer.
		// Refuse with `T{}` rather than reading garbage; logging is
		// deliberately omitted on this path because the shape gates
		// already covered the high-signal cases — this is the
		// belt-and-braces backstop.
		if (Offset < 0 || Offset + static_cast<int32>(sizeof(T)) > Storage.Num())
		{
			return T{};
		}
		T Result;
		FMemory::Memcpy(&Result, Storage.GetData() + Offset, sizeof(T));

		// Class-constraint guard for UObject-derived pointer reads. The
		// shape guard above already ensured we're reading from a real
		// Object/Actor slot, so `Result` is either nullptr or a properly-
		// stored UObject pointer (written via the matching WriteValue
		// shape guard). The remaining attack: the slot legitimately holds
		// a UObject of the wrong subclass (e.g., a `UCurveVector` in a
		// slot the consumer reads as `UCurveFloat*`). IsA filters that
		// without risking a deref on a random pointer.
		if constexpr (std::is_pointer_v<T>
			&& std::is_base_of_v<UObject, std::remove_pointer_t<T>>)
		{
			if (Result && !Result->IsA(std::remove_pointer_t<T>::StaticClass()))
			{
				return nullptr;
			}
		}

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
				// Strict shape gate before CopyScriptStruct — see ReadValue
				// for the same rationale. Refuse on unknown offset / wrong
				// PinType / wrong StructType; silently no-op rather than
				// stamping a wrong-shape struct's property layout into a
				// slot of a different struct.
				const FSlotShape* Shape = SlotShapes.Find(Offset);
				if (!Shape
					|| Shape->PinType != EComposableCameraPinType::Struct
					|| Shape->StructType != T::StaticStruct())
				{
					return;
				}

				const int32 Index = GetStructSlotIndex(Offset);
				if (!StructSlots.IsValidIndex(Index))
				{
					return;
				}
				FInstancedStruct& Slot = StructSlots[Index];
				if (!Slot.IsValid() || Slot.GetScriptStruct() != T::StaticStruct())
				{
					return;
				}
				T::StaticStruct()->CopyScriptStruct(Slot.GetMutableMemory(), &Value);
				return;
			}
		}

		// Strict shape validation — see ReadValue for the same rationale.
		// Refuse: (1) unknown offset, (2) PinType mismatch (would either
		// misinterpret bytes or pollute the GC mirror through
		// RefreshReferenceSlot), (3) size mismatch (would memcpy past the
		// slot and clobber adjacent storage).
		{
			const FSlotShape* Shape = SlotShapes.Find(Offset);
			constexpr EComposableCameraPinType ExpectedType =
				UE::ComposableCameras::Private::ExpectedPinTypeFor<T>();
			if (!Shape || Shape->PinType != ExpectedType
				|| Shape->Size != static_cast<int32>(sizeof(T)))
			{
				return;
			}
			// Symmetric struct-type defense — see ReadValue.
			if constexpr (ExpectedType == EComposableCameraPinType::Struct)
			{
				if (Shape->StructType != T::StaticStruct())
				{
					return;
				}
			}
		}

		// Hard byte-bounds check before the memcpy — same rationale as
		// `ReadValue`. The trailing `RefreshReferenceSlot` is the real
		// hazard on this side: an out-of-range memcpy could clobber
		// adjacent ref slots' bytes, and `RefreshReferenceSlot` would
		// then re-read those polluted bytes as `AActor*` / `UObject*`
		// and register garbage with the GC mirror — same crash class
		// the eleventh-pass review fixed for cross-shape access. No-op
		// silently rather than poison the GC mirror.
		if (Offset < 0 || Offset + static_cast<int32>(sizeof(T)) > Storage.Num())
		{
			return;
		}
		FMemory::Memcpy(Storage.GetData() + Offset, &Value, sizeof(T));
		RefreshReferenceSlot(Offset);
	}

	/** Read a value for a specific output pin.
	 *
	 *  Real `if`-with-return on the lookup miss — `check()` strips in
	 *  Shipping, so a typo in a custom C++ node's pin name or a stale
	 *  output pin (asset edited to remove the pin but the C++ node
	 *  hasn't been rebuilt) would null-deref the missing offset. The
	 *  low-level `ReadValue<T>` is now hard-guarded too, but this
	 *  wrapper is the one node authors call, so the failure must be
	 *  caught at the wrapper boundary. Returning `T{}` matches the
	 *  ReadValue degradation pattern. */
	template<typename T>
	T ReadOutputPin(int32 NodeIndex, FName PinName) const
	{
		const FComposableCameraPinKey Key{ NodeIndex, PinName };
		const int32* Offset = OutputPinOffsets.Find(Key);
		if (!Offset)
		{
			return T{};
		}
		return ReadValue<T>(*Offset);
	}

	/** Write a value for a specific output pin. See `ReadOutputPin`. */
	template<typename T>
	void WriteOutputPin(int32 NodeIndex, FName PinName, const T& Value)
	{
		const FComposableCameraPinKey Key{ NodeIndex, PinName };
		const int32* Offset = OutputPinOffsets.Find(Key);
		if (!Offset)
		{
			return;
		}
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
		// Real `if`-with-return on the lookup miss — same Shipping-strips-
		// check rationale as `ReadOutputPin`. A renamed / removed variable
		// in the asset that a custom C++ node still references would
		// otherwise null-deref the missing offset.
		const int32* Offset = InternalVariableOffsets.Find(VariableName);
		if (!Offset)
		{
			return T{};
		}
		return ReadValue<T>(*Offset);
	}

	/** Write an internal variable by name. See `ReadInternalVariable`. */
	template<typename T>
	void WriteInternalVariable(FName VariableName, const T& Value)
	{
		const int32* Offset = InternalVariableOffsets.Find(VariableName);
		if (!Offset)
		{
			return;
		}
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
	 *  Returns true on success, false on any shape / index / bounds mismatch.
	 *  The previous void overload guarded mismatches with `check()` only —
	 *  Shipping strips those, leaving a stale offset table from a legacy
	 *  asset (variable retyped after wiring, exec entry that escaped the
	 *  Phase 2 invalid-set, asset round-trip race) free to drive
	 *  `CopyScriptStruct` into a wrong-type struct slot or `memcpy` past a
	 *  POD slot's end. The shape-table look-up here is one TMap::Find per
	 *  endpoint per copy and turns the failure into a deterministic skip
	 *  with a runtime warning, in every build. */
	bool TryCopySlot(int32 SourceOffset, int32 TargetOffset, int32 NumBytes)
	{
		const FSlotShape* SrcShape = SlotShapes.Find(SourceOffset);
		const FSlotShape* DstShape = SlotShapes.Find(TargetOffset);
		if (!SrcShape || !DstShape)
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("TryCopySlot refused: unknown offset (Src=%d, Dst=%d)."),
				SourceOffset, TargetOffset);
			return false;
		}

		const bool bSourceIsStruct = IsStructSlotOffset(SourceOffset);
		const bool bTargetIsStruct = IsStructSlotOffset(TargetOffset);
		if (bSourceIsStruct != bTargetIsStruct)
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("TryCopySlot refused: storage-class mismatch (Src %s, Dst %s)."),
				bSourceIsStruct ? TEXT("struct") : TEXT("POD"),
				bTargetIsStruct ? TEXT("struct") : TEXT("POD"));
			return false;
		}

		if (bSourceIsStruct)
		{
			// Both endpoints are non-POD struct slots — verify the
			// struct types match in BOTH SlotShapes (recorded at layout
			// time) AND the live FInstancedStruct (the actual typed
			// memory). Mismatch on either axis means CopyScriptStruct
			// would walk one struct's property layout against another's
			// memory — heap corruption / GC pollution under embedded
			// FString / TArray / object refs.
			if (SrcShape->StructType != DstShape->StructType
				|| SrcShape->StructType == nullptr)
			{
				UE_LOG(LogComposableCameraSystem, Warning,
					TEXT("TryCopySlot refused: struct-type shape mismatch (Src=%s, Dst=%s)."),
					*GetNameSafe(SrcShape->StructType), *GetNameSafe(DstShape->StructType));
				return false;
			}

			const int32 SrcIndex = GetStructSlotIndex(SourceOffset);
			const int32 DstIndex = GetStructSlotIndex(TargetOffset);
			if (!StructSlots.IsValidIndex(SrcIndex) || !StructSlots.IsValidIndex(DstIndex))
			{
				UE_LOG(LogComposableCameraSystem, Warning,
					TEXT("TryCopySlot refused: struct-slot index out of range (SrcIdx=%d, DstIdx=%d, Num=%d)."),
					SrcIndex, DstIndex, StructSlots.Num());
				return false;
			}
			const FInstancedStruct& Src = StructSlots[SrcIndex];
			FInstancedStruct& Dst = StructSlots[DstIndex];
			if (!Src.IsValid() || !Dst.IsValid()
				|| Src.GetScriptStruct() != Dst.GetScriptStruct()
				|| Src.GetScriptStruct() != SrcShape->StructType)
			{
				UE_LOG(LogComposableCameraSystem, Warning,
					TEXT("TryCopySlot refused: live struct-slot type drift."));
				return false;
			}
			Dst.GetScriptStruct()->CopyScriptStruct(Dst.GetMutableMemory(), Src.GetMemory());
			return true;
		}

		// POD path — verify shape (PinType + Size) matches and the
		// declared NumBytes does not exceed the recorded slot size on
		// either endpoint. A stale exec entry whose `VariableSlotSize`
		// outsizes the actual slots would otherwise stomp adjacent
		// storage and corrupt the GC mirror via the trailing
		// RefreshReferenceSlot.
		if (SrcShape->PinType != DstShape->PinType
			|| SrcShape->Size != DstShape->Size)
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("TryCopySlot refused: POD shape mismatch (Src PinType=%d Size=%d, Dst PinType=%d Size=%d)."),
				static_cast<int32>(SrcShape->PinType), SrcShape->Size,
				static_cast<int32>(DstShape->PinType), DstShape->Size);
			return false;
		}
		if (NumBytes <= 0 || NumBytes != SrcShape->Size)
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("TryCopySlot refused: NumBytes (%d) does not match slot size (%d)."),
				NumBytes, SrcShape->Size);
			return false;
		}
		if (SourceOffset < 0 || SourceOffset + NumBytes > Storage.Num()
			|| TargetOffset < 0 || TargetOffset + NumBytes > Storage.Num())
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("TryCopySlot refused: byte-bounds violation (Src=%d, Dst=%d, N=%d, StorageNum=%d)."),
				SourceOffset, TargetOffset, NumBytes, Storage.Num());
			return false;
		}

		FMemory::Memcpy(Storage.GetData() + TargetOffset, Storage.GetData() + SourceOffset, NumBytes);
		RefreshReferenceSlot(TargetOffset);
		return true;
	}

	/** Backwards-compatible wrapper that discards the success bool. New
	 *  callers should prefer `TryCopySlot` and react to a `false` result
	 *  rather than rely on `check()` to abort under Debug only. */
	void CopySlot(int32 SourceOffset, int32 TargetOffset, int32 NumBytes)
	{
		(void)TryCopySlot(SourceOffset, TargetOffset, NumBytes);
	}

	/** Direct access to the FInstancedStruct backing a non-POD struct slot.
	 *  Used by auto-resolve / subobject-pin code paths whose dispatch happens
	 *  on a runtime EComposableCameraPinType value (not a compile-time T) --
	 *  the templated ReadValue<T> path is preferred when T is known.
	 *
	 *  PREFERRED: `TryGetStructSlot(Offset, ExpectedStruct)` —
	 *  failure-aware, returns nullptr on bad offset / bad index / shape
	 *  mismatch. Use this from anywhere a wrong / stale offset is even
	 *  remotely possible (every call site that doesn't carry a layout
	 *  invariant proving the offset is correct).
	 *
	 *  The `*Checked` variants below now also harden against bad offsets
	 *  in every build (they used to rely on `check()` only, which
	 *  Shipping strips, leaving an out-of-bounds `StructSlots[Index]`
	 *  read free to walk into adjacent heap then `CopyScriptStruct` on
	 *  garbage). They `LowLevelFatalError` when something violates the
	 *  documented precondition — caller still gets the early-detection
	 *  semantic in Debug, but Shipping no longer silently corrupts. New
	 *  call sites should still prefer the Try* form unless the
	 *  precondition is provably enforced upstream. */
	const FInstancedStruct* TryGetStructSlot(int32 Offset, const UScriptStruct* ExpectedStruct = nullptr) const
	{
		if (!IsStructSlotOffset(Offset))
		{
			return nullptr;
		}
		const int32 Index = GetStructSlotIndex(Offset);
		if (!StructSlots.IsValidIndex(Index))
		{
			return nullptr;
		}
		const FInstancedStruct& Slot = StructSlots[Index];
		if (!Slot.IsValid())
		{
			return nullptr;
		}
		if (ExpectedStruct && Slot.GetScriptStruct() != ExpectedStruct)
		{
			return nullptr;
		}
		return &Slot;
	}

	FInstancedStruct* TryGetStructSlotMutable(int32 Offset, const UScriptStruct* ExpectedStruct = nullptr)
	{
		if (!IsStructSlotOffset(Offset))
		{
			return nullptr;
		}
		const int32 Index = GetStructSlotIndex(Offset);
		if (!StructSlots.IsValidIndex(Index))
		{
			return nullptr;
		}
		FInstancedStruct& Slot = StructSlots[Index];
		if (!Slot.IsValid())
		{
			return nullptr;
		}
		if (ExpectedStruct && Slot.GetScriptStruct() != ExpectedStruct)
		{
			return nullptr;
		}
		return &Slot;
	}

	const FInstancedStruct& GetStructSlotChecked(int32 Offset) const
	{
		// Shipping-safe hardening: real branch to a fatal error rather
		// than rely on `check()` (stripped in Shipping → out-of-bounds
		// read → CopyScriptStruct on garbage in adjacent heap). This is
		// the explicit-precondition variant; consumers that can't prove
		// the offset is good should use TryGetStructSlot above.
		if (!IsStructSlotOffset(Offset))
		{
			LowLevelFatalError(TEXT("GetStructSlotChecked: offset %d is not a struct-slot synthetic offset."), Offset);
		}
		const int32 Index = GetStructSlotIndex(Offset);
		if (!StructSlots.IsValidIndex(Index))
		{
			LowLevelFatalError(TEXT("GetStructSlotChecked: struct-slot index %d out of range (Num=%d)."), Index, StructSlots.Num());
		}
		return StructSlots[Index];
	}

	FInstancedStruct& GetStructSlotMutableChecked(int32 Offset)
	{
		if (!IsStructSlotOffset(Offset))
		{
			LowLevelFatalError(TEXT("GetStructSlotMutableChecked: offset %d is not a struct-slot synthetic offset."), Offset);
		}
		const int32 Index = GetStructSlotIndex(Offset);
		if (!StructSlots.IsValidIndex(Index))
		{
			LowLevelFatalError(TEXT("GetStructSlotMutableChecked: struct-slot index %d out of range (Num=%d)."), Index, StructSlots.Num());
		}
		return StructSlots[Index];
	}

	void RegisterReferenceSlot(EComposableCameraPinType PinType, int32 Offset);
	void RefreshReferenceSlot(int32 Offset);
	void RefreshAllReferenceSlots();
	void AddReferencedObjects(FReferenceCollector& Collector);

	/** Check if storage has been allocated.
	 *
	 *  An asset can legitimately have ONLY non-POD struct slots (every exposed
	 *  parameter / variable / output pin is a USTRUCT containing FString /
	 *  TArray / object refs). In that case `Storage` stays empty and `TotalSize`
	 *  stays 0, but the data block is still meaningfully populated via
	 *  `StructSlots`. The byte-pool-only check would mark such an asset
	 *  invalid and cause debug introspection / FullExecChain dispatch to
	 *  silently fall back to legacy paths. Either pool counts. */
	bool IsValid() const
	{
		return (TotalSize > 0 && Storage.Num() > 0) || StructSlots.Num() > 0;
	}

	/** Re-initialize all storage to its post-allocation default state. Both
	 *  pools are reset:
	 *    - `Storage` is memzeroed (POD slots back to all-zero bytes).
	 *    - Each `StructSlots[i]` is re-initialized via `InitializeAs(SameType)`,
	 *      which destroys + default-constructs in place. The slot's struct type
	 *      identity is preserved so existing offset tables stay valid; only the
	 *      values reset (FString empty, TArray empty, UObject* null, etc.).
	 *
	 *  Currently called only at allocation time, where `Storage.SetNumZeroed`
	 *  + the `RegisterStructSlot` loop already produce this state. The
	 *  function exists for future re-init callers (data-block reuse across
	 *  reactivations, pooled allocators); without the StructSlots reset such
	 *  callers would observe stale heap-owned state from the previous use. */
	void ZeroInitialize()
	{
		if (Storage.Num() > 0)
		{
			FMemory::Memzero(Storage.GetData(), Storage.Num());
			RefreshAllReferenceSlots();
		}

		for (FInstancedStruct& Slot : StructSlots)
		{
			if (const UScriptStruct* Type = Slot.GetScriptStruct())
			{
				// InitializeAs(Type) on an already-initialized slot of the
				// same type runs the slot's destructor then the default
				// constructor in place — equivalent to "zero-init for non-POD
				// struct" without freeing/reallocating the FInstancedStruct's
				// owning memory layout.
				Slot.InitializeAs(Type);
			}
		}
	}
};
