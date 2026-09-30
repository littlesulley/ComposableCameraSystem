// Copyright 2026 Sulley. All Rights Reserved.

#include "DataAssets/ComposableCameraActionTypeAsset.h"

#include "Actions/ComposableCameraActionBase.h"
#include "Core/ComposableCameraParameterBlock.h"
#include "Nodes/ComposableCameraNodePinTypes.h"
#include "UObject/UnrealType.h"

bool UComposableCameraActionTypeAsset::IsExposableProperty(const FProperty* Property)
{
	if (!Property || Property->ArrayDim != 1
		|| !Property->HasAllPropertyFlags(CPF_Edit | CPF_BlueprintVisible)
		|| Property->HasAnyPropertyFlags(CPF_Transient | CPF_EditConst
			| CPF_DisableEditOnInstance | CPF_EditorOnly | CPF_Deprecated)
		|| Property->GetOwnerClass() == UComposableCameraActionBase::StaticClass()
		|| Property->IsA<FClassProperty>())
	{
		return false;
	}
	EComposableCameraPinType PinType;
	UScriptStruct* StructType = nullptr;
	UEnum* EnumType = nullptr;
	UFunction* SignatureFunction = nullptr;
	return TryMapPropertyToPinType(Property, PinType, StructType, EnumType,
		&SignatureFunction);
}

UComposableCameraActionBase* UComposableCameraActionTypeAsset::CreateAction(
	UObject* Outer, const FComposableCameraParameterBlock& Parameters) const
{
	if (!IsValid(Outer) || !IsValid(ActionTemplate))
	{
		return nullptr;
	}

	UComposableCameraActionBase* Action = DuplicateObject<UComposableCameraActionBase>(
		ActionTemplate, Outer);
	if (!Action)
	{
		return nullptr;
	}

	for (TFieldIterator<FProperty> It(Action->GetClass()); It; ++It)
	{
		FProperty* Property = *It;
		if (!IsExposableProperty(Property))
		{
			continue;
		}
		const FName Name = Property->GetFName();
		if (!Parameters.HasValue(Name))
		{
			continue;
		}

		EComposableCameraPinType PinType;
		UScriptStruct* StructType = nullptr;
		UEnum* EnumType = nullptr;
		UFunction* SignatureFunction = nullptr;
		if (!TryMapPropertyToPinType(Property, PinType, StructType, EnumType,
			&SignatureFunction))
		{
			continue;
		}
		void* Dest = Property->ContainerPtrToValuePtr<void>(Action);

		switch (PinType)
		{
		case EComposableCameraPinType::Bool:
		{
			bool Value = false;
			if (Parameters.Get(Name, Value))
			{
				CastFieldChecked<FBoolProperty>(Property)->SetPropertyValue(Dest, Value);
			}
			break;
		}
		case EComposableCameraPinType::Int32:
		{
			int32 Value = 0;
			if (Parameters.Get(Name, Value)) { CastFieldChecked<FIntProperty>(Property)->SetPropertyValue(Dest, Value); }
			break;
		}
		case EComposableCameraPinType::Float:
		{
			float Value = 0.f;
			if (Parameters.Get(Name, Value)) { CastFieldChecked<FFloatProperty>(Property)->SetPropertyValue(Dest, Value); }
			break;
		}
		case EComposableCameraPinType::Double:
		{
			double Value = 0.;
			if (Parameters.Get(Name, Value)) { CastFieldChecked<FDoubleProperty>(Property)->SetPropertyValue(Dest, Value); }
			break;
		}
		case EComposableCameraPinType::Name:
		{
			FName Value;
			if (Parameters.Get(Name, Value)) { CastFieldChecked<FNameProperty>(Property)->SetPropertyValue(Dest, Value); }
			break;
		}
		case EComposableCameraPinType::Actor:
		case EComposableCameraPinType::Object:
		{
			const FComposableCameraParameterValue* Entry = Parameters.Values.Find(Name);
			if (!Entry || Entry->PinType != PinType
				|| (PinType == EComposableCameraPinType::Actor
					? !Parameters.ActorValues.Contains(Name)
					: !Parameters.ObjectValues.Contains(Name)))
			{
				break;
			}
			UObject* Value = PinType == EComposableCameraPinType::Actor
				? Parameters.ActorValues.FindRef(Name).Get()
				: Parameters.ObjectValues.FindRef(Name).Get();
			if (Value && !IsValid(Value))
			{
				Value = nullptr;
			}
			const FObjectPropertyBase* ObjectProperty = CastFieldChecked<FObjectPropertyBase>(Property);
			if (!Value || !ObjectProperty->PropertyClass
				|| Value->IsA(ObjectProperty->PropertyClass))
			{
				ObjectProperty->SetObjectPropertyValue(Dest, Value);
			}
			break;
		}
		case EComposableCameraPinType::Enum:
		{
			int64 Value = 0;
			if (Parameters.Get(Name, Value))
			{
				if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
				{
					EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(Dest, Value);
				}
				else if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
				{
					ByteProperty->SetIntPropertyValue(Dest, Value);
				}
			}
			break;
		}
		case EComposableCameraPinType::Delegate:
		{
			const FScriptDelegate* Source = Parameters.DelegateValues.Find(Name);
			FDelegateProperty* DelegateProperty = CastFieldChecked<FDelegateProperty>(Property);
			if (Source && DelegateProperty->SignatureFunction)
			{
				const UObject* Bound = Source->GetUObject();
				UFunction* Function = Bound
					? Bound->FindFunction(Source->GetFunctionName()) : nullptr;
				if (!Bound || (Function && Function->IsSignatureCompatibleWith(
					DelegateProperty->SignatureFunction)))
				{
					*DelegateProperty->GetPropertyValuePtr_InContainer(Action) = *Source;
				}
			}
			break;
		}
		case EComposableCameraPinType::Struct:
		{
			const FInstancedStruct* Source = Parameters.StructValues.Find(Name);
			if (Source && Source->IsValid() && Source->GetScriptStruct() == StructType)
			{
				StructType->CopyScriptStruct(Dest, Source->GetMemory());
			}
			else if (IsBytewiseSafeStruct(StructType))
			{
				Parameters.CopyRawTo(Name, static_cast<uint8*>(Dest),
					StructType->GetStructureSize(), PinType);
			}
			break;
		}
		default:
		{
			const int32 Size = GetPinTypeSize(PinType, StructType);
			if (Size == Property->GetSize())
			{
				Parameters.CopyRawTo(Name, static_cast<uint8*>(Dest), Size, PinType);
			}
			break;
		}
		}
	}
	return Action;
}
