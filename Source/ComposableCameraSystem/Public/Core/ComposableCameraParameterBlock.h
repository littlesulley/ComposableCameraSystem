// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraSystemModule.h"
#include "GameFramework/Actor.h"
#include "Nodes/ComposableCameraNodePinTypes.h"
#include "StructUtils/InstancedStruct.h"
#include "ComposableCameraParameterBlock.generated.h"

class FReferenceCollector;

/**
 * A single parameter value in a ParameterBlock.
 * Type-erased storage using a byte array.
 */
USTRUCT(BlueprintType)
struct COMPOSABLECAMERASYSTEM_API FComposableCameraParameterValue
{
	GENERATED_BODY()

	/** The pin type of this value. */
	UPROPERTY()
	EComposableCameraPinType PinType = EComposableCameraPinType::Float;

	/** Raw bytes holding the value. Size depends on PinType. */
	TArray<uint8> Data;

	/** Set a typed value. */
	template<typename T>
	void Set(EComposableCameraPinType InPinType, const T& Value)
	{
		PinType = InPinType;
		Data.SetNumUninitialized(sizeof(T));
		FMemory::Memcpy(Data.GetData(), &Value, sizeof(T));
	}

	/** Get a typed value. Returns false if types mismatch or data is empty. */
	template<typename T>
	bool Get(T& OutValue) const
	{
		if (Data.Num() != sizeof(T))
		{
			return false;
		}
		FMemory::Memcpy(&OutValue, Data.GetData(), sizeof(T));
		return true;
	}
};

/**
 * Container for parameter values passed by callers when activating a camera from a type asset.
 *
 * The K2Node fills this automatically from its dynamic pins. C++ callers fill it manually.
 * DataTable callers fill it by parsing row data.
 */
USTRUCT(BlueprintType)
struct COMPOSABLECAMERASYSTEM_API FComposableCameraParameterBlock
{
	GENERATED_BODY()

	/** Type-erased parameter storage, keyed by parameter name.
	 *  POD-only — delegates are stored in DelegateValues instead. */
	UPROPERTY()
	TMap<FName, FComposableCameraParameterValue> Values;

	/** GC-visible owners for object-valued entries mirrored in Values. */
	UPROPERTY()
	TMap<FName, TObjectPtr<AActor>> ActorValues;

	/** GC-visible owners for object-valued entries mirrored in Values. */
	UPROPERTY()
	TMap<FName, TObjectPtr<UObject>> ObjectValues;

	/** Parallel storage for single-cast delegate bindings. Delegates are not
	 *  POD and cannot be stored in the byte-array-based FComposableCameraParameterValue.
	 *  They are applied at activation time via ApplyDelegateBindings (on the type
	 *  asset), which writes them into the target node's FDelegateProperty UPROPERTY
	 *  via reflection. */
	TMap<FName, FScriptDelegate> DelegateValues;

	/** Parallel storage for non-POD struct values (USTRUCTs containing FString /
	 *  FText / TArray / object refs / delegates -- anything `IsBytewiseSafeStruct`
	 *  rejects). The byte-array `Values` map cannot transport these because raw
	 *  memcpy aliases heap-owned storage and makes the GC blind to embedded
	 *  references; FInstancedStruct owns its memory, runs proper constructors /
	 *  destructors, and surfaces UObject references via AddStructReferencedObjects.
	 *  POD struct values (FVector / FRotator / FTransform / etc.) still go through
	 *  the byte-array `Values` map -- they're memcpy-safe and the existing offset
	 *  tables in RuntimeDataBlock are tighter. */
	UPROPERTY()
	TMap<FName, FInstancedStruct> StructValues;

	void Reserve(int32 Num)
	{
		Values.Reserve(Num);
		ActorValues.Reserve(Num);
		ObjectValues.Reserve(Num);
		DelegateValues.Reserve(Num);
		StructValues.Reserve(Num);
	}

	void StoreValue(FName Name, FComposableCameraParameterValue&& Entry)
	{
		ActorValues.Remove(Name);
		ObjectValues.Remove(Name);
		DelegateValues.Remove(Name);
		StructValues.Remove(Name);

		if (Entry.PinType == EComposableCameraPinType::Actor && Entry.Data.Num() == sizeof(AActor*))
		{
			AActor* ActorValue = nullptr;
			FMemory::Memcpy(&ActorValue, Entry.Data.GetData(), sizeof(AActor*));
			ActorValues.Add(Name, ActorValue);
		}
		else if (Entry.PinType == EComposableCameraPinType::Object && Entry.Data.Num() == sizeof(UObject*))
		{
			UObject* ObjectValue = nullptr;
			FMemory::Memcpy(&ObjectValue, Entry.Data.GetData(), sizeof(UObject*));
			ObjectValues.Add(Name, ObjectValue);
		}

		Values.Add(Name, MoveTemp(Entry));
	}

	/** Set a non-POD struct parameter. The struct is copied into a fresh
	 *  FInstancedStruct via InitializeAs(StructType, Memory), which runs the
	 *  proper per-property copy (FString operator=, TArray copy, UObject ptr
	 *  etc.) and owns the result for the lifetime of this map entry. The
	 *  parallel `Values` / `ActorValues` / `ObjectValues` / `DelegateValues`
	 *  entries under the same Name are cleared so a subsequent Get-by-name
	 *  cannot read a stale POD-shaped entry for what is now a struct value. */
	void SetStruct(FName Name, const UScriptStruct* Struct, const void* Memory)
	{
		if (!Struct || !Memory)
		{
			return;
		}
		// Defense-in-depth: refuse to wrap our own infrastructure structs.
		// The Blueprint compiler can emit bytecode that resolves the
		// CustomStructureParam wildcard 'Value' arg to the FIRST arg's
		// type instead of the actually-wired type (a known UE BP bug for
		// certain wiring shapes -- e.g. literal default on a struct pin
		// without a wired Make node). When that happens, SetStruct would
		// be called with Struct = FComposableCameraParameterBlock::
		// StaticStruct() and Memory = some bogus stack location, and
		// FInstancedStruct::InitializeAs's CopyScriptStruct walks the
		// ParameterBlock's TMap fields reading bogus pointers, crashing
		// in TSet::operator=. Refuse the call instead.
		if (Struct == StaticStruct()
			|| Struct == FComposableCameraParameterValue::StaticStruct())
		{
			// Known UE 5.6 BP wildcard bug: when a CustomStructureParam pin
			// receives a pin-default literal routed through a MakeLiteral
			// intermediate (e.g. K2 Activate Camera Override Pin of type
			// Vector3D with a typed default like "(0,0,10000)"), the emitted
			// bytecode resolves the wildcard arg's FProperty to the
			// function's FIRST parameter type (FComposableCameraParameterBlock)
			// instead of the actual wired type (FVector). Calling
			// FInstancedStruct::InitializeAs with that mis-typed Struct +
			// the value bytes intended for FVector crashes inside
			// CopyScriptStruct (TSet::operator= on bogus memory).
			//
			// Workaround at the BP author level: wire a Make Vector / Make
			// Rotator / etc. node into the Override Pin instead of typing
			// the literal default directly. The wired path takes a different
			// ExpandNode branch (MovePinLinksToIntermediate) whose bytecode
			// is unaffected.
			//
			// Long-term fix is to refactor K2 ExpandNode to dispatch per
			// pin type to typed setters (SetParameterBlockVector etc.) so
			// the CustomStructureParam wildcard is only used for non-POD
			// struct values where it's actually needed; tracked separately.
			//
			// Until then, log once per call and refuse the setter so the
			// ApplyParameterBlock InitialValueString fallback handles the
			// missing value rather than silently corrupting memory.
			UE_LOG(LogComposableCameraSystem, Verbose,
				TEXT("SetStruct refused for CCS infrastructure type '%s'. K2 SetParameterBlockValue CustomThunk received a mis-typed wildcard arg from the BP compiler. With the typed-setter dispatch in K2 ExpandNode this should not normally trigger -- if you see this fire, the wildcard fallback (Enum / arbitrary Struct / Delegate) hit the same bug pattern and the value will silently not propagate."),
				*Struct->GetName());
			return;
		}

		Values.Remove(Name);
		ActorValues.Remove(Name);
		ObjectValues.Remove(Name);
		DelegateValues.Remove(Name);

		FInstancedStruct Slot;
		Slot.InitializeAs(Struct, static_cast<const uint8*>(Memory));
		StructValues.Add(Name, MoveTemp(Slot));
	}

	void AddReferencedObjects(FReferenceCollector& Collector);

	/** Set a bool parameter. */
	void SetBool(FName Name, bool Value)
	{
		FComposableCameraParameterValue Entry;
		Entry.Set<bool>(EComposableCameraPinType::Bool, Value);
		StoreValue(Name, MoveTemp(Entry));
	}

	/** Set an int32 parameter. */
	void SetInt32(FName Name, int32 Value)
	{
		FComposableCameraParameterValue Entry;
		Entry.Set<int32>(EComposableCameraPinType::Int32, Value);
		StoreValue(Name, MoveTemp(Entry));
	}

	/** Set a float parameter. */
	void SetFloat(FName Name, float Value)
	{
		FComposableCameraParameterValue Entry;
		Entry.Set<float>(EComposableCameraPinType::Float, Value);
		StoreValue(Name, MoveTemp(Entry));
	}

	/** Set a double parameter. */
	void SetDouble(FName Name, double Value)
	{
		FComposableCameraParameterValue Entry;
		Entry.Set<double>(EComposableCameraPinType::Double, Value);
		StoreValue(Name, MoveTemp(Entry));
	}

	/** Set a Vector parameter. */
	void SetVector(FName Name, const FVector& Value)
	{
		FComposableCameraParameterValue Entry;
		Entry.Set<FVector>(EComposableCameraPinType::Vector3D, Value);
		StoreValue(Name, MoveTemp(Entry));
	}

	/** Set a Rotator parameter. */
	void SetRotator(FName Name, const FRotator& Value)
	{
		FComposableCameraParameterValue Entry;
		Entry.Set<FRotator>(EComposableCameraPinType::Rotator, Value);
		StoreValue(Name, MoveTemp(Entry));
	}

	/** Set a Transform parameter. */
	void SetTransform(FName Name, const FTransform& Value)
	{
		FComposableCameraParameterValue Entry;
		Entry.Set<FTransform>(EComposableCameraPinType::Transform, Value);
		StoreValue(Name, MoveTemp(Entry));
	}

	/** Set an Actor pointer parameter. */
	void SetActor(FName Name, AActor* Value)
	{
		FComposableCameraParameterValue Entry;
		Entry.Set<AActor*>(EComposableCameraPinType::Actor, Value);
		StoreValue(Name, MoveTemp(Entry));
	}

	/** Set a UObject pointer parameter. */
	void SetObject(FName Name, UObject* Value)
	{
		FComposableCameraParameterValue Entry;
		Entry.Set<UObject*>(EComposableCameraPinType::Object, Value);
		StoreValue(Name, MoveTemp(Entry));
	}

	/** Set an FName parameter. FName is POD (NAME_INDEX + NAME_NUMBER, 8 bytes) and
	 *  is memcpy-safe in the type-erased data storage. */
	void SetName(FName Name, FName Value)
	{
		FComposableCameraParameterValue Entry;
		Entry.Set<FName>(EComposableCameraPinType::Name, Value);
		StoreValue(Name, MoveTemp(Entry));
	}

	/** Set an enum parameter. Enums are always normalized to int64 in the data
	 *  storage, regardless of the backing property's actual underlying width.
	 *  The narrow-cast into the final storage happens at resolve time, where the
	 *  owning FProperty is known (see WriteEnumInt64ToProperty). */
	void SetEnum(FName Name, int64 Value)
	{
		FComposableCameraParameterValue Entry;
		Entry.Set<int64>(EComposableCameraPinType::Enum, Value);
		StoreValue(Name, MoveTemp(Entry));
	}

	/** Set a single-cast delegate binding. The delegate is stored in a parallel
	 *  map (not the POD byte array) and applied at activation time via
	 *  ApplyDelegateBindings on the type asset. */
	void SetDelegate(FName Name, const FScriptDelegate& Value)
	{
		Values.Remove(Name);
		ActorValues.Remove(Name);
		ObjectValues.Remove(Name);
		StructValues.Remove(Name);
		DelegateValues.Add(Name, Value);
	}

	/** Check if a parameter exists by name (POD / actor / object / struct / delegate). */
	bool HasValue(FName Name) const
	{
		return Values.Contains(Name) || ActorValues.Contains(Name) || ObjectValues.Contains(Name)
			|| StructValues.Contains(Name) || DelegateValues.Contains(Name);
	}

	/** Try to get a typed value. Returns false if not found or type mismatch. */
	template<typename T>
	bool Get(FName Name, T& OutValue) const
	{
		if (const FComposableCameraParameterValue* Found = Values.Find(Name))
		{
			return Found->Get<T>(OutValue);
		}
		return false;
	}

	/** Copy a parameter's raw bytes into a destination buffer.
	 *  Returns the number of bytes copied, or 0 if not found. */
	int32 CopyRawTo(FName Name, uint8* Dest, int32 DestSize) const
	{
		if (const FComposableCameraParameterValue* Found = Values.Find(Name))
		{
			if (Found->Data.Num() <= DestSize)
			{
				FMemory::Memcpy(Dest, Found->Data.GetData(), Found->Data.Num());
				return Found->Data.Num();
			}
		}
		return 0;
	}

	/**
	 * Parse a serialized string into a typed entry and store it under ParameterName.
	 *
	 * This is the single string→typed-value entry point shared by the DataTable
	 * activation path and the DataTable row property-type customization. The two
	 * sides must round-trip through the same parser so that anything you can type
	 * in the editor is accepted identically at runtime.
	 *
	 * Supported types:
	 *   Bool, Int32, Float, Double  — LexFromString
	 *   Vector2D/3D/4, Rotator, Transform — ImportText on the matching core struct
	 *   Struct                      — ImportText on the provided StructType
	 *   Object                      — resolved via FSoftObjectPath and sync-loaded
	 *   Name                        — FName::FromString (no Unicode canonicalization)
	 *   Enum                        — UEnum::GetValueByNameString, stored as int64
	 *
	 * Unsupported (returns false, writes OutError):
	 *   Actor — actors are world-scoped and cannot be resolved from a DataTable
	 *           asset. Use Object with a soft path to a CDO/archetype instead if
	 *           you need a class reference.
	 *
	 * @param OutBlock        Parameter block to write into.
	 * @param ParameterName   Key the entry is stored under in OutBlock.Values.
	 * @param PinType         Target pin type — dispatches the parse branch.
	 * @param StructType      Only read when PinType == Struct; ignored otherwise.
	 * @param EnumType        Only read when PinType == Enum; ignored otherwise.
	 *                        Used to parse the display / authored name back to an
	 *                        int64 value (UEnum::GetValueByNameString).
	 * @param ValueString     The serialized value. Empty input is treated as a
	 *                        parse failure so callers can decide whether to fall
	 *                        back to the node pin's authored default.
	 * @param OutError        Optional human-readable error written on failure.
	 * @return                true on success, false otherwise. On failure the
	 *                        OutBlock is left untouched for this key.
	 */
	static bool ApplyStringValue(
		FComposableCameraParameterBlock& OutBlock,
		FName ParameterName,
		EComposableCameraPinType PinType,
		UScriptStruct* StructType,
		UEnum* EnumType,
		const FString& ValueString,
		FString* OutError = nullptr);
};
