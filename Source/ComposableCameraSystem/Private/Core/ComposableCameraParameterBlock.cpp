// Copyright Sulley. All rights reserved.

#include "Core/ComposableCameraParameterBlock.h"

#include "ComposableCameraSystemModule.h"
#include "UObject/GCObject.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

namespace ComposableCameraParameterBlockPrivate
{
	/** Set the OutError string if the pointer is non-null. */
	static void WriteError(FString* OutError, FString&& Message)
	{
		if (OutError)
		{
			*OutError = MoveTemp(Message);
		}
	}

	/** Shared helper: run ImportText for a core struct type identified by T, then
	 *  Set() the result into the entry under the matching pin type. */
	template<typename T>
	bool ImportCoreStructAndSet(
		FComposableCameraParameterBlock& OutBlock,
		FName ParameterName,
		EComposableCameraPinType PinType,
		UScriptStruct* CoreStructType,
		const FString& ValueString,
		FString* OutError)
	{
		T Value{};
		const TCHAR* Buffer = *ValueString;
		// ImportText expects the raw struct pointer. Use ImportText_Direct so we
		// don't depend on any containing UObject for the parse.
		const TCHAR* Result = CoreStructType->ImportText(
			Buffer, &Value, /*OwnerObject*/ nullptr,
			PPF_None, /*ErrorText*/ nullptr,
			CoreStructType->GetName());
		if (!Result)
		{
			WriteError(OutError, FString::Printf(
				TEXT("Failed to parse '%s' as %s"),
				*ValueString, *CoreStructType->GetName()));
			return false;
		}

		FComposableCameraParameterValue Entry;
		Entry.Set<T>(PinType, Value);
		OutBlock.StoreValue(ParameterName, MoveTemp(Entry));
		return true;
	}
}

void FComposableCameraParameterBlock::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (auto& Pair : ActorValues)
	{
		Collector.AddReferencedObject(Pair.Value);
	}
	for (auto& Pair : ObjectValues)
	{
		Collector.AddReferencedObject(Pair.Value);
	}
	// FInstancedStruct exposes embedded UObject references via the script
	// struct's AddStructReferencedObjects path. Walking StructValues here lets
	// the GC see Actor / UObject members buried inside non-POD struct values
	// the caller passed in via SetStruct.
	//
	// In addition to the property-graph walk, mark the `UScriptStruct*` itself
	// for each slot. `UserDefinedStruct` (Blueprint-authored struct asset) is
	// a regular UObject that GC reclaims when no rooted reference exists -	// `SourceParameterBlock` / `PendingParameterBlock` are non-reflected
	// owners that only call this manual walker, and `CachedParameters` is
	// reached via the same path through ARO. Without an explicit mark on
	// the type, a UserDefinedStruct value's type can be reclaimed mid-frame
	// and the next `Slot.GetScriptStruct()` / `CopyScriptStruct(...)` reads
	// stale type memory. Same pattern as
	// `FComposableCameraRuntimeDataBlock::AddReferencedObjects`.
	for (auto& Pair : StructValues)
	{
		FInstancedStruct& Slot = Pair.Value;
		if (Slot.IsValid())
		{
			if (const UScriptStruct* Struct = Slot.GetScriptStruct())
			{
				TObjectPtr<UScriptStruct> TypeRef = const_cast<UScriptStruct*>(Struct);
				Collector.AddReferencedObject(TypeRef);
				Collector.AddPropertyReferencesWithStructARO(Struct, Slot.GetMutableMemory());
			}
		}
	}
	// FScriptDelegate stores its bound target in a TWeakObjectPtr -GC does not
	// keep that object alive on its own. Without this walk, a delegate whose
	// target is only kept alive transitively through ParameterBlock would see
	// the target collected between SetDelegate(...) and ApplyDelegateBindings,
	// and the delegate would silently apply as unbound (GetUObject() returns
	// nullptr) on the destination node. Mark the resolved target as reachable
	// so it survives until the delegate is consumed; the FScriptDelegate's
	// own weak-ptr semantics still null cleanly if the user explicitly
	// destroys the target.
	for (auto& Pair : DelegateValues)
	{
		if (UObject* Bound = Pair.Value.GetUObject())
		{
			TObjectPtr<UObject> StrongRef = Bound;
			Collector.AddReferencedObject(StrongRef);
		}
	}
}

bool FComposableCameraParameterBlock::ApplyStringValue(
	FComposableCameraParameterBlock& OutBlock,
	FName ParameterName,
	EComposableCameraPinType PinType,
	UScriptStruct* StructType,
	UEnum* EnumType,
	const FString& ValueString,
	FString* OutError)
{
	using namespace ComposableCameraParameterBlockPrivate;

	if (ValueString.IsEmpty())
	{
		WriteError(OutError, TEXT("Empty value string"));
		return false;
	}

	switch (PinType)
	{
	case EComposableCameraPinType::Bool:
	{
		// LexFromString on bool accepts "true"/"false"/"yes"/"no"/"1"/"0".
		bool Value = false;
		LexFromString(Value, *ValueString);
		FComposableCameraParameterValue Entry;
		Entry.Set<bool>(PinType, Value);
		OutBlock.StoreValue(ParameterName, MoveTemp(Entry));
		return true;
	}

	case EComposableCameraPinType::Int32:
	{
		int32 Value = 0;
		LexFromString(Value, *ValueString);
		FComposableCameraParameterValue Entry;
		Entry.Set<int32>(PinType, Value);
		OutBlock.StoreValue(ParameterName, MoveTemp(Entry));
		return true;
	}

	case EComposableCameraPinType::Float:
	{
		float Value = 0.0f;
		LexFromString(Value, *ValueString);
		FComposableCameraParameterValue Entry;
		Entry.Set<float>(PinType, Value);
		OutBlock.StoreValue(ParameterName, MoveTemp(Entry));
		return true;
	}

	case EComposableCameraPinType::Double:
	{
		double Value = 0.0;
		LexFromString(Value, *ValueString);
		FComposableCameraParameterValue Entry;
		Entry.Set<double>(PinType, Value);
		OutBlock.StoreValue(ParameterName, MoveTemp(Entry));
		return true;
	}

	case EComposableCameraPinType::Vector2D:
	{
		return ImportCoreStructAndSet<FVector2D>(
			OutBlock, ParameterName, PinType,
			TBaseStructure<FVector2D>::Get(), ValueString, OutError);
	}

	case EComposableCameraPinType::Vector3D:
	{
		return ImportCoreStructAndSet<FVector>(
			OutBlock, ParameterName, PinType,
			TBaseStructure<FVector>::Get(), ValueString, OutError);
	}

	case EComposableCameraPinType::Vector4:
	{
		return ImportCoreStructAndSet<FVector4>(
			OutBlock, ParameterName, PinType,
			TBaseStructure<FVector4>::Get(), ValueString, OutError);
	}

	case EComposableCameraPinType::Rotator:
	{
		return ImportCoreStructAndSet<FRotator>(
			OutBlock, ParameterName, PinType,
			TBaseStructure<FRotator>::Get(), ValueString, OutError);
	}

	case EComposableCameraPinType::Transform:
	{
		return ImportCoreStructAndSet<FTransform>(
			OutBlock, ParameterName, PinType,
			TBaseStructure<FTransform>::Get(), ValueString, OutError);
	}

	case EComposableCameraPinType::Actor:
	{
		// Actors live in worlds, not assets. A DataTable row cannot reference a
		// live actor. Refuse rather than silently loading a CDO.
		WriteError(OutError, TEXT(
			"Actor parameters cannot be set from a DataTable row. "
			"Use an Object parameter with a soft path if you need a class or archetype reference."));
		return false;
	}

	case EComposableCameraPinType::Object:
	{
		// Treat the string as a soft object path and sync-load. Empty or invalid
		// paths write a null pointer into the block so the runtime sees an
		// explicit null instead of a stale default.
		const FSoftObjectPath Path(ValueString);
		UObject* Loaded = nullptr;
		if (Path.IsValid())
		{
			Loaded = Path.TryLoad();
		}

		FComposableCameraParameterValue Entry;
		Entry.Set<UObject*>(PinType, Loaded);
		OutBlock.StoreValue(ParameterName, MoveTemp(Entry));

		if (!Loaded)
		{
			WriteError(OutError, FString::Printf(
				TEXT("Object path '%s' could not be resolved; stored null"),
				*ValueString));
		}
		return true;
	}

	case EComposableCameraPinType::Name:
	{
		// FName construction from an arbitrary string is lossy for Unicode
		// (FName tables are 8-bit + comparison-hash). Names authored through the
		// editor should be ASCII. We accept any string here without rejection -		// if the user writes garbage we still produce a valid (garbage) FName.
		FComposableCameraParameterValue Entry;
		Entry.Set<FName>(PinType, FName(*ValueString));
		OutBlock.StoreValue(ParameterName, MoveTemp(Entry));
		return true;
	}

	case EComposableCameraPinType::Enum:
	{
		if (!EnumType)
		{
			WriteError(OutError, TEXT("Enum pin type requires an EnumType"));
			return false;
		}

		// The authored string is expected to match one of the enum's entry names
		// (either the short name like "EFoo::Bar" or the qualified form). We also
		// accept a numeric literal as a fallback so raw integer authoring still
		// round-trips. This matches how the editor-side parameter table row
		// customization exports the selected enum value.
		int64 ParsedValue = EnumType->GetValueByNameString(ValueString);
		if (ParsedValue == INDEX_NONE)
		{
			// Fallback: accept bare integer literals.
			if (ValueString.IsNumeric())
			{
				ParsedValue = FCString::Atoi64(*ValueString);
				if (!EnumType->IsValidEnumValue(ParsedValue))
				{
					WriteError(OutError, FString::Printf(
						TEXT("Numeric value '%s' is not a valid entry for enum %s"),
						*ValueString, *EnumType->GetName()));
					return false;
				}
			}
			else
			{
				WriteError(OutError, FString::Printf(
					TEXT("'%s' does not match any entry of enum %s"),
					*ValueString, *EnumType->GetName()));
				return false;
			}
		}

		FComposableCameraParameterValue Entry;
		Entry.Set<int64>(PinType, ParsedValue);
		OutBlock.StoreValue(ParameterName, MoveTemp(Entry));
		return true;
	}

	case EComposableCameraPinType::Struct:
	{
		if (!StructType)
		{
			WriteError(OutError, TEXT("Struct pin type requires a StructType"));
			return false;
		}

		const int32 Size = StructType->GetStructureSize();
		if (Size <= 0)
		{
			WriteError(OutError, FString::Printf(
				TEXT("Struct '%s' has zero structure size"), *StructType->GetName()));
			return false;
		}

		// Two paths:
		//   POD     -> initialize in place inside Entry.Data (byte storage),
		//              ImportText into those bytes, store via StoreValue.
		//   non-POD -> initialize a fresh FInstancedStruct, ImportText into its
		//              owned memory, store via SetStruct (which routes to
		//              StructValues and clears the byte-array entry under the
		//              same name). FInstancedStruct owns construction /
		//              destruction / GC -- exactly the contract bytes can't
		//              fulfill for FString / TArray / object refs.
		if (IsBytewiseSafeStruct(StructType))
		{
			FComposableCameraParameterValue Entry;
			Entry.PinType = PinType;
			Entry.Data.SetNumZeroed(Size);
			StructType->InitializeStruct(Entry.Data.GetData());

			const TCHAR* Buffer = *ValueString;
			const TCHAR* Result = StructType->ImportText(
				Buffer, Entry.Data.GetData(), /*OwnerObject*/ nullptr,
				PPF_None, /*ErrorText*/ nullptr,
				StructType->GetName());

			if (!Result)
			{
				StructType->DestroyStruct(Entry.Data.GetData());
				WriteError(OutError, FString::Printf(
					TEXT("Failed to parse '%s' as struct %s"),
					*ValueString, *StructType->GetName()));
				return false;
			}

			OutBlock.StoreValue(ParameterName, MoveTemp(Entry));
			return true;
		}

		// Non-POD: typed storage via FInstancedStruct.
		FInstancedStruct Slot;
		Slot.InitializeAs(StructType);

		const TCHAR* Buffer = *ValueString;
		const TCHAR* Result = StructType->ImportText(
			Buffer, Slot.GetMutableMemory(), /*OwnerObject*/ nullptr,
			PPF_None, /*ErrorText*/ nullptr,
			StructType->GetName());

		if (!Result)
		{
			WriteError(OutError, FString::Printf(
				TEXT("Failed to parse '%s' as struct %s"),
				*ValueString, *StructType->GetName()));
			return false;
		}

		// Route through SetStruct so the parallel POD/Actor/Object/Delegate
		// entries under this name are cleared (defensive against authoring
		// changes that flip the type but reuse the same parameter name).
		OutBlock.SetStruct(ParameterName, StructType, Slot.GetMemory());
		return true;
	}

	case EComposableCameraPinType::Delegate:
	{
		// Delegates cannot be serialized from a string. They are bound at
		// activation time through the K2 ActivateComposableCamera node, not
		// through DataTable rows. Return false so the caller can fall back
		// to the node pin's authored default (which for delegates is "unbound").
		WriteError(OutError, TEXT("Delegate parameters cannot be set from a string value"));
		return false;
	}

	default:
	{
		WriteError(OutError, FString::Printf(
			TEXT("Unhandled pin type %d"), static_cast<int32>(PinType)));
		return false;
	}
	}
}
