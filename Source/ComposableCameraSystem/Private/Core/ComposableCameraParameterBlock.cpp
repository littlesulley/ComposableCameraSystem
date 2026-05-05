// Copyright Sulley. All rights reserved.

#include "Core/ComposableCameraParameterBlock.h"

#include "ComposableCameraSystemModule.h"
#include "UObject/GCObject.h"
#include "UObject/SoftObjectPath.h"
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
		// Actors live in worlds, not assets — a DataTable row cannot reference a
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
		// editor should be ASCII. We accept any string here without rejection —
		// if the user writes garbage we still produce a valid (garbage) FName.
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
		// round-trips — this matches how the editor-side parameter table row
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

		if (!IsBytewiseSafeStruct(StructType))
		{
			WriteError(OutError, FString::Printf(
				TEXT("Struct '%s' is not POD (contains FString / FText / TArray / TMap / TSet / object reference / delegate fields). The byte-array ParameterBlock cannot store non-POD structs without owned typed storage; type-asset Build() flags this at authoring time."),
				*StructType->GetName()));
			return false;
		}

		const int32 Size = StructType->GetStructureSize();
		if (Size <= 0)
		{
			WriteError(OutError, FString::Printf(
				TEXT("Struct '%s' has zero structure size"), *StructType->GetName()));
			return false;
		}

		// Initialize the struct in place inside Entry.Data, then ImportText
		// directly into that storage. The previous design (initialize a
		// scratch buffer, ImportText into scratch, memcpy bytes into a
		// separate Entry.Data, DestroyStruct on scratch) carried no benefit
		// for POD storage and -- subtly -- left Entry.Data holding bytes
		// whose pointee state was already destroyed if the struct ever
		// turned out to be non-POD. The IsBytewiseSafeStruct gate above
		// keeps us in POD-only land, so destruction is a no-op and a
		// single in-place initialization is correct.
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

	case EComposableCameraPinType::Delegate:
	{
		// Delegates cannot be serialized from a string — they are bound at
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
