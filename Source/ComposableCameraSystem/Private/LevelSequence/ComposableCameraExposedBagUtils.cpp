// Copyright Sulley. All rights reserved.

#include "LevelSequence/ComposableCameraExposedBagUtils.h"

#include "Core/ComposableCameraParameterBlock.h"
#include "GameFramework/Actor.h"
#include "LevelSequence/ComposableCameraPinTypeUtils.h"

namespace UE::ComposableCameras::ExposedBag
{
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
				OutBlock.StoreValue(Name, MoveTemp(Entry));
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
				OutBlock.StoreValue(Name, MoveTemp(Entry));
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
		{
			if (!StructType)
			{
				return;
			}
			if (auto R = Bag.GetValueStruct(Name, StructType); R.HasValue())
			{
				const FStructView View = R.GetValue();
				if (!View.IsValid() || View.GetScriptStruct() != StructType)
				{
					return;
				}

				if (IsBytewiseSafeStruct(StructType))
				{
					// POD path: bytes go into Entry.Data; StoreValue routes
					// to the byte-array Values map.
					const int32 Size = StructType->GetStructureSize();
					if (Size > 0)
					{
						FComposableCameraParameterValue Entry;
						Entry.PinType = EComposableCameraPinType::Struct;
						Entry.Data.SetNumZeroed(Size);
						StructType->InitializeStruct(Entry.Data.GetData());
						StructType->CopyScriptStruct(Entry.Data.GetData(), View.GetMemory());
						OutBlock.StoreValue(Name, MoveTemp(Entry));
					}
				}
				else
				{
					// Non-POD: route through SetStruct so the FInstancedStruct
					// path owns construction / destruction / GC traversal of
					// embedded heap-owned members.
					OutBlock.SetStruct(Name, StructType, View.GetMemory());
				}
			}
			return;
		}

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
