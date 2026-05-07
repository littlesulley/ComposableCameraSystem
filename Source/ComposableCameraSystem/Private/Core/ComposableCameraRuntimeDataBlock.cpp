// Copyright Sulley. All rights reserved.

#include "Core/ComposableCameraRuntimeDataBlock.h"

#include "UObject/GCObject.h"
#include "UObject/UObjectGlobals.h"

void FComposableCameraRuntimeDataBlock::RegisterReferenceSlot(EComposableCameraPinType PinType, int32 Offset)
{
	if (Offset < 0)
	{
		return;
	}
	// Struct slots own their own GC story via AddPropertyReferencesWithStructARO
	// in AddReferencedObjects -- don't double-track them in the Actor / Object
	// mirror maps. Mirror maps are scoped to raw-pointer slots in Storage.
	if (IsStructSlotOffset(Offset))
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
	// Struct slot writes don't need mirror refresh -- the FInstancedStruct
	// itself is the GC-visible owner. Cheap early-out keeps the per-write
	// hot path tight.
	if (IsStructSlotOffset(Offset))
	{
		return;
	}

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
	// Struct slots: two refs per slot to keep GC-safe.
	//
	//  (a) The slot's STORAGE — `AddPropertyReferencesWithStructARO` walks
	//      the script-struct's reflected property graph so embedded
	//      UObject / Actor references inside the user's USTRUCT are kept
	//      alive. FInstancedStruct itself is not a UObject; this ARO API
	//      is the only path to surface those.
	//
	//  (b) The slot's TYPE — the `UScriptStruct*` itself. For Blueprint
	//      `UserDefinedStruct` types (which ARE GC-eligible — unlike
	//      native USTRUCTs which live in the type registry forever), the
	//      runtime DataBlock is NOT a UPROPERTY container, and even
	//      though the camera now strongly owns its `SourceTypeAsset`
	//      (`TObjectPtr<UComposableCameraTypeAsset>`), the type asset's
	//      strong ref does not transitively keep individual
	//      UserDefinedStruct slot types alive — those are independent
	//      asset references reachable from the type asset's reflected
	//      pin metadata only. The runtime DataBlock has to mark slot
	//      types itself; a GC pass would otherwise reclaim a
	//      UserDefinedStruct mid-blend and the next
	//      `Slot.GetScriptStruct()` / `CopyScriptStruct(...)` would
	//      dereference a freed type pointer — heap corruption inside
	//      the property walk. Using a `TObjectPtr<UScriptStruct>` local
	//      satisfies the C4996-clean overload of `AddReferencedObject`.
	for (FInstancedStruct& Slot : StructSlots)
	{
		if (Slot.IsValid())
		{
			if (const UScriptStruct* ConstStruct = Slot.GetScriptStruct())
			{
				// AddReferencedObject expects a non-const ref; the slot's
				// type identity stays the same across the call (collector
				// only marks reachable in the mark phase, doesn't mutate).
				TObjectPtr<UScriptStruct> TypeRef = const_cast<UScriptStruct*>(ConstStruct);
				Collector.AddReferencedObject(TypeRef);
				Collector.AddPropertyReferencesWithStructARO(ConstStruct, Slot.GetMutableMemory());
			}
		}
	}

	// SlotShapes.StructType: walk and mark each recorded UScriptStruct so
	// shape metadata stays valid even for slots that aren't currently
	// populated in StructSlots (e.g. an exposed-parameter slot whose value
	// hasn't been assigned this activation). Same UserDefinedStruct
	// concern as above.
	for (auto& Pair : SlotShapes)
	{
		if (Pair.Value.StructType)
		{
			Collector.AddReferencedObject(Pair.Value.StructType);
		}
	}
}
