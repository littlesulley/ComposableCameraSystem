// Copyright 2026 Sulley. All Rights Reserved.

#include "Modifiers/ComposableCameraModifierRuntimeState.h"

#include "Cameras/ComposableCameraCameraBase.h"
#include "ComposableCameraSystemModule.h"
#include "DataAssets/ComposableCameraModifierDataAsset.h"
#include "Modifiers/ComposableCameraModifierBase.h"
#include "Modifiers/ComposableCameraModifierTransition.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	template<typename T>
	T ReadStructValue(const FProperty* Property, const UObject* Container)
	{
		T Result {};
		const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
		if (StructProperty
			&& StructProperty->Struct == TBaseStructure<T>::Get()
			&& Container)
		{
			StructProperty->Struct->CopyScriptStruct(
				&Result,
				StructProperty->ContainerPtrToValuePtr<void>(Container));
		}
		return Result;
	}

	template<typename T>
	void WriteStructValue(const FProperty* Property, UObject* Container, const T& Value)
	{
		const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
		if (StructProperty
			&& StructProperty->Struct == TBaseStructure<T>::Get()
			&& Container)
		{
			StructProperty->Struct->CopyScriptStruct(
				StructProperty->ContainerPtrToValuePtr<void>(Container),
				&Value);
		}
	}

	void CopyPropertyValue(
		const FProperty* Property,
		UObject* Destination,
		const UObject* Source)
	{
		if (!Property || !Destination || !Source)
		{
			return;
		}

		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property);
			ObjectProperty && Property->HasAnyPropertyFlags(CPF_InstancedReference))
		{
			UObject* SourceObject = ObjectProperty->GetObjectPropertyValue_InContainer(Source);
			UObject* Duplicate = SourceObject
				? StaticDuplicateObject(SourceObject, Destination)
				: nullptr;
			ObjectProperty->SetObjectPropertyValue_InContainer(Destination, Duplicate);
			return;
		}

		Property->CopyCompleteValue_InContainer(Destination, Source);
	}

	template<typename T>
	T ReadLowerOrBaseline(
		const FComposableCameraModifierNodeState& NodeState,
		const FComposableCameraModifierPropertyState& PropertyState)
	{
		T Value {};
		if (PropertyState.bHasInputPin && NodeState.Node)
		{
			if constexpr (std::is_same_v<T, FLinearColor>)
			{
				// FLinearColor is a generic Struct pin in UE5.6, not a
				// CStaticStructProvider. Reuse the non-templated struct-slot
				// copy path, then read the temporary lower value before the
				// final blended result overwrites the runtime node.
				if (NodeState.Node->TryCopyUnderlyingInputPinToProperty(
					PropertyState.InputPin,
					PropertyState.Property,
					NodeState.Node.Get()))
				{
					return ReadStructValue<T>(
						PropertyState.Property, NodeState.Node.Get());
				}
			}
			else if (NodeState.Node->TryResolveUnderlyingInputPin<T>(
				PropertyState.InputPin.PinName, Value))
			{
				return Value;
			}
		}
		return ReadStructValue<T>(
			PropertyState.Property, NodeState.BaselineSnapshot.Get());
	}

	float ReadFloat(const FProperty* Property, const UObject* Container)
	{
		if (const FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property);
			FloatProperty && Container)
		{
			return FloatProperty->GetPropertyValue_InContainer(Container);
		}
		return 0.f;
	}

	double ReadDouble(const FProperty* Property, const UObject* Container)
	{
		if (const FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property);
			DoubleProperty && Container)
		{
			return DoubleProperty->GetPropertyValue_InContainer(Container);
		}
		return 0.0;
	}

	float ReadLowerFloat(
		const FComposableCameraModifierNodeState& NodeState,
		const FComposableCameraModifierPropertyState& PropertyState)
	{
		float Value = 0.f;
		if (PropertyState.bHasInputPin
			&& NodeState.Node
			&& NodeState.Node->TryResolveUnderlyingInputPin<float>(
				PropertyState.InputPin.PinName, Value))
		{
			return Value;
		}
		return ReadFloat(PropertyState.Property, NodeState.BaselineSnapshot.Get());
	}

	double ReadLowerDouble(
		const FComposableCameraModifierNodeState& NodeState,
		const FComposableCameraModifierPropertyState& PropertyState)
	{
		double Value = 0.0;
		if (PropertyState.bHasInputPin
			&& NodeState.Node
			&& NodeState.Node->TryResolveUnderlyingInputPin<double>(
				PropertyState.InputPin.PinName, Value))
		{
			return Value;
		}
		return ReadDouble(PropertyState.Property, NodeState.BaselineSnapshot.Get());
	}

	template<typename T>
	T ResolveStructEndpoint(
		const FComposableCameraModifierNodeState& NodeState,
		const FComposableCameraModifierPropertyState& PropertyState,
		bool bUseLower,
		const UObject* SnapshotOrTemplate)
	{
		return bUseLower
			? ReadLowerOrBaseline<T>(NodeState, PropertyState)
			: ReadStructValue<T>(PropertyState.Property, SnapshotOrTemplate);
	}

	bool ApplyContinuousValue(
		FComposableCameraModifierNodeState& NodeState,
		FComposableCameraModifierPropertyState& PropertyState,
		float Weight)
	{
		UComposableCameraCameraNodeBase* Node = NodeState.Node.Get();
		if (!Node || !PropertyState.Property)
		{
			return false;
		}

		const bool bTargetUsesLower =
			PropertyState.Phase == EComposableCameraModifierPropertyPhase::Exiting;
		const UObject* SourceObject = PropertyState.SourceSnapshot
			? static_cast<const UObject*>(PropertyState.SourceSnapshot.Get())
			: static_cast<const UObject*>(NodeState.BaselineSnapshot.Get());
		const UObject* TargetObject = PropertyState.TargetTemplate.Get();

		if (const FFloatProperty* FloatProperty =
			CastField<FFloatProperty>(PropertyState.Property))
		{
			const float Source = PropertyState.bSourceUsesLiveLower
				? ReadLowerFloat(NodeState, PropertyState)
				: ReadFloat(PropertyState.Property, SourceObject);
			const float Target = bTargetUsesLower
				? ReadLowerFloat(NodeState, PropertyState)
				: ReadFloat(PropertyState.Property, TargetObject);
			FloatProperty->SetPropertyValue_InContainer(Node, FMath::Lerp(Source, Target, Weight));
			return true;
		}

		if (const FDoubleProperty* DoubleProperty =
			CastField<FDoubleProperty>(PropertyState.Property))
		{
			const double Source = PropertyState.bSourceUsesLiveLower
				? ReadLowerDouble(NodeState, PropertyState)
				: ReadDouble(PropertyState.Property, SourceObject);
			const double Target = bTargetUsesLower
				? ReadLowerDouble(NodeState, PropertyState)
				: ReadDouble(PropertyState.Property, TargetObject);
			DoubleProperty->SetPropertyValue_InContainer(Node, FMath::Lerp(Source, Target, Weight));
			return true;
		}

		const FStructProperty* StructProperty =
			CastField<FStructProperty>(PropertyState.Property);
		if (!StructProperty)
		{
			return false;
		}

		if (StructProperty->Struct == TBaseStructure<FVector2D>::Get())
		{
			const FVector2D Source = ResolveStructEndpoint<FVector2D>(
				NodeState, PropertyState, PropertyState.bSourceUsesLiveLower, SourceObject);
			const FVector2D Target = ResolveStructEndpoint<FVector2D>(
				NodeState, PropertyState, bTargetUsesLower, TargetObject);
			WriteStructValue(PropertyState.Property, Node, FMath::Lerp(Source, Target, Weight));
			return true;
		}
		if (StructProperty->Struct == TBaseStructure<FVector>::Get())
		{
			const FVector Source = ResolveStructEndpoint<FVector>(
				NodeState, PropertyState, PropertyState.bSourceUsesLiveLower, SourceObject);
			const FVector Target = ResolveStructEndpoint<FVector>(
				NodeState, PropertyState, bTargetUsesLower, TargetObject);
			WriteStructValue(PropertyState.Property, Node, FMath::Lerp(Source, Target, Weight));
			return true;
		}
		if (StructProperty->Struct == TBaseStructure<FVector4>::Get())
		{
			const FVector4 Source = ResolveStructEndpoint<FVector4>(
				NodeState, PropertyState, PropertyState.bSourceUsesLiveLower, SourceObject);
			const FVector4 Target = ResolveStructEndpoint<FVector4>(
				NodeState, PropertyState, bTargetUsesLower, TargetObject);
			WriteStructValue(PropertyState.Property, Node, FMath::Lerp(Source, Target, Weight));
			return true;
		}
		if (StructProperty->Struct == TBaseStructure<FRotator>::Get())
		{
			const FRotator Source = ResolveStructEndpoint<FRotator>(
				NodeState, PropertyState, PropertyState.bSourceUsesLiveLower, SourceObject);
			const FRotator Target = ResolveStructEndpoint<FRotator>(
				NodeState, PropertyState, bTargetUsesLower, TargetObject);
			const FRotator Result =
				FQuat::Slerp(Source.Quaternion(), Target.Quaternion(), Weight).Rotator();
			WriteStructValue(PropertyState.Property, Node, Result);
			return true;
		}
		if (StructProperty->Struct == TBaseStructure<FTransform>::Get())
		{
			const FTransform Source = ResolveStructEndpoint<FTransform>(
				NodeState, PropertyState, PropertyState.bSourceUsesLiveLower, SourceObject);
			const FTransform Target = ResolveStructEndpoint<FTransform>(
				NodeState, PropertyState, bTargetUsesLower, TargetObject);
			FTransform Result;
			Result.Blend(Source, Target, Weight);
			WriteStructValue(PropertyState.Property, Node, Result);
			return true;
		}
		if (StructProperty->Struct == TBaseStructure<FLinearColor>::Get())
		{
			const FLinearColor Source = ResolveStructEndpoint<FLinearColor>(
				NodeState, PropertyState, PropertyState.bSourceUsesLiveLower, SourceObject);
			const FLinearColor Target = ResolveStructEndpoint<FLinearColor>(
				NodeState, PropertyState, bTargetUsesLower, TargetObject);
			WriteStructValue(PropertyState.Property, Node, FMath::Lerp(Source, Target, Weight));
			return true;
		}
		return false;
	}

	void ApplyLowerOrBaseline(
		FComposableCameraModifierNodeState& NodeState,
		FComposableCameraModifierPropertyState& PropertyState)
	{
		if (PropertyState.bHasInputPin
			&& NodeState.Node
			&& NodeState.Node->TryCopyUnderlyingInputPinToProperty(
				PropertyState.InputPin,
				PropertyState.Property,
				NodeState.Node.Get()))
		{
			return;
		}
		CopyPropertyValue(
			PropertyState.Property,
			NodeState.Node.Get(),
			NodeState.BaselineSnapshot.Get());
	}

	enum class EModifierPropertyTransitionKind : uint8
	{
		Enter,
		Replace,
		Exit
	};

	UComposableCameraModifierTransitionBase* ResolvePropertyTransition(
		EModifierPropertyTransitionKind Kind,
		const UComposableCameraNodeModifierDataAsset* PreviousAsset,
		const UComposableCameraNodeModifierDataAsset* DesiredAsset,
		UComposableCameraModifierTransitionBase* TransitionOverride,
		bool bResolveTransitionsFromAssets)
	{
		if (!bResolveTransitionsFromAssets)
		{
			return TransitionOverride;
		}

		switch (Kind)
		{
		case EModifierPropertyTransitionKind::Enter:
			return DesiredAsset
				? DesiredAsset->OverrideEnterValueTransition.Get()
				: nullptr;
		case EModifierPropertyTransitionKind::Replace:
			if (!DesiredAsset)
			{
				return nullptr;
			}
			if (DesiredAsset->OverrideReplaceValueTransition)
			{
				return DesiredAsset->OverrideReplaceValueTransition.Get();
			}
			if (!PreviousAsset
				|| DesiredAsset->Priority >= PreviousAsset->Priority)
			{
				return DesiredAsset->OverrideEnterValueTransition.Get();
			}
			return PreviousAsset->OverrideExitValueTransition.Get();
		case EModifierPropertyTransitionKind::Exit:
			return PreviousAsset
				? PreviousAsset->OverrideExitValueTransition.Get()
				: nullptr;
		}
		return nullptr;
	}

	ComposableCameraModifier::T_EffectiveModifier ExpandLegacyEffectiveModifiers(
		const ComposableCameraModifier::T_NodeModifier& EffectiveModifiers)
	{
		ComposableCameraModifier::T_EffectiveModifier Result;
		for (const auto& NodePair : EffectiveModifiers)
		{
			const ComposableCameraModifier::FModifierEntry& Entry =
				NodePair.Value;
			if (!Entry.Modifier)
			{
				continue;
			}

			ComposableCameraModifier::T_PropertyModifier& Properties =
				Result.FindOrAdd(NodePair.Key);
			if (!Entry.Modifier->UsesNodeTemplateOverride())
			{
				Properties.Add(NAME_None, Entry);
				continue;
			}

			for (const FName PropertyName
				: Entry.Modifier->OverrideProperties)
			{
				if (!PropertyName.IsNone())
				{
					Properties.Add(PropertyName, Entry);
				}
			}
			if (Properties.IsEmpty())
			{
				Result.Remove(NodePair.Key);
			}
		}
		return Result;
	}

	const ComposableCameraModifier::FModifierEntry* FindInPlacePropertyEntry(
		const ComposableCameraModifier::T_PropertyModifier* DesiredModifiers,
		FName PropertyName)
	{
		const ComposableCameraModifier::FModifierEntry* Entry =
			DesiredModifiers
				? DesiredModifiers->Find(PropertyName)
				: nullptr;
		return Entry
			&& Entry->Asset
			&& Entry->Asset->ApplyMode
				== EComposableCameraModifierApplyMode::ModifyExistingInstance
			&& Entry->Modifier
			&& Entry->Modifier->UsesNodeTemplateOverride()
				? Entry
				: nullptr;
	}

	bool HasInPlacePropertyEntry(
		const ComposableCameraModifier::T_PropertyModifier* DesiredModifiers)
	{
		if (!DesiredModifiers)
		{
			return false;
		}
		for (const auto& PropertyPair : *DesiredModifiers)
		{
			if (!PropertyPair.Key.IsNone()
				&& FindInPlacePropertyEntry(
					DesiredModifiers, PropertyPair.Key))
			{
				return true;
			}
		}
		return false;
	}
}

void UComposableCameraModifierRuntimeState::ApplyInitialEffectiveModifiers(
	AComposableCameraCameraBase* Camera,
	const ComposableCameraModifier::T_EffectiveModifier& EffectiveModifiers)
{
	bSuppressNotifications = true;
	ReconcileModifiersInternal(
		Camera, EffectiveModifiers, nullptr, true, true);
	bSuppressNotifications = false;
}

void UComposableCameraModifierRuntimeState::ApplyInitialModifiers(
	AComposableCameraCameraBase* Camera,
	const ComposableCameraModifier::T_NodeModifier& EffectiveModifiers)
{
	ApplyInitialEffectiveModifiers(
		Camera, ExpandLegacyEffectiveModifiers(EffectiveModifiers));
}

void UComposableCameraModifierRuntimeState::ReconcileEffectiveModifiersFromAssets(
	AComposableCameraCameraBase* Camera,
	const ComposableCameraModifier::T_EffectiveModifier& EffectiveModifiers)
{
	ReconcileModifiersInternal(
		Camera, EffectiveModifiers, nullptr, true, false);
}

void UComposableCameraModifierRuntimeState::ReconcileModifiersFromAssets(
	AComposableCameraCameraBase* Camera,
	const ComposableCameraModifier::T_NodeModifier& EffectiveModifiers)
{
	ReconcileEffectiveModifiersFromAssets(
		Camera, ExpandLegacyEffectiveModifiers(EffectiveModifiers));
}

void UComposableCameraModifierRuntimeState::ReconcileModifiers(
	AComposableCameraCameraBase* Camera,
	const ComposableCameraModifier::T_NodeModifier& EffectiveModifiers,
	UComposableCameraModifierTransitionBase* TransitionOverride)
{
	ReconcileModifiersInternal(
		Camera,
		ExpandLegacyEffectiveModifiers(EffectiveModifiers),
		TransitionOverride,
		false,
		false);
}

void UComposableCameraModifierRuntimeState::ReconcileModifiersInternal(
	AComposableCameraCameraBase* Camera,
	const ComposableCameraModifier::T_EffectiveModifier& EffectiveModifiers,
	UComposableCameraModifierTransitionBase* TransitionOverride,
	bool bResolveTransitionsFromAssets,
	bool bImmediate)
{
	if (!Camera)
	{
		return;
	}

	for (UComposableCameraCameraNodeBase* Node : Camera->CameraNodes)
	{
		if (!Node)
		{
			continue;
		}

		const ComposableCameraModifier::T_PropertyModifier* DesiredModifiers =
			EffectiveModifiers.Find(Node->GetClass());
		const ComposableCameraModifier::FModifierEntry* WholeNodeEntry =
			DesiredModifiers
				? DesiredModifiers->Find(NAME_None)
				: nullptr;
		if (WholeNodeEntry
			&& WholeNodeEntry->Asset
			&& WholeNodeEntry->Asset->ApplyMode
				== EComposableCameraModifierApplyMode::ModifyExistingInstance)
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("In-place Modifier '%s' targets '%s' through a Custom Modifier. "
					"Custom side effects cannot be transitioned or restored; entry skipped."),
				*GetNameSafe(WholeNodeEntry->Asset.Get()),
				*GetNameSafe(Node->GetClass()));
		}

		FComposableCameraModifierNodeState* NodeState = FindNodeState(Node);
		if (!NodeState && !HasInPlacePropertyEntry(DesiredModifiers))
		{
			continue;
		}
		if (!NodeState)
		{
			FComposableCameraModifierNodeState& NewState = NodeStates.AddDefaulted_GetRef();
			NewState.Node = Node;
			NewState.BaselineSnapshot =
				DuplicateObject<UComposableCameraCameraNodeBase>(Node, this);
			NodeState = &NewState;
		}

		ReconcileNode(
			*NodeState,
			DesiredModifiers,
			TransitionOverride,
			bResolveTransitionsFromAssets,
			bImmediate);
	}

	PruneEmptyNodeStates();
}

FComposableCameraModifierNodeState*
UComposableCameraModifierRuntimeState::FindNodeState(
	UComposableCameraCameraNodeBase* Node)
{
	for (FComposableCameraModifierNodeState& NodeState : NodeStates)
	{
		if (NodeState.Node == Node)
		{
			return &NodeState;
		}
	}
	return nullptr;
}

void UComposableCameraModifierRuntimeState::ReconcileNode(
	FComposableCameraModifierNodeState& NodeState,
	const ComposableCameraModifier::T_PropertyModifier* DesiredModifiers,
	UComposableCameraModifierTransitionBase* TransitionOverride,
	bool bResolveTransitionsFromAssets,
	bool bImmediate)
{
	UComposableCameraCameraNodeBase* SourceSnapshot = nullptr;
	const auto GetSourceSnapshot = [&]() -> UComposableCameraCameraNodeBase*
	{
		if (!SourceSnapshot && !bImmediate && NodeState.Node)
		{
			SourceSnapshot =
				DuplicateObject<UComposableCameraCameraNodeBase>(
					NodeState.Node.Get(), this);
		}
		return SourceSnapshot;
	};

	for (FComposableCameraModifierPropertyState& PropertyState : NodeState.Properties)
	{
		UComposableCameraModifierBase* PreviousModifier =
			PropertyState.DesiredModifier.Get();
		UComposableCameraNodeModifierDataAsset* PreviousAsset =
			PropertyState.DesiredAsset.Get();
		const ComposableCameraModifier::FModifierEntry* DesiredEntry =
			FindInPlacePropertyEntry(
				DesiredModifiers, PropertyState.PropertyName);
		UComposableCameraModifierBase* DesiredModifier =
			DesiredEntry ? DesiredEntry->Modifier.Get() : nullptr;
		UComposableCameraNodeModifierDataAsset* DesiredAsset =
			DesiredEntry ? DesiredEntry->Asset.Get() : nullptr;
		if (PreviousModifier == DesiredModifier
			&& PreviousAsset == DesiredAsset)
		{
			continue;
		}

		if (DesiredModifier)
		{
			const EModifierPropertyTransitionKind TransitionKind =
				PreviousModifier
					? EModifierPropertyTransitionKind::Replace
					: EModifierPropertyTransitionKind::Enter;
			StartTransitionToModifier(
				PropertyState,
				GetSourceSnapshot(),
				DesiredModifier->NodeTemplate.Get(),
				ResolvePropertyTransition(
					TransitionKind,
					PreviousAsset,
					DesiredAsset,
					TransitionOverride,
					bResolveTransitionsFromAssets),
				false,
				bImmediate);
		}
		else if (PreviousModifier)
		{
			StartExitTransition(
				PropertyState,
				GetSourceSnapshot(),
				ResolvePropertyTransition(
					EModifierPropertyTransitionKind::Exit,
					PreviousAsset,
					DesiredAsset,
					TransitionOverride,
					bResolveTransitionsFromAssets),
				bImmediate);
		}
		// Neither previous nor desired owns this property: preserve an
		// already-running exit instead of restarting its clock.
		PropertyState.DesiredModifier = DesiredModifier;
		PropertyState.DesiredAsset = DesiredAsset;
	}

	if (DesiredModifiers)
	{
		for (const auto& DesiredProperty : *DesiredModifiers)
		{
			const FName PropertyName = DesiredProperty.Key;
			const ComposableCameraModifier::FModifierEntry* DesiredEntry =
				PropertyName.IsNone()
					? nullptr
					: FindInPlacePropertyEntry(
						DesiredModifiers, PropertyName);
			if (!DesiredEntry)
			{
				continue;
			}

			const bool bAlreadyBound = NodeState.Properties.ContainsByPredicate(
				[PropertyName](const FComposableCameraModifierPropertyState& PropertyState)
				{
					return PropertyState.PropertyName == PropertyName;
				});
			if (!bAlreadyBound)
			{
				FComposableCameraModifierPropertyState* NewProperty =
					AddPropertyBinding(
					NodeState,
					PropertyName,
					DesiredEntry->Modifier->NodeTemplate.Get(),
					ResolvePropertyTransition(
						EModifierPropertyTransitionKind::Enter,
						nullptr,
						DesiredEntry->Asset.Get(),
						TransitionOverride,
						bResolveTransitionsFromAssets),
					bImmediate);
				if (NewProperty)
				{
					NewProperty->DesiredModifier =
						DesiredEntry->Modifier.Get();
					NewProperty->DesiredAsset =
						DesiredEntry->Asset.Get();
				}
			}
		}
	}

	if (bImmediate)
	{
		for (FComposableCameraModifierPropertyState& PropertyState : NodeState.Properties)
		{
			ApplyProperty(NodeState, PropertyState);
		}

		// Normal runtime exits must survive until ApplyForNode releases their
		// lower layer and unregisters node ownership.
		for (int32 Index = NodeState.Properties.Num() - 1; Index >= 0; --Index)
		{
			if (NodeState.Properties[Index].bPendingRemoval)
			{
				NodeState.Properties.RemoveAtSwap(
					Index, 1, EAllowShrinking::No);
			}
		}
	}
}

FComposableCameraModifierPropertyState*
UComposableCameraModifierRuntimeState::AddPropertyBinding(
	FComposableCameraModifierNodeState& NodeState,
	FName PropertyName,
	UComposableCameraCameraNodeBase* TargetTemplate,
	UComposableCameraModifierTransitionBase* Transition,
	bool bImmediate)
{
	UComposableCameraCameraNodeBase* Node = NodeState.Node.Get();
	if (!Node || !TargetTemplate || TargetTemplate->GetClass() != Node->GetClass())
	{
		return nullptr;
	}

	FProperty* Property = FindFProperty<FProperty>(Node->GetClass(), PropertyName);
	if (!UComposableCameraModifierBase::IsNodePropertyOverridable(Property))
	{
		return nullptr;
	}

	FComposableCameraNodePinDeclaration InputPin;
	const bool bHasInputPin = Node->FindInputPinForModifierProperty(Property, InputPin);
	if ((!bHasInputPin && !Node->SupportsInPlaceModifierProperty(PropertyName))
		|| (bHasInputPin && InputPin.PinType == EComposableCameraPinType::Delegate))
	{
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("In-place Modifier skipped unsupported property '%s.%s'. "
				"Use an input pin or opt the node property into runtime mutation."),
			*GetNameSafe(Node->GetClass()), *PropertyName.ToString());
		return nullptr;
	}

	FComposableCameraModifierPropertyState& PropertyState =
		NodeState.Properties.AddDefaulted_GetRef();
	PropertyState.PropertyName = PropertyName;
	PropertyState.Property = Property;
	PropertyState.FieldOffset = Property->GetOffset_ForInternal();
	PropertyState.bHasInputPin = bHasInputPin;
	PropertyState.InputPin = InputPin;
	PropertyState.bContinuous =
		UComposableCameraModifierBase::IsNodePropertyContinuouslyBlendable(Property);

	Node->RegisterInPlaceModifierOverride(
		Property,
		bHasInputPin ? InputPin.PinName : NAME_None);
	StartTransitionToModifier(
		PropertyState,
		nullptr,
		TargetTemplate,
		Transition,
		true,
		bImmediate);
	return &PropertyState;
}

void UComposableCameraModifierRuntimeState::StartTransitionToModifier(
	FComposableCameraModifierPropertyState& PropertyState,
	UComposableCameraCameraNodeBase* SourceSnapshot,
	UComposableCameraCameraNodeBase* TargetTemplate,
	UComposableCameraModifierTransitionBase* Transition,
	bool bUseLiveLowerSource,
	bool bImmediate)
{
	PropertyState.SourceSnapshot = SourceSnapshot;
	PropertyState.TargetTemplate = TargetTemplate;
	PropertyState.Transition = bImmediate ? nullptr : Transition;
	PropertyState.ElapsedTime = 0.f;
	PropertyState.bSourceUsesLiveLower = bUseLiveLowerSource;
	PropertyState.bDiscreteTargetApplied = false;
	PropertyState.bPendingRemoval = false;
	PropertyState.Phase = SourceSnapshot
		? EComposableCameraModifierPropertyPhase::Replacing
		: EComposableCameraModifierPropertyPhase::Entering;

	if (bImmediate || !Transition || Transition->Duration <= 0.f)
	{
		PropertyState.Phase = EComposableCameraModifierPropertyPhase::Active;
		PropertyState.Transition = nullptr;
	}
}

void UComposableCameraModifierRuntimeState::StartExitTransition(
	FComposableCameraModifierPropertyState& PropertyState,
	UComposableCameraCameraNodeBase* SourceSnapshot,
	UComposableCameraModifierTransitionBase* Transition,
	bool bImmediate)
{
	PropertyState.SourceSnapshot = SourceSnapshot;
	PropertyState.TargetTemplate = nullptr;
	PropertyState.Transition = bImmediate ? nullptr : Transition;
	PropertyState.ElapsedTime = 0.f;
	PropertyState.bSourceUsesLiveLower = false;
	PropertyState.bDiscreteTargetApplied = false;
	PropertyState.bPendingRemoval = false;
	PropertyState.Phase = EComposableCameraModifierPropertyPhase::Exiting;

	if (bImmediate || !Transition || Transition->Duration <= 0.f)
	{
		PropertyState.bPendingRemoval = true;
	}
}

void UComposableCameraModifierRuntimeState::BeginCameraTick(float DeltaTime)
{
	const float SafeDeltaTime = FMath::Max(0.f, DeltaTime);
	for (FComposableCameraModifierNodeState& NodeState : NodeStates)
	{
		for (FComposableCameraModifierPropertyState& PropertyState : NodeState.Properties)
		{
			if (PropertyState.Phase != EComposableCameraModifierPropertyPhase::Active
				&& !PropertyState.bPendingRemoval)
			{
				PropertyState.ElapsedTime += SafeDeltaTime;
			}
		}
	}
}

void UComposableCameraModifierRuntimeState::ApplyForNode(
	UComposableCameraCameraNodeBase* Node)
{
	FComposableCameraModifierNodeState* NodeState = FindNodeState(Node);
	if (!NodeState)
	{
		return;
	}

	for (FComposableCameraModifierPropertyState& PropertyState : NodeState->Properties)
	{
		ApplyProperty(*NodeState, PropertyState);
	}

	for (int32 Index = NodeState->Properties.Num() - 1; Index >= 0; --Index)
	{
		if (NodeState->Properties[Index].bPendingRemoval)
		{
			NodeState->Properties.RemoveAtSwap(Index, 1, EAllowShrinking::No);
		}
	}
}

void UComposableCameraModifierRuntimeState::ApplyProperty(
	FComposableCameraModifierNodeState& NodeState,
	FComposableCameraModifierPropertyState& PropertyState)
{
	if (!NodeState.Node || !PropertyState.Property)
	{
		PropertyState.bPendingRemoval = true;
		return;
	}

	if (PropertyState.bPendingRemoval)
	{
		ReleaseProperty(NodeState, PropertyState);
		return;
	}

	const bool bActive =
		PropertyState.Phase == EComposableCameraModifierPropertyPhase::Active;
	if (bActive
		&& !PropertyState.bContinuous
		&& PropertyState.bDiscreteTargetApplied)
	{
		return;
	}
	const float Weight = bActive
		? 1.f
		: PropertyState.Transition
			? PropertyState.Transition->EvaluateWeight(PropertyState.ElapsedTime)
			: 1.f;
	const bool bTimeFinished = bActive
		|| !PropertyState.Transition
		|| PropertyState.ElapsedTime >= PropertyState.Transition->Duration;

	if (PropertyState.bContinuous)
	{
		ApplyContinuousValue(NodeState, PropertyState, Weight);
		if (!bSuppressNotifications)
		{
			NodeState.Node->NotifyModifierPropertyChanged(PropertyState.PropertyName);
		}

		if (bTimeFinished)
		{
			if (PropertyState.Phase == EComposableCameraModifierPropertyPhase::Exiting)
			{
				ReleaseProperty(NodeState, PropertyState);
			}
			else
			{
				PropertyState.Phase = EComposableCameraModifierPropertyPhase::Active;
				PropertyState.SourceSnapshot = nullptr;
				PropertyState.Transition = nullptr;
			}
		}
		return;
	}

	const float SwitchWeight = PropertyState.Transition
		? PropertyState.Transition->DiscreteSwitchWeight
		: 1.f;
	const bool bUseTarget = bActive || Weight >= SwitchWeight || bTimeFinished;
	if (PropertyState.Phase == EComposableCameraModifierPropertyPhase::Exiting)
	{
		if (bUseTarget)
		{
			ReleaseProperty(NodeState, PropertyState);
		}
		else
		{
			CopyPropertyValue(
				PropertyState.Property,
				NodeState.Node.Get(),
				PropertyState.SourceSnapshot.Get());
		}
		return;
	}

	if (bUseTarget)
	{
		CopyPropertyValue(
			PropertyState.Property,
			NodeState.Node.Get(),
			PropertyState.TargetTemplate.Get());
		if (!bSuppressNotifications)
		{
			NodeState.Node->NotifyModifierPropertyChanged(PropertyState.PropertyName);
		}
		PropertyState.Phase = EComposableCameraModifierPropertyPhase::Active;
		PropertyState.bDiscreteTargetApplied = true;
		PropertyState.SourceSnapshot = nullptr;
		PropertyState.Transition = nullptr;
	}
	else if (PropertyState.bSourceUsesLiveLower)
	{
		ApplyLowerOrBaseline(NodeState, PropertyState);
		if (!bSuppressNotifications)
		{
			NodeState.Node->NotifyModifierPropertyChanged(PropertyState.PropertyName);
		}
	}
	else
	{
		CopyPropertyValue(
			PropertyState.Property,
			NodeState.Node.Get(),
			PropertyState.SourceSnapshot.Get());
	}
}

void UComposableCameraModifierRuntimeState::ReleaseProperty(
	FComposableCameraModifierNodeState& NodeState,
	FComposableCameraModifierPropertyState& PropertyState)
{
	ApplyLowerOrBaseline(NodeState, PropertyState);
	if (NodeState.Node)
	{
		NodeState.Node->UnregisterInPlaceModifierOverride(PropertyState.FieldOffset);
		if (!bSuppressNotifications)
		{
			NodeState.Node->NotifyModifierPropertyChanged(PropertyState.PropertyName);
		}
	}
	PropertyState.bPendingRemoval = true;
}

void UComposableCameraModifierRuntimeState::EndCameraTick()
{
	PruneEmptyNodeStates();
}

void UComposableCameraModifierRuntimeState::PruneEmptyNodeStates()
{
	for (int32 Index = NodeStates.Num() - 1; Index >= 0; --Index)
	{
		FComposableCameraModifierNodeState& NodeState = NodeStates[Index];
		if (NodeState.Properties.IsEmpty())
		{
			NodeStates.RemoveAtSwap(Index, 1, EAllowShrinking::No);
		}
	}
}

bool UComposableCameraModifierRuntimeState::HasBindings() const
{
	for (const FComposableCameraModifierNodeState& NodeState : NodeStates)
	{
		if (!NodeState.Properties.IsEmpty())
		{
			return true;
		}
	}
	return false;
}

void UComposableCameraModifierRuntimeState::BuildDebugSnapshot(
	TArray<FComposableCameraModifierPropertyDebugSnapshot>& Out) const
{
	for (const FComposableCameraModifierNodeState& NodeState : NodeStates)
	{
		const UComposableCameraCameraNodeBase* Node = NodeState.Node.Get();
		if (!Node)
		{
			continue;
		}

		for (const FComposableCameraModifierPropertyState& PropertyState
			: NodeState.Properties)
		{
			if (!PropertyState.Property || PropertyState.bPendingRemoval)
			{
				continue;
			}

			FComposableCameraModifierPropertyDebugSnapshot& Item =
				Out.AddDefaulted_GetRef();
			Item.NodeClassPath = Node->GetClass()->GetPathName();
			Item.NodeClassName = FName::NameToDisplayString(
				Node->GetClass()->GetName(), false);
			Item.PropertyName = PropertyState.PropertyName.ToString();
			Item.OwnerName = PropertyState.DesiredAsset
				? PropertyState.DesiredAsset->GetName()
				: FString();
			Item.Phase = PropertyState.Phase;
			Item.Progress = PropertyState.Phase
				== EComposableCameraModifierPropertyPhase::Active
				? 1.f
				: PropertyState.Transition
					&& PropertyState.Transition->Duration > 0.f
						? FMath::Clamp(
							PropertyState.ElapsedTime
								/ PropertyState.Transition->Duration,
							0.f, 1.f)
						: 1.f;

			const FProperty* Property = PropertyState.Property;
			Property->ExportTextItem_Direct(
				Item.CurrentValue,
				Property->ContainerPtrToValuePtr<void>(Node),
				nullptr, nullptr, PPF_None);
			if (PropertyState.Phase
				== EComposableCameraModifierPropertyPhase::Exiting)
			{
				Item.TargetValue = PropertyState.bHasInputPin
					? TEXT("Live Lower") : TEXT("Baseline");
			}
			else if (PropertyState.TargetTemplate)
			{
				Property->ExportTextItem_Direct(
					Item.TargetValue,
					Property->ContainerPtrToValuePtr<void>(
						PropertyState.TargetTemplate.Get()),
					nullptr, nullptr, PPF_None);
			}
			else
			{
				Item.TargetValue = TEXT("(Unavailable)");
			}
		}
	}
}
