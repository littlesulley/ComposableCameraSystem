// Copyright Sulley. All rights reserved.

#include "LevelSequence/ComposableCameraTypeAssetReference.h"

#include "ComposableCameraSystemModule.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "LevelSequence/ComposableCameraPinTypeUtils.h"

namespace
{
	/**
	 * Build a single FPropertyBagPropertyDesc and, if the pin type is
	 * representable in a property bag, append it to OutDescs. Returns false
	 * for types we intentionally skip (currently just Delegate).
	 *
	 * The descriptor's PropertyFlags explicitly include CPF_Interp on top of
	 * the default CPF_Edit. CPF_Interp is what Sequencer's FindPropertySetter
	 * checks to decide whether the leaf is keyable — without it,
	 * CanKeyProperty returns false on every leaf and the track-editor
	 * menu collapses to "no entries". The flag propagates onto the dynamic
	 * FProperty that UPropertyBag::GetOrCreateFromDescs creates (see
	 * Engine/Source/Runtime/CoreUObject/Private/StructUtils/PropertyBag.cpp
	 * "NewProperty->SetPropertyFlags((EPropertyFlags)Desc.PropertyFlags);").
	 */
	bool AddDescIfSupported(
		FName Name,
		EComposableCameraPinType PinType,
		const UScriptStruct* StructType,
		const UEnum* EnumType,
		TArray<FPropertyBagPropertyDesc>& OutDescs)
	{
		EPropertyBagPropertyType BagType = EPropertyBagPropertyType::None;
		const UObject* ValueObj = nullptr;
		if (!UE::ComposableCameras::PinTypeToPropertyBagType(PinType, StructType, EnumType, BagType, ValueObj))
		{
			return false;
		}
		FPropertyBagPropertyDesc Desc(Name, EPropertyBagContainerType::None, BagType, ValueObj,
			static_cast<EPropertyFlags>(CPF_Edit | CPF_Interp));
		OutDescs.Add(MoveTemp(Desc));
		return true;
	}

	/**
	 * Copy a single bag value (keyed by Name) into OutBlock via the matching
	 * FComposableCameraParameterBlock typed setter.
	 *
	 * If the bag has no value for Name (property missing or read error), the
	 * block is left untouched — the camera will fall back to the node's
	 * authored default during ApplyParameterBlock.
	 */
	void CopyBagValueIntoBlock(
		const FInstancedPropertyBag& Bag,
		FName Name,
		EComposableCameraPinType PinType,
		const UScriptStruct* StructType,
		const UEnum* EnumType,
		FComposableCameraParameterBlock& OutBlock)
	{
		switch (PinType)
		{
		case EComposableCameraPinType::Bool:
			if (auto R = Bag.GetValueBool(Name); R.HasValue())
			{
				OutBlock.SetBool(Name, R.GetValue());
			}
			return;

		case EComposableCameraPinType::Int32:
			if (auto R = Bag.GetValueInt32(Name); R.HasValue())
			{
				OutBlock.SetInt32(Name, R.GetValue());
			}
			return;

		case EComposableCameraPinType::Float:
			if (auto R = Bag.GetValueFloat(Name); R.HasValue())
			{
				OutBlock.SetFloat(Name, R.GetValue());
			}
			return;

		case EComposableCameraPinType::Double:
			if (auto R = Bag.GetValueDouble(Name); R.HasValue())
			{
				OutBlock.SetDouble(Name, R.GetValue());
			}
			return;

		case EComposableCameraPinType::Name:
			if (auto R = Bag.GetValueName(Name); R.HasValue())
			{
				OutBlock.SetName(Name, R.GetValue());
			}
			return;

		case EComposableCameraPinType::Vector2D:
			if (auto R = Bag.GetValueStruct<FVector2D>(Name); R.HasValue())
			{
				FVector2D Value = *R.GetValue();
				FComposableCameraParameterValue Entry;
				Entry.Set<FVector2D>(EComposableCameraPinType::Vector2D, Value);
				OutBlock.Values.Add(Name, MoveTemp(Entry));
			}
			return;

		case EComposableCameraPinType::Vector3D:
			if (auto R = Bag.GetValueStruct<FVector>(Name); R.HasValue())
			{
				OutBlock.SetVector(Name, *R.GetValue());
			}
			return;

		case EComposableCameraPinType::Vector4:
			if (auto R = Bag.GetValueStruct<FVector4>(Name); R.HasValue())
			{
				FVector4 Value = *R.GetValue();
				FComposableCameraParameterValue Entry;
				Entry.Set<FVector4>(EComposableCameraPinType::Vector4, Value);
				OutBlock.Values.Add(Name, MoveTemp(Entry));
			}
			return;

		case EComposableCameraPinType::Rotator:
			if (auto R = Bag.GetValueStruct<FRotator>(Name); R.HasValue())
			{
				OutBlock.SetRotator(Name, *R.GetValue());
			}
			return;

		case EComposableCameraPinType::Transform:
			if (auto R = Bag.GetValueStruct<FTransform>(Name); R.HasValue())
			{
				OutBlock.SetTransform(Name, *R.GetValue());
			}
			return;

		case EComposableCameraPinType::Struct:
			if (StructType)
			{
				if (auto R = Bag.GetValueStruct(Name, StructType); R.HasValue())
				{
					const FStructView View = R.GetValue();
					if (const uint8* Bytes = View.GetMemory())
					{
						const int32 Size = StructType->GetStructureSize();
						FComposableCameraParameterValue Entry;
						Entry.PinType = EComposableCameraPinType::Struct;
						Entry.Data.SetNumUninitialized(Size);
						StructType->InitializeStruct(Entry.Data.GetData());
						StructType->CopyScriptStruct(Entry.Data.GetData(), Bytes);
						OutBlock.Values.Add(Name, MoveTemp(Entry));
					}
				}
			}
			return;

		case EComposableCameraPinType::Actor:
			if (auto R = Bag.GetValueObject(Name, AActor::StaticClass()); R.HasValue())
			{
				OutBlock.SetActor(Name, Cast<AActor>(R.GetValue()));
			}
			return;

		case EComposableCameraPinType::Object:
			if (auto R = Bag.GetValueObject(Name); R.HasValue())
			{
				OutBlock.SetObject(Name, R.GetValue());
			}
			return;

		case EComposableCameraPinType::Enum:
			// The bag stores enums as a FByteProperty backed by EnumType (uint8).
			// CCS's ParameterBlock normalizes all enums to int64 regardless of
			// backing width; widen here so ApplyParameterBlock's narrow-cast
			// path can write to any FEnumProperty (uint8 / int32 / int64).
			if (EnumType)
			{
				if (auto R = Bag.GetValueEnum(Name, const_cast<UEnum*>(EnumType)); R.HasValue())
				{
					OutBlock.SetEnum(Name, static_cast<int64>(R.GetValue()));
				}
			}
			return;

		case EComposableCameraPinType::Delegate:
		default:
			// Delegates are not representable in the bag; nothing to copy.
			return;
		}
	}
}

void FComposableCameraTypeAssetReference::RebuildBagsFromTypeAsset()
{
	if (!TypeAsset)
	{
		Parameters.Reset();
		Variables.Reset();
		return;
	}

	// Parameters bag: one descriptor per entry in TypeAsset->ExposedParameters.
	TArray<FPropertyBagPropertyDesc> ParameterDescs;
	ParameterDescs.Reserve(TypeAsset->ExposedParameters.Num());
	for (const FComposableCameraExposedParameter& Param : TypeAsset->ExposedParameters)
	{
		if (Param.ParameterName.IsNone())
		{
			continue;
		}
		AddDescIfSupported(Param.ParameterName, Param.PinType, Param.StructType, Param.EnumType, ParameterDescs);
	}

	// Variables bag: one descriptor per entry in TypeAsset->ExposedVariables.
	// InternalVariables are node-private (not caller-overridable) — they do not
	// appear in the bag and are driven purely by the TypeAsset's InitialValueString.
	TArray<FPropertyBagPropertyDesc> VariableDescs;
	VariableDescs.Reserve(TypeAsset->ExposedVariables.Num());
	for (const FComposableCameraInternalVariable& Var : TypeAsset->ExposedVariables)
	{
		if (Var.VariableName.IsNone())
		{
			continue;
		}
		AddDescIfSupported(Var.VariableName, Var.VariableType, Var.StructType, Var.EnumType, VariableDescs);
	}

	// MigrateToNewBagStruct preserves values for properties whose name + type
	// survive the new layout and resets the rest to their type defaults. This
	// is what we want: renaming an exposed parameter → entry re-created; pure
	// addition of new parameters → existing values preserved; type change →
	// value reset (can't carry a float through into a vector slot safely).
	if (const UPropertyBag* NewParamStruct = UPropertyBag::GetOrCreateFromDescs(ParameterDescs))
	{
		Parameters.MigrateToNewBagStruct(NewParamStruct);
	}
	if (const UPropertyBag* NewVarStruct = UPropertyBag::GetOrCreateFromDescs(VariableDescs))
	{
		Variables.MigrateToNewBagStruct(NewVarStruct);
	}
}

void FComposableCameraTypeAssetReference::BuildParameterBlock(FComposableCameraParameterBlock& OutBlock) const
{
	if (!TypeAsset)
	{
		return;
	}

	for (const FComposableCameraExposedParameter& Param : TypeAsset->ExposedParameters)
	{
		if (Param.ParameterName.IsNone())
		{
			continue;
		}
		CopyBagValueIntoBlock(Parameters, Param.ParameterName, Param.PinType, Param.StructType, Param.EnumType, OutBlock);
	}

	for (const FComposableCameraInternalVariable& Var : TypeAsset->ExposedVariables)
	{
		if (Var.VariableName.IsNone())
		{
			continue;
		}
		CopyBagValueIntoBlock(Variables, Var.VariableName, Var.VariableType, Var.StructType, Var.EnumType, OutBlock);
	}
}
