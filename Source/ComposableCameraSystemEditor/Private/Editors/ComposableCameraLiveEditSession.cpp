// Copyright 2026 Sulley. All Rights Reserved.

#include "Editors/ComposableCameraLiveEditSession.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Editors/ComposableCameraNodeGraph.h"
#include "Editors/ComposableCameraNodeGraphNode.h"
#include "Editors/ComposableCameraNodeGraphSchema.h"
#include "ComposableCameraEdGraphPinTypeUtils.h"
#include "Editor.h"
#include "EdGraphSchema_K2.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "ScopedTransaction.h"
#include "Serialization/ArchiveUObject.h"
#include "Serialization/StructuredArchiveAdapters.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/LazyObjectPtr.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "ComposableCameraLiveEditSession"

namespace
{
	FString ExportValue(const FProperty* Property, const UObject* Object)
	{
		FString Result;
		Property->ExportTextItem_Direct(Result, Property->ContainerPtrToValuePtr<void>(Object), nullptr, const_cast<UObject*>(Object), PPF_None);
		return Result;
	}

	// Owned object paths differ between proxies. Compare editable contents instead.
	FString ValueSignature(const FProperty* Property, const void* Value, const UObject* Owner, int32 Depth = 0, bool bElement = false)
	{
		if (Depth > 24) return TEXT("<recursive>");
		if (!bElement && Property->ArrayDim > 1)
		{
			FString Result;
			for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
				Result += TEXT("[") + ValueSignature(Property, static_cast<const uint8*>(Value) + Index * Property->GetElementSize(), Owner, Depth + 1, true) + TEXT("]");
			return Result;
		}
		if (const auto* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			const UObject* Object = ObjectProperty->GetObjectPropertyValue(Value);
			if (Object && Property->HasAnyPropertyFlags(CPF_InstancedReference))
			{
				FString Result = Object->GetClass()->GetPathName();
				for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
					if (It->HasAnyPropertyFlags(CPF_Edit) && !It->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
						Result += TEXT("|") + It->GetName() + TEXT("=") + ValueSignature(*It, It->ContainerPtrToValuePtr<void>(Object), Object, Depth + 1);
				return Result;
			}
		}
		if (const auto* Struct = CastField<FStructProperty>(Property))
		{
			FString Result;
			for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
				if (!It->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
					Result += TEXT("|") + It->GetName() + TEXT("=") + ValueSignature(*It, It->ContainerPtrToValuePtr<void>(Value), Owner, Depth + 1);
			return Result;
		}
		if (const auto* Array = CastField<FArrayProperty>(Property))
		{
			FScriptArrayHelper Helper(Array, Value);
			FString Result;
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
				Result += TEXT("[") + ValueSignature(Array->Inner, Helper.GetRawPtr(Index), Owner, Depth + 1) + TEXT("]");
			return Result;
		}
		if (const auto* Map = CastField<FMapProperty>(Property))
		{
			FScriptMapHelper Helper(Map, Value);
			TArray<FString> Entries;
			for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
				if (Helper.IsValidIndex(Index)) Entries.Add(ValueSignature(Map->KeyProp, Helper.GetKeyPtr(Index), Owner, Depth + 1) + TEXT("=") + ValueSignature(Map->ValueProp, Helper.GetValuePtr(Index), Owner, Depth + 1));
			Entries.Sort();
			return FString::Join(Entries, TEXT("|"));
		}
		if (const auto* Set = CastField<FSetProperty>(Property))
		{
			FScriptSetHelper Helper(Set, Value);
			TArray<FString> Entries;
			for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
				if (Helper.IsValidIndex(Index)) Entries.Add(ValueSignature(Set->ElementProp, Helper.GetElementPtr(Index), Owner, Depth + 1));
			Entries.Sort();
			return FString::Join(Entries, TEXT("|"));
		}
		FString Result;
		Property->ExportTextItem_Direct(Result, Value, nullptr, const_cast<UObject*>(Owner), PPF_None);
		return Result;
	}
	FString Signature(const FProperty* Property, const UObject* Object)
	{
		return ValueSignature(Property, Property->ContainerPtrToValuePtr<void>(Object), Object);
	}
	void CopyValue(FProperty* Property, UObject* Source, UObject* Destination)
	{
		UComposableCameraCameraNodeBase::CopyLiveEditProperty(Property, Source, Destination);
	}
	// Reference-bearing map keys / set elements need fresh hashes after remapping.
	void RehashContainers(const FProperty* Property, void* Value)
	{
		for (int32 Element = 0; Element < Property->ArrayDim; ++Element)
		{
			void* Item = static_cast<uint8*>(Value) + Element * Property->GetElementSize();
			if (const auto* Struct = CastField<FStructProperty>(Property))
				for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It) RehashContainers(*It, It->ContainerPtrToValuePtr<void>(Item));
			else if (const auto* Array = CastField<FArrayProperty>(Property))
			{
				FScriptArrayHelper Helper(Array, Item);
				for (int32 Index = 0; Index < Helper.Num(); ++Index) RehashContainers(Array->Inner, Helper.GetRawPtr(Index));
			}
			else if (const auto* Map = CastField<FMapProperty>(Property))
			{
				FScriptMapHelper Helper(Map, Item);
				for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
					if (Helper.IsValidIndex(Index)) { RehashContainers(Map->KeyProp, Helper.GetKeyPtr(Index)); RehashContainers(Map->ValueProp, Helper.GetValuePtr(Index)); }
				Helper.Rehash();
			}
			else if (const auto* Set = CastField<FSetProperty>(Property))
			{
				FScriptSetHelper Helper(Set, Item);
				for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
					if (Helper.IsValidIndex(Index)) RehashContainers(Set->ElementProp, Helper.GetElementPtr(Index));
				Helper.Rehash();
			}
		}
	}
	class FWorldReferenceRemapper final : public FArchiveUObject
	{
	public:
		FWorldReferenceRemapper(UObject* InRoot, bool bInToRuntime, bool bInClearMissing, UWorld* InWorld)
			: Root(InRoot), TargetWorld(InWorld), bToRuntime(bInToRuntime), bClearMissing(bInClearMissing)
		{
			ArIsObjectReferenceCollector = true;
			ArIsModifyingWeakAndStrongReferences = true;
			ArIgnoreOuterRef = true;
			ArIgnoreArchetypeRef = true;
			Owned.Add(Root);
		}
		using FArchiveUObject::operator<<;
		virtual FArchive& operator<<(UObject*& Reference) override
		{
			if (!Reference || Reference == Root) return *this;
			if (Reference->IsIn(Root)) { Owned.AddUnique(Reference); return *this; }
			AActor* Actor = Cast<AActor>(Reference);
			if (!Actor) Actor = Reference->GetTypedOuter<AActor>();
			if (!Actor || !Actor->GetWorld()) return *this;
			const bool bIsPIE = Actor->GetWorld()->WorldType == EWorldType::PIE;
			if (bToRuntime ? (bIsPIE && (!TargetWorld || Actor->GetWorld() == TargetWorld)) : !bIsPIE) return *this;
			AActor* Counterpart = bToRuntime ? EditorUtilities::GetSimWorldCounterpartActor(Actor) : EditorUtilities::GetEditorWorldCounterpartActor(Actor);
			if (bToRuntime && TargetWorld && (!Counterpart || Counterpart->GetWorld() != TargetWorld))
			{
				Counterpart = nullptr;
				for (TActorIterator<AActor> It(TargetWorld); It; ++It)
					if (Actor->GetActorGuid().IsValid() && It->GetActorGuid() == Actor->GetActorGuid()) { Counterpart = *It; break; }
			}
			UObject* Replacement = Reference == Actor ? Counterpart : Counterpart ? FindObject<UObject>(Counterpart, *Reference->GetPathName(Actor)) : nullptr;
			if (Replacement) Reference = Replacement;
			else { bAllMapped = false; if (bClearMissing) Reference = nullptr; }
			return *this;
		}
		virtual FArchive& operator<<(FLazyObjectPtr& Value) override
		{
			if (UObject* Object = Value.Get()) { *this << Object; Value = Object; }
			return *this;
		}
		virtual FArchive& operator<<(FSoftObjectPtr& Value) override
		{
			if (UObject* Object = Value.Get()) { *this << Object; Value = Object; }
			return *this;
		}
		virtual FArchive& operator<<(FSoftObjectPath& Value) override
		{
			if (UObject* Object = Value.ResolveObject()) { *this << Object; Value = FSoftObjectPath(Object); }
			return *this;
		}
		bool Remap(FProperty* Property)
		{
			if (Property)
			{
				for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
					Property->SerializeItem(FStructuredArchiveFromArchive(*this).GetSlot(), Property->ContainerPtrToValuePtr<void>(Root.Get(), Index), nullptr);
				RehashContainers(Property, Property->ContainerPtrToValuePtr<void>(Root.Get()));
			}
			else Root->Serialize(*this);
			for (int32 Index = Property ? 1 : 0; Index < Owned.Num(); ++Index)
			{
				if (Index > 0) Owned[Index]->Serialize(*this);
				for (TFieldIterator<FProperty> It(Owned[Index]->GetClass()); It; ++It)
					RehashContainers(*It, It->ContainerPtrToValuePtr<void>(Owned[Index].Get()));
			}
			return bAllMapped;
		}
	private:
		TObjectPtr<UObject> Root;
		TObjectPtr<UWorld> TargetWorld;
		TArray<TObjectPtr<UObject>> Owned;
		bool bToRuntime;
		bool bClearMissing;
		bool bAllMapped = true;
	};
	bool RemapWorldReferences(UObject* Root, bool bToRuntime, bool bClearMissing, UWorld* TargetWorld = nullptr, FProperty* Property = nullptr)
	{
		return FWorldReferenceRemapper(Root, bToRuntime, bClearMissing, TargetWorld).Remap(Property);
	}
	bool ContainsObject(const FProperty* Property, const void* Value, const UObject* Object, bool bElement = false)
	{
		if (!bElement && Property->ArrayDim > 1)
		{
			for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
				if (ContainsObject(Property, static_cast<const uint8*>(Value) + Index * Property->GetElementSize(), Object, true)) return true;
			return false;
		}
		if (const auto* Reference = CastField<FObjectPropertyBase>(Property))
		{
			const UObject* Target = Reference->GetObjectPropertyValue(Value);
			return Target && (Target == Object || Object->IsIn(Target));
		}
		if (const auto* Struct = CastField<FStructProperty>(Property))
			for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
				if (ContainsObject(*It, It->ContainerPtrToValuePtr<void>(Value), Object)) return true;
		if (const auto* Array = CastField<FArrayProperty>(Property))
		{
			FScriptArrayHelper Helper(Array, Value);
			for (int32 Index = 0; Index < Helper.Num(); ++Index) if (ContainsObject(Array->Inner, Helper.GetRawPtr(Index), Object)) return true;
		}
		if (const auto* Map = CastField<FMapProperty>(Property))
		{
			FScriptMapHelper Helper(Map, Value);
			for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
				if (Helper.IsValidIndex(Index) && (ContainsObject(Map->KeyProp, Helper.GetKeyPtr(Index), Object) || ContainsObject(Map->ValueProp, Helper.GetValuePtr(Index), Object))) return true;
		}
		if (const auto* Set = CastField<FSetProperty>(Property))
		{
			FScriptSetHelper Helper(Set, Value);
			for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
				if (Helper.IsValidIndex(Index) && ContainsObject(Set->ElementProp, Helper.GetElementPtr(Index), Object)) return true;
		}
		return false;
	}
	bool FindPin(const UComposableCameraCameraNodeBase* Node, const FProperty* Property, FComposableCameraNodePinDeclaration& Pin)
	{
		return Node && Property && Node->FindInputPinForModifierProperty(Property, Pin);
	}
	FString SourceDefault(const UComposableCameraTypeAsset* Asset, const UComposableCameraCameraNodeBase* Template, FName Name)
	{
		TArray<FComposableCameraNodePinDeclaration> Pins;
		Template->GatherAllPinDeclarations(Pins);
		const FString Prefix = Name.ToString() + TEXT(".");
		const auto* Graph = Cast<UComposableCameraNodeGraph>(Asset->EditorGraph);
		const UComposableCameraNodeGraphNode* GraphNode = nullptr;
		if (Graph)
			for (const UEdGraphNode* Raw : Graph->Nodes)
			{
				const auto* Node = Cast<UComposableCameraNodeGraphNode>(Raw);
				if (Node && Node->NodeTemplate == Template) { GraphNode = Node; break; }
			}
		const int32 Index = Asset->NodeTemplates.IndexOfByPredicate([Template](const TObjectPtr<UComposableCameraCameraNodeBase>& Node) { return Node.Get() == Template; });
		FString Result;
		for (const auto& Pin : Pins)
		{
			if (Pin.PinName != Name && !Pin.PinName.ToString().StartsWith(Prefix)) continue;
			FString Value = GraphNode ? GraphNode->GetEffectivePinDefault(Pin) : Pin.DefaultValueString;
			if (!GraphNode && Asset->NodePinOverrides.IsValidIndex(Index))
				for (const auto& Override : Asset->NodePinOverrides[Index].Overrides)
					if (Override.PinName == Pin.PinName && Override.bHasDefaultOverride) { Value = Override.DefaultValueOverride; break; }
			Result += Pin.PinName.ToString() + TEXT("=") + Value + TEXT("|");
		}
		return Result;
	}
	bool PreservesDrivenPins(const UComposableCameraTypeAsset* Asset, const UComposableCameraCameraNodeBase* Template,
		const UComposableCameraCameraNodeBase* Candidate, const UComposableCameraNodeGraphNode* Target)
	{
		TArray<FComposableCameraNodePinDeclaration> OldPins, NewPins;
		Template->GatherAllPinDeclarations(OldPins);
		Candidate->GatherAllPinDeclarations(NewPins);
		const int32 Index = Asset->NodeTemplates.IndexOfByPredicate([Template](const auto& Node) { return Node.Get() == Template; });
		auto KeepsPin = [&](FName Name, EEdGraphPinDirection Direction, const FEdGraphPinType& OldType)
		{
			const auto* NewPin = NewPins.FindByPredicate([&](const auto& Pin)
			{
				return Pin.PinName == Name && (Pin.Direction == EComposableCameraPinDirection::Input ? EGPD_Input : EGPD_Output) == Direction;
			});
			if (!NewPin || ComposableCameraEdGraphPinTypeUtils::MakeEdGraphPinTypeFromCameraPinType(NewPin->PinType, NewPin->StructType, NewPin->EnumType, NewPin->SignatureFunction) != OldType) return false;
			if (Direction == EGPD_Output) return true;
			if (Target)
			{
				if (const auto* Override = Target->FindPinOverride(Name)) return Override->bAsPin;
			}
			else if (Asset->NodePinOverrides.IsValidIndex(Index))
				for (const auto& Override : Asset->NodePinOverrides[Index].Overrides)
					if (Override.PinName == Name) return Override.bAsPin;
			return NewPin->bDefaultAsPin;
		};
		auto KeepsDeclaredPin = [&](FName Name, EComposableCameraPinDirection Direction)
		{
			const auto* OldPin = OldPins.FindByPredicate([&](const auto& Pin) { return Pin.PinName == Name && Pin.Direction == Direction; });
			return OldPin && KeepsPin(Name, Direction == EComposableCameraPinDirection::Input ? EGPD_Input : EGPD_Output,
				ComposableCameraEdGraphPinTypeUtils::MakeEdGraphPinTypeFromCameraPinType(OldPin->PinType, OldPin->StructType, OldPin->EnumType, OldPin->SignatureFunction));
		};
		if (Target)
		{
			for (const UEdGraphPin* Pin : Target->Pins)
				if (!Pin->LinkedTo.IsEmpty() && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec && !KeepsPin(Pin->PinName, Pin->Direction, Pin->PinType)) return false;
		}
		else
		{
			for (const auto& Connection : Asset->PinConnections)
			{
				if (Connection.SourceNodeIndex == Index && !KeepsDeclaredPin(Connection.SourcePinName, EComposableCameraPinDirection::Output)) return false;
				if (Connection.TargetNodeIndex == Index && !KeepsDeclaredPin(Connection.TargetPinName, EComposableCameraPinDirection::Input)) return false;
			}
			for (const auto& Variable : Asset->VariableNodes)
				for (const auto& Connection : Variable.Connections)
					if (!Connection.bIsComputeChain && Connection.CameraNodeIndex == Index
						&& !KeepsDeclaredPin(Connection.CameraPinName, Variable.bIsSetter ? EComposableCameraPinDirection::Output : EComposableCameraPinDirection::Input)) return false;
		}
		for (const auto& Parameter : Asset->ExposedParameters)
			if (Parameter.TargetNodeIndex == Index && !KeepsDeclaredPin(Parameter.TargetPinName, EComposableCameraPinDirection::Input)) return false;
		return true;
	}
}

bool FComposableCameraLiveEditSession::IsLiveEditProperty(const FProperty* Property)
{
	return Property && Property->GetOwnerClass() != UComposableCameraCameraNodeBase::StaticClass()
		&& Property->HasAnyPropertyFlags(CPF_Edit)
		&& !Property->HasAnyPropertyFlags(CPF_EditConst | CPF_Transient | CPF_Deprecated);
}

bool FComposableCameraLiveEditSession::BindCamera(AComposableCameraCameraBase* InCamera)
{
	if (HasChanges() || !IsValid(InCamera) || !InCamera->GetWorld()
		|| InCamera->GetWorld()->WorldType != EWorldType::PIE || InCamera->GetWorld()->bIsTearingDown || !InCamera->SourceTypeAsset) return false;
	Reset();
	Nodes.Reset();
	Camera = InCamera;
	SourceAsset = InCamera->SourceTypeAsset.Get();
	SourceTemplates.Reset();
	RuntimeNodes.Reset();
	StartNodeOrder.Reset();
	for (int32 Index = 0; Index < InCamera->CameraNodes.Num(); ++Index)
	{
		UComposableCameraCameraNodeBase* Runtime = InCamera->CameraNodes[Index];
		auto* Template = SourceAsset->NodeTemplates.IsValidIndex(Index) ? SourceAsset->NodeTemplates[Index].Get() : nullptr;
		const bool bMatches = Runtime && Template && Runtime->GetClass() == Template->GetClass() && Runtime->GetFName() == Template->GetFName();
		SourceTemplates.Add(bMatches ? Template : nullptr);
		RuntimeNodes.Add(bMatches ? Runtime : nullptr);
	}
	// Start exec-wire order, never template-array or canvas-position order.
	if (!InCamera->FullExecChain.IsEmpty())
	{
		for (const auto& Step : InCamera->FullExecChain)
			if (Step.EntryType == EComposableCameraExecEntryType::CameraNode) StartNodeOrder.AddUnique(Step.CameraNodeIndex);
	}
	else StartNodeOrder = SourceAsset->ExecutionOrder;
	StartNodeOrder.RemoveAll([this](int32 Index) { return !RuntimeNodes.IsValidIndex(Index) || !RuntimeNodes[Index].IsValid() || !SourceTemplates[Index].IsValid(); });
	SelectedNodeIndex = INDEX_NONE;
	for (int32 Index : StartNodeOrder) SelectNode(Index);
	SelectedNodeIndex = StartNodeOrder.IsEmpty() ? INDEX_NONE : StartNodeOrder[0];
	return true;
}
TArray<int32> FComposableCameraLiveEditSession::GetNodeIndices() const { return StartNodeOrder; }
FText FComposableCameraLiveEditSession::GetNodeLabel(int32 Index) const
{
	const auto* Template = SourceTemplates.IsValidIndex(Index) ? SourceTemplates[Index].Get() : nullptr;
	return Template ? FText::Format(LOCTEXT("NodeLabel", "{0}. {1}  [Asset #{2}]"), FText::AsNumber(StartNodeOrder.IndexOfByKey(Index) + 1), Template->GetClass()->GetDisplayNameText(), FText::AsNumber(Index)) : FText();
}
bool FComposableCameraLiveEditSession::SelectNode(int32 Index)
{
	if (!IsLive() || !StartNodeOrder.Contains(Index)) return false;
	SelectedNodeIndex = Index;
	if (FindSelectedState()) return true;
	FNodeState& State = Nodes.AddDefaulted_GetRef();
	State.RuntimeIndex = Index;
	State.SourceTemplate = SourceTemplates[Index];
	State.RuntimeNode = RuntimeNodes[Index];
	State.EditingNode = DuplicateObject<UComposableCameraCameraNodeBase>(State.SourceTemplate.Get(), GetTransientPackage());
	State.BaselineNode = DuplicateObject<UComposableCameraCameraNodeBase>(State.SourceTemplate.Get(), GetTransientPackage());
	State.AcceptedNode = DuplicateObject<UComposableCameraCameraNodeBase>(State.SourceTemplate.Get(), GetTransientPackage());
	for (auto* Proxy : { State.EditingNode.Get(), State.BaselineNode.Get(), State.AcceptedNode.Get() })
	{
		Proxy->SetFlags(RF_Transient);
		Proxy->ClearFlags(RF_Public | RF_Standalone | RF_Transactional);
	}
	for (TFieldIterator<FProperty> It(State.EditingNode->GetClass()); It; ++It)
	{
		if (!IsLiveEditProperty(*It)) continue;
		CopyValue(*It, State.RuntimeNode.Get(), State.EditingNode.Get());
		FComposableCameraNodePinDeclaration Pin;
		if (FindPin(State.RuntimeNode.Get(), *It, Pin) && !State.RuntimeNode->HasModifierOverrideFieldOffset(It->GetOffset_ForInternal()))
			State.RuntimeNode->TryCopyUnderlyingInputPinToProperty(Pin, *It, State.EditingNode.Get());
	}
	RemapWorldReferences(State.EditingNode.Get(), false, false);
	for (TFieldIterator<FProperty> It(State.EditingNode->GetClass()); It; ++It)
	{
		if (!IsLiveEditProperty(*It)) continue;
		CopyValue(*It, State.EditingNode.Get(), State.BaselineNode.Get());
		CopyValue(*It, State.EditingNode.Get(), State.AcceptedNode.Get());
		State.AcceptedValues.Add(It->GetFName(), Signature(*It, State.EditingNode.Get()));
	}
	CaptureSourceValues(State);
	return true;
}
FComposableCameraLiveEditSession::FNodeState* FComposableCameraLiveEditSession::FindSelectedState()
{
	return Nodes.FindByPredicate([this](const FNodeState& State) { return State.RuntimeIndex == SelectedNodeIndex; });
}
const FComposableCameraLiveEditSession::FNodeState* FComposableCameraLiveEditSession::FindSelectedState() const
{
	return Nodes.FindByPredicate([this](const FNodeState& State) { return State.RuntimeIndex == SelectedNodeIndex; });
}
UComposableCameraCameraNodeBase* FComposableCameraLiveEditSession::GetEditingNode() const { return GetEditingNode(SelectedNodeIndex); }
UComposableCameraCameraNodeBase* FComposableCameraLiveEditSession::GetEditingNode(int32 Index) const
{
	const auto* State = Nodes.FindByPredicate([Index](const FNodeState& Entry) { return Entry.RuntimeIndex == Index; });
	return State ? State->EditingNode.Get() : nullptr;
}
int32 FComposableCameraLiveEditSession::FindEditingNode(UObject* Object, FName& Name) const
{
	for (const FNodeState& State : Nodes)
	{
		if (Object == State.EditingNode) return State.RuntimeIndex;
		if (!Object || !Object->IsIn(State.EditingNode.Get())) continue;
		for (TFieldIterator<FProperty> It(State.EditingNode->GetClass()); It; ++It)
		{
			if (!IsLiveEditProperty(*It)) continue;
			if (ContainsObject(*It, It->ContainerPtrToValuePtr<void>(State.EditingNode.Get()), Object)) { Name = It->GetFName(); return State.RuntimeIndex; }
		}
	}
	return INDEX_NONE;
}
bool FComposableCameraLiveEditSession::HasChanges() const { return Nodes.ContainsByPredicate([](const FNodeState& State) { return !State.ChangedProperties.IsEmpty(); }); }
bool FComposableCameraLiveEditSession::HasOverrides() const { return Nodes.ContainsByPredicate([](const FNodeState& State) { return !State.OverrideProperties.IsEmpty(); }); }
bool FComposableCameraLiveEditSession::IsLive() const
{
	const auto* Cam = Camera.Get();
	return Cam && Cam->GetWorld() && Cam->GetWorld()->WorldType == EWorldType::PIE && !Cam->GetWorld()->bIsTearingDown;
}
bool FComposableCameraLiveEditSession::CanEditProperty(FName Name, FString& Reason) const
{
	const auto* State = FindSelectedState();
	if (!IsLive() || !State || !State->RuntimeNode.IsValid()) { Reason = TEXT("PIE ended or camera unavailable"); return false; }
	const FProperty* Property = FindFProperty<FProperty>(State->RuntimeNode->GetClass(), Name);
	if (!IsLiveEditProperty(Property) || !Camera->CameraNodes.IsValidIndex(State->RuntimeIndex) || Camera->CameraNodes[State->RuntimeIndex] != State->RuntimeNode.Get()) { Reason = TEXT("Runtime node unavailable"); return false; }
	Reason = TEXT("Trial overrides all driven layers; Reset resumes current driver");
	return true;
}
bool FComposableCameraLiveEditSession::WriteRuntimeProperty(FNodeState& State, FName Name, UObject* Values, bool bRestoring)
{
	if (!IsLive() || !State.RuntimeNode.IsValid() || !Camera->CameraNodes.IsValidIndex(State.RuntimeIndex) || Camera->CameraNodes[State.RuntimeIndex] != State.RuntimeNode.Get()) return false;
	FProperty* Property = FindFProperty<FProperty>(State.EditingNode->GetClass(), Name);
	if (!IsLiveEditProperty(Property)) return false;
	if (bRestoring) State.RuntimeNode->RemoveLiveEditProperty(Name);
	else
	{
		TStrongObjectPtr<UComposableCameraCameraNodeBase> Candidate(NewObject<UComposableCameraCameraNodeBase>(GetTransientPackage(), Values->GetClass(), NAME_None, RF_Transient));
		CopyValue(Property, Values, Candidate.Get());
		if (!RemapWorldReferences(Candidate.Get(), true, false, Camera->GetWorld(), Property)) return false;
		State.RuntimeNode->SetLiveEditProperty(Property, Candidate.Get());
	}
	State.RuntimeNode->RefreshLiveEditState(Name);
	return true;
}
bool FComposableCameraLiveEditSession::ApplyProperty(FName Name, FString& Reason)
{
	auto* State = FindSelectedState();
	if (!State || !State->AcceptedValues.Contains(Name)) return false;
	FProperty* Property = FindFProperty<FProperty>(State->EditingNode->GetClass(), Name);
	if (!CanEditProperty(Name, Reason))
	{
		CopyValue(Property, State->AcceptedNode.Get(), State->EditingNode.Get());
		return false;
	}
	if (!WriteRuntimeProperty(*State, Name, State->EditingNode.Get(), false))
	{
		Reason = TEXT("Runtime node changed, or actor reference has no counterpart in the selected PIE world");
		CopyValue(Property, State->AcceptedNode.Get(), State->EditingNode.Get());
		return false;
	}
	CopyValue(Property, State->EditingNode.Get(), State->AcceptedNode.Get());
	State->AcceptedValues[Name] = Signature(Property, State->EditingNode.Get());
	State->OverrideProperties.Add(Name);
	if (Signature(Property, State->EditingNode.Get()) == Signature(Property, State->BaselineNode.Get())) State->ChangedProperties.Remove(Name);
	else State->ChangedProperties.Add(Name);
	return true;
}
void FComposableCameraLiveEditSession::CaptureSourceValues(FNodeState& State)
{
	const auto* Asset = SourceAsset.Get();
	const auto* Template = State.SourceTemplate.Get();
	if (!Asset || !Template) return;
	for (const auto& Pair : State.AcceptedValues)
	{
		const FProperty* Property = FindFProperty<FProperty>(Template->GetClass(), Pair.Key);
		State.SourceDefaults.Add(Pair.Key, SourceDefault(Asset, Template, Pair.Key));
		State.SourceValues.Add(Pair.Key, Signature(Property, Template));
	}
}
bool FComposableCameraLiveEditSession::ApplyToAsset(FString& Reason)
{
	auto* Asset = SourceAsset.Get();
	if (!Asset || !HasChanges()) { Reason = TEXT("No pending changes or source asset unavailable"); return false; }
	auto* Graph = Cast<UComposableCameraNodeGraph>(Asset->EditorGraph);
	TArray<TStrongObjectPtr<UComposableCameraCameraNodeBase>> Candidates;
	Candidates.SetNum(Nodes.Num());
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		const FNodeState& State = Nodes[Index];
		if (State.ChangedProperties.IsEmpty()) continue;
		const auto* Template = State.SourceTemplate.Get();
		if (!Template || !Asset->NodeTemplates.Contains(Template)) { Reason = TEXT("Source node removed or rebuilt; pending values retained"); return false; }
		Candidates[Index].Reset(DuplicateObject<UComposableCameraCameraNodeBase>(State.SourceTemplate.Get(), GetTransientPackage()));
		auto* Candidate = Candidates[Index].Get();
		Candidate->SetFlags(RF_Transient);
		Candidate->ClearFlags(RF_Public | RF_Standalone | RF_Transactional);
		for (FName Name : State.ChangedProperties)
		{
			FProperty* Property = FindFProperty<FProperty>(Template->GetClass(), Name);
			if (!IsLiveEditProperty(Property) || SourceDefault(Asset, Template, Name) != State.SourceDefaults[Name] || Signature(Property, Template) != State.SourceValues[Name]) { Reason = TEXT("Source value changed since trial began; pending values retained"); return false; }
			CopyValue(Property, State.EditingNode.Get(), Candidate);
			if (!RemapWorldReferences(Candidate, false, false, nullptr, Property)) { Reason = TEXT("An edited PIE-only actor/object has no saved counterpart. Choose an authoring actor/asset before Apply."); return false; }
		}
		const UComposableCameraNodeGraphNode* Target = nullptr;
		if (Graph)
			for (const UEdGraphNode* Raw : Graph->Nodes)
			{
				const auto* Node = Cast<UComposableCameraNodeGraphNode>(Raw);
				if (Node && Node->NodeTemplate == Template) { Target = Node; break; }
			}
		if (Graph && !Target) { Reason = TEXT("Source graph node unavailable; pending values retained"); return false; }
		if (!PreservesDrivenPins(Asset, Template, Candidate, Target)) { Reason = TEXT("Apply would remove or change a wired / exposed pin. Reset this schema change first; pending values retained."); return false; }
	}
	if (!Graph)
	{
		Graph = NewObject<UComposableCameraNodeGraph>(Asset, NAME_None, RF_Transactional | RF_Transient);
		Graph->Schema = UComposableCameraNodeGraphSchema::StaticClass();
		Graph->OwningTypeAsset = Asset;
		Asset->EditorGraph = Graph;
		Graph->RebuildFromTypeAsset();
	}
	TArray<UComposableCameraNodeGraphNode*> Targets;
	for (const FNodeState& State : Nodes)
	{
		UComposableCameraNodeGraphNode* Target = nullptr;
		if (!State.ChangedProperties.IsEmpty())
		{
			for (UEdGraphNode* Raw : Graph->Nodes)
			{
				auto* Node = Cast<UComposableCameraNodeGraphNode>(Raw);
				if (Node && Node->NodeTemplate == State.SourceTemplate.Get()) { Target = Node; break; }
			}
			if (!Target) { Reason = TEXT("Source graph node unavailable; pending values retained"); return false; }
		}
		Targets.Add(Target);
	}
	const FScopedTransaction Transaction(LOCTEXT("ApplyTrial", "Apply PIE camera trial to asset"));
	Asset->Modify();
	Graph->Modify();
	{
		TGuardValue<bool> SyncGuard(Graph->bIsSyncingToTypeAsset, true);
		for (int32 Index = 0; Index < Nodes.Num(); ++Index)
		{
			FNodeState& State = Nodes[Index];
			if (!Targets[Index]) continue;
			Targets[Index]->Modify();
			auto* Template = State.SourceTemplate.Get();
			Template->Modify();
			for (FName Name : State.ChangedProperties)
			{
				FProperty* Property = FindFProperty<FProperty>(Template->GetClass(), Name);
				CopyValue(Property, Candidates[Index].Get(), Template);
				FComposableCameraNodePinDeclaration Pin;
				if (FindPin(Template, Property, Pin) && Pin.PinType != EComposableCameraPinType::Delegate)
					Targets[Index]->SetPinDefaultOverride(Name, ExportValue(Property, Template));
				const auto* Parent = CastField<FObjectPropertyBase>(Property);
				UObject* Subobject = Parent ? Parent->GetObjectPropertyValue_InContainer(Template) : nullptr;
				if (Subobject && Property->HasAnyPropertyFlags(CPF_InstancedReference))
				{
					TArray<FComposableCameraNodePinDeclaration> Pins;
					Template->GatherAllPinDeclarations(Pins);
					for (const auto& ChildPin : Pins)
					{
						FString RootName, ChildName;
						if (!ChildPin.PinName.ToString().Split(TEXT("."), &RootName, &ChildName) || FName(*RootName) != Name) continue;
						if (const FProperty* Child = FindFProperty<FProperty>(Subobject->GetClass(), FName(*ChildName)))
							Targets[Index]->SetPinDefaultOverride(ChildPin.PinName, ExportValue(Child, Subobject));
					}
				}
				if (Property->HasAnyPropertyFlags(CPF_InstancedReference | CPF_ContainsInstancedReference))
					Targets[Index]->PruneObsoletePinOverrides(Name);
			}
			Targets[Index]->ReconstructPins();
		}
	}
	Graph->SyncToTypeAsset();
	{
		TGuardValue<bool> SyncGuard(Graph->bIsSyncingToTypeAsset, true);
		Graph->NotifyGraphChanged(); // Refresh open editors without a redundant sync.
	}
	Asset->PostEditChange();
	Asset->MarkPackageDirty();
	for (FNodeState& State : Nodes)
	{
		for (FName Name : State.ChangedProperties) CopyValue(FindFProperty<FProperty>(State.EditingNode->GetClass(), Name), State.EditingNode.Get(), State.BaselineNode.Get());
		State.ChangedProperties.Reset();
		CaptureSourceValues(State);
	}
	Reason = TEXT("Applied authoring defaults. Save the asset. Trial stays active; Reset resumes graph / Modifier drivers.");
	return true;
}
bool FComposableCameraLiveEditSession::Reset()
{
	for (FNodeState& State : Nodes)
		for (FName Name : State.OverrideProperties.Array())
		{
			if (IsLive() && !WriteRuntimeProperty(State, Name, State.BaselineNode.Get(), true)) continue;
			FProperty* Property = FindFProperty<FProperty>(State.EditingNode->GetClass(), Name);
			if (IsLive())
			{
				CopyValue(Property, State.RuntimeNode.Get(), State.EditingNode.Get());
				FComposableCameraNodePinDeclaration Pin;
				if (FindPin(State.RuntimeNode.Get(), Property, Pin) && !State.RuntimeNode->HasModifierOverrideFieldOffset(Property->GetOffset_ForInternal()))
					State.RuntimeNode->TryCopyUnderlyingInputPinToProperty(Pin, Property, State.EditingNode.Get());
				RemapWorldReferences(State.EditingNode.Get(), false, false, nullptr, Property);
			}
			else CopyValue(Property, State.BaselineNode.Get(), State.EditingNode.Get());
			CopyValue(Property, State.EditingNode.Get(), State.BaselineNode.Get());
			CopyValue(Property, State.BaselineNode.Get(), State.AcceptedNode.Get());
			State.AcceptedValues[Name] = Signature(Property, State.EditingNode.Get());
			State.ChangedProperties.Remove(Name);
			State.OverrideProperties.Remove(Name);
		}
	if (HasOverrides()) return false;
	// Undo / concurrent source edits invalidate old conflict snapshots. A complete
	// Reset starts a fresh trial against the current authoring state.
	for (FNodeState& State : Nodes) CaptureSourceValues(State);
	return true;
}
void FComposableCameraLiveEditSession::DetachRuntime()
{
	Camera.Reset();
	for (auto& Node : RuntimeNodes) Node.Reset();
	for (FNodeState& State : Nodes)
	{
		State.RuntimeNode.Reset();
		for (auto* Proxy : { State.EditingNode.Get(), State.BaselineNode.Get(), State.AcceptedNode.Get() }) RemapWorldReferences(Proxy, false, true);
	}
}
FString FComposableCameraLiveEditSession::DescribeProperties() const
{
	return IsLive() ? TEXT("All editable Start-chain parameters. Trial overrides wires / Modifiers; Reset resumes their current values.") : TEXT("Choose a PIE camera. Only Start-chain nodes appear; pending defaults can be applied after PIE.");
}
void FComposableCameraLiveEditSession::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (FNodeState& State : Nodes)
	{
		Collector.AddReferencedObject(State.EditingNode);
		Collector.AddReferencedObject(State.BaselineNode);
		Collector.AddReferencedObject(State.AcceptedNode);
	}
}
#undef LOCTEXT_NAMESPACE
