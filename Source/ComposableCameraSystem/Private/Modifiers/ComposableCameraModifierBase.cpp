// Copyright 2026 Sulley. All Rights Reserved.

#include "Modifiers/ComposableCameraModifierBase.h"

#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

TSubclassOf<UComposableCameraCameraNodeBase> UComposableCameraModifierBase::GetTargetNodeClass() const
{
	if (bUseCustomModifierClass && CustomModifier && CustomModifier != this)
	{
		return CustomModifier->NodeClass;
	}

	if (NodeTemplate)
	{
		return TSubclassOf<UComposableCameraCameraNodeBase>(NodeTemplate->GetClass());
	}
	return NodeClass;
}

bool UComposableCameraModifierBase::UsesNodeTemplateOverride() const
{
	if (bUseCustomModifierClass)
	{
		return false;
	}
	return NodeTemplate != nullptr;
}

bool UComposableCameraModifierBase::IsNodePropertyOverridable(const FProperty* Property)
{
	if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit))
	{
		return false;
	}

	// Modifier templates are node instances, so class-authoring metadata such
	// as PaletteCategory (EditDefaultsOnly) must never appear as a runtime
	// override. Read-only, runtime-only, and explicitly opted-out fields are
	// excluded too.
	return !Property->HasAnyPropertyFlags(
		CPF_DisableEditOnInstance | CPF_EditConst | CPF_Transient | CPF_Deprecated)
		&& !Property->HasMetaData(TEXT("NoModifierOverride"));
}

bool UComposableCameraModifierBase::IsNodePropertyContinuouslyBlendable(const FProperty* Property)
{
	if (!Property)
	{
		return false;
	}

	if (Property->IsA<FFloatProperty>() || Property->IsA<FDoubleProperty>())
	{
		return true;
	}

	const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
	if (!StructProperty)
	{
		return false;
	}

	const UScriptStruct* Struct = StructProperty->Struct;
	return Struct == TBaseStructure<FVector2D>::Get()
		|| Struct == TBaseStructure<FVector>::Get()
		|| Struct == TBaseStructure<FVector4>::Get()
		|| Struct == TBaseStructure<FRotator>::Get()
		|| Struct == TBaseStructure<FTransform>::Get()
		|| Struct == TBaseStructure<FLinearColor>::Get();
}

void UComposableCameraModifierBase::ApplyModifierToNode(UComposableCameraCameraNodeBase* Node)
{
	if (!Node)
	{
		return;
	}

	if (bUseCustomModifierClass)
	{
		if (CustomModifier && CustomModifier != this)
		{
			Node->ApplyCustomModifierWithPinOwnership(
				[this, Node]() { CustomModifier->ApplyModifier(Node); });
		}
		return;
	}

	if (!NodeTemplate)
	{
		// Preserve existing user-authored Modifier Blueprint behavior.
		Node->ApplyCustomModifierWithPinOwnership(
			[this, Node]() { ApplyModifier(Node); });
		return;
	}

	if (NodeTemplate->GetClass() != Node->GetClass())
	{
		return;
	}

	for (const FName PropertyName : OverrideProperties)
	{
		ApplyModifierPropertyToNode(Node, PropertyName);
	}
}

bool UComposableCameraModifierBase::ApplyModifierPropertyToNode(
	UComposableCameraCameraNodeBase* Node,
	FName PropertyName)
{
	if (!Node
		|| !UsesNodeTemplateOverride()
		|| NodeTemplate->GetClass() != Node->GetClass()
		|| !OverrideProperties.Contains(PropertyName))
	{
		return false;
	}

	FProperty* Property = FindFProperty<FProperty>(Node->GetClass(), PropertyName);
	if (!IsNodePropertyOverridable(Property))
	{
		return false;
	}

	const int32 FieldOffset = Property->GetOffset_ForInternal();
	if (Node->HasModifierOverrideFieldOffset(FieldOffset))
	{
		return false;
	}

	if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property);
		ObjectProperty && Property->HasAnyPropertyFlags(CPF_InstancedReference))
	{
		// Instanced subobjects cannot be shallow-copied: the runtime node must
		// own its own duplicate rather than reference an asset subobject.
		UObject* SourceObject =
			ObjectProperty->GetObjectPropertyValue_InContainer(NodeTemplate);
		UObject* DuplicatedObject = SourceObject
			? StaticDuplicateObject(SourceObject, Node)
			: nullptr;
		ObjectProperty->SetObjectPropertyValue_InContainer(Node, DuplicatedObject);
	}
	else
	{
		Property->CopyCompleteValue_InContainer(Node, NodeTemplate);
	}

	Node->RegisterModifierOverrideFieldOffset(FieldOffset);
	return true;
}
