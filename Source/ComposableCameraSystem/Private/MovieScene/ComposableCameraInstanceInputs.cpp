// Copyright 2026 Sulley. All Rights Reserved.
#include "MovieScene/ComposableCameraInstanceData.h"
#include "Core/ComposableCameraRuntimeDataBlock.h"
#include "DataAssets/ComposableCameraTypeAsset.h"

namespace UE::ComposableCameras::InstanceInputs
{
template<typename T>
static void CopyStruct(const FInstancedPropertyBag& Bag, FName Name, FComposableCameraRuntimeDataBlock& Block, int32 Offset)
{
	if (auto Value = Bag.GetValueStruct<T>(Name); Value.HasValue()) Block.WriteValue<T>(Offset, *Value.GetValue());
}
static void Apply(const FInstancedPropertyBag& Bag, FName Name, EComposableCameraPinType Type,
	UScriptStruct* Struct, UEnum* Enum, FComposableCameraRuntimeDataBlock& Block, int32 Offset)
{
	const auto* Shape = Block.SlotShapes.Find(Offset);
	if (!Shape || Shape->PinType != Type) return;
	switch (Type)
	{
	case EComposableCameraPinType::Bool: if (auto Value = Bag.GetValueBool(Name); Value.HasValue()) Block.WriteValue<bool>(Offset, Value.GetValue()); break;
	case EComposableCameraPinType::Int32: if (auto Value = Bag.GetValueInt32(Name); Value.HasValue()) Block.WriteValue<int32>(Offset, Value.GetValue()); break;
	case EComposableCameraPinType::Float: if (auto Value = Bag.GetValueFloat(Name); Value.HasValue()) Block.WriteValue<float>(Offset, Value.GetValue()); break;
	case EComposableCameraPinType::Double: if (auto Value = Bag.GetValueDouble(Name); Value.HasValue()) Block.WriteValue<double>(Offset, Value.GetValue()); break;
	case EComposableCameraPinType::Name: if (auto Value = Bag.GetValueName(Name); Value.HasValue()) Block.WriteValue<FName>(Offset, Value.GetValue()); break;
	case EComposableCameraPinType::Enum: if (Enum) { if (auto Value = Bag.GetValueEnum(Name, Enum); Value.HasValue()) Block.WriteValue<int64>(Offset, Value.GetValue()); } break;
	case EComposableCameraPinType::Actor: if (auto Value = Bag.GetValueObject(Name, AActor::StaticClass()); Value.HasValue()) Block.WriteValue<AActor*>(Offset, Cast<AActor>(Value.GetValue())); break;
	case EComposableCameraPinType::Object: if (auto Value = Bag.GetValueObject(Name); Value.HasValue()) Block.WriteValue<UObject*>(Offset, Value.GetValue()); break;
	case EComposableCameraPinType::Vector2D: CopyStruct<FVector2D>(Bag, Name, Block, Offset); break;
	case EComposableCameraPinType::Vector3D: CopyStruct<FVector>(Bag, Name, Block, Offset); break;
	case EComposableCameraPinType::Vector4: CopyStruct<FVector4>(Bag, Name, Block, Offset); break;
	case EComposableCameraPinType::Rotator: CopyStruct<FRotator>(Bag, Name, Block, Offset); break;
	case EComposableCameraPinType::Transform: CopyStruct<FTransform>(Bag, Name, Block, Offset); break;
	case EComposableCameraPinType::Struct:
	{
		if (!Struct || Shape->StructType != Struct) break;
		auto Value = Bag.GetValueStruct(Name, Struct);
		if (!Value.HasValue() || Value.GetValue().GetScriptStruct() != Struct) break;
		if (Block.IsStructSlotOffset(Offset))
		{
			const int32 Index = Block.GetStructSlotIndex(Offset);
			if (!Block.StructSlots.IsValidIndex(Index)) break;
			FInstancedStruct& Slot = Block.StructSlots[Index];
			if (Slot.GetScriptStruct() != Struct) break;
			// Reuses the slot; a variable-sized member can allocate only when its authored
			// content grows. Numeric animation and object binding do not resize members.
			Struct->CopyScriptStruct(Slot.GetMutableMemory(), Value.GetValue().GetMemory());
		}
		else if (Shape->Size == Struct->GetStructureSize() && Offset >= 0 && Offset <= Block.Storage.Num() - Shape->Size)
			Struct->CopyScriptStruct(Block.Storage.GetData() + Offset, Value.GetValue().GetMemory());
		break;
	}
	case EComposableCameraPinType::Delegate: break;
	default: break;
	}
}
}

void UComposableCameraInstanceData::ApplyInputs(FComposableCameraRuntimeDataBlock& Block) const
{
	if (!Camera.TypeAsset) return;
	for (const auto& Parameter : Camera.TypeAsset->ExposedParameters)
		if (const int32* Offset = Block.ExposedParameterOffsets.Find(Parameter.ParameterName))
			UE::ComposableCameras::InstanceInputs::Apply(Camera.Parameters, Parameter.ParameterName, Parameter.PinType, Parameter.StructType, Parameter.EnumType, Block, *Offset);
	for (const auto& Variable : Camera.TypeAsset->ExposedVariables)
		if (const int32* Offset = Block.InternalVariableOffsets.Find(Variable.VariableName))
			UE::ComposableCameras::InstanceInputs::Apply(Camera.Variables, Variable.VariableName, Variable.VariableType, Variable.StructType, Variable.EnumType, Block, *Offset);
}
