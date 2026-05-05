// Copyright Sulley. All rights reserved.

#include "Core/ComposableCameraRuntimeDataBlock.h"

#include "UObject/GCObject.h"

void FComposableCameraRuntimeDataBlock::RegisterReferenceSlot(EComposableCameraPinType PinType, int32 Offset)
{
	if (Offset < 0)
	{
		return;
	}

	if (PinType == EComposableCameraPinType::Actor)
	{
		ActorReferenceSlots.Add(Offset, nullptr);
	}
	else if (PinType == EComposableCameraPinType::Object)
	{
		ObjectReferenceSlots.Add(Offset, nullptr);
	}
}

void FComposableCameraRuntimeDataBlock::RefreshReferenceSlot(int32 Offset)
{
	if (ActorReferenceSlots.Num() == 0 && ObjectReferenceSlots.Num() == 0)
	{
		return;
	}

	if (TObjectPtr<AActor>* ActorSlot = ActorReferenceSlots.Find(Offset))
	{
		AActor* ActorValue = nullptr;
		if (Offset >= 0 && Offset + static_cast<int32>(sizeof(AActor*)) <= Storage.Num())
		{
			FMemory::Memcpy(&ActorValue, Storage.GetData() + Offset, sizeof(AActor*));
		}
		*ActorSlot = ActorValue;
	}

	if (TObjectPtr<UObject>* ObjectSlot = ObjectReferenceSlots.Find(Offset))
	{
		UObject* ObjectValue = nullptr;
		if (Offset >= 0 && Offset + static_cast<int32>(sizeof(UObject*)) <= Storage.Num())
		{
			FMemory::Memcpy(&ObjectValue, Storage.GetData() + Offset, sizeof(UObject*));
		}
		*ObjectSlot = ObjectValue;
	}
}

void FComposableCameraRuntimeDataBlock::RefreshAllReferenceSlots()
{
	TArray<int32> Offsets;
	ActorReferenceSlots.GetKeys(Offsets);
	for (const int32 Offset : Offsets)
	{
		RefreshReferenceSlot(Offset);
	}

	Offsets.Reset();
	ObjectReferenceSlots.GetKeys(Offsets);
	for (const int32 Offset : Offsets)
	{
		RefreshReferenceSlot(Offset);
	}
}

void FComposableCameraRuntimeDataBlock::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (auto& Pair : ActorReferenceSlots)
	{
		Collector.AddReferencedObject(Pair.Value);
	}
	for (auto& Pair : ObjectReferenceSlots)
	{
		Collector.AddReferencedObject(Pair.Value);
	}
}
