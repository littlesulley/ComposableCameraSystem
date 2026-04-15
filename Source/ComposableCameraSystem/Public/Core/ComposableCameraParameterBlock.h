// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/ComposableCameraNodePinTypes.h"
#include "ComposableCameraParameterBlock.generated.h"

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

	/** Type-erased parameter storage, keyed by parameter name. */
	UPROPERTY()
	TMap<FName, FComposableCameraParameterValue> Values;

	/** Set a bool parameter. */
	void SetBool(FName Name, bool Value)
	{
		FComposableCameraParameterValue Entry;
		Entry.Set<bool>(EComposableCameraPinType::Bool, Value);
		Values.Add(Name, MoveTemp(Entry));
	}

	/** Set an int32 parameter. */
	void SetInt32(FName Name, int32 Value)
	{
		FComposableCameraParameterValue Entry;
		Entry.Set<int32>(EComposableCameraPinType::Int32, Value);
		Values.Add(Name, MoveTemp(Entry));
	}

	/** Set a float parameter. */
	void SetFloat(FName Name, float Value)
	{
		FComposableCameraParameterValue Entry;
		Entry.Set<float>(EComposableCameraPinType::Float, Value);
		Values.Add(Name, MoveTemp(Entry));
	}

	/** Set a double parameter. */
	void SetDouble(FName Name, double Value)
	{
		FComposableCameraParameterValue Entry;
		Entry.Set<double>(EComposableCameraPinType::Double, Value);
		Values.Add(Name, MoveTemp(Entry));
	}

	/** Set a Vector parameter. */
	void SetVector(FName Name, const FVector& Value)
	{
		FComposableCameraParameterValue Entry;
		Entry.Set<FVector>(EComposableCameraPinType::Vector3D, Value);
		Values.Add(Name, MoveTemp(Entry));
	}

	/** Set a Rotator parameter. */
	void SetRotator(FName Name, const FRotator& Value)
	{
		FComposableCameraParameterValue Entry;
		Entry.Set<FRotator>(EComposableCameraPinType::Rotator, Value);
		Values.Add(Name, MoveTemp(Entry));
	}

	/** Set a Transform parameter. */
	void SetTransform(FName Name, const FTransform& Value)
	{
		FComposableCameraParameterValue Entry;
		Entry.Set<FTransform>(EComposableCameraPinType::Transform, Value);
		Values.Add(Name, MoveTemp(Entry));
	}

	/** Set an Actor pointer parameter. */
	void SetActor(FName Name, AActor* Value)
	{
		FComposableCameraParameterValue Entry;
		Entry.Set<AActor*>(EComposableCameraPinType::Actor, Value);
		Values.Add(Name, MoveTemp(Entry));
	}

	/** Set a UObject pointer parameter. */
	void SetObject(FName Name, UObject* Value)
	{
		FComposableCameraParameterValue Entry;
		Entry.Set<UObject*>(EComposableCameraPinType::Object, Value);
		Values.Add(Name, MoveTemp(Entry));
	}

	/** Set an FName parameter. FName is POD (NAME_INDEX + NAME_NUMBER, 8 bytes) and
	 *  is memcpy-safe in the type-erased data storage. */
	void SetName(FName Name, FName Value)
	{
		FComposableCameraParameterValue Entry;
		Entry.Set<FName>(EComposableCameraPinType::Name, Value);
		Values.Add(Name, MoveTemp(Entry));
	}

	/** Set an enum parameter. Enums are always normalized to int64 in the data
	 *  storage, regardless of the backing property's actual underlying width.
	 *  The narrow-cast into the final storage happens at resolve time, where the
	 *  owning FProperty is known (see WriteEnumInt64ToProperty). */
	void SetEnum(FName Name, int64 Value)
	{
		FComposableCameraParameterValue Entry;
		Entry.Set<int64>(EComposableCameraPinType::Enum, Value);
		Values.Add(Name, MoveTemp(Entry));
	}

	/** Check if a parameter exists by name. */
	bool HasValue(FName Name) const
	{
		return Values.Contains(Name);
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
