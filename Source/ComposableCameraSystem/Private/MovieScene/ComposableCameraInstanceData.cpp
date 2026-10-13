// Copyright 2026 Sulley. All Rights Reserved.
#include "MovieScene/ComposableCameraInstanceData.h"

#include "DataAssets/ComposableCameraShotAsset.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "GameFramework/Actor.h"
#include "MovieScene/MovieSceneComposableCameraInstanceTrack.h"
#include "Nodes/ComposableCameraCompositionFramingNode.h"
#include "UObject/UnrealType.h"

namespace UE::ComposableCameras::InstanceData
{
static void Gather(UStruct* Struct, void* Memory, const FString& Prefix, TArray<FComposableCameraInstanceField>& Fields)
{
	if (!Struct || !Memory) return;
	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		FProperty* Property = *It;
		if (!Property->HasAnyPropertyFlags(CPF_Edit) || Property->HasAnyPropertyFlags(CPF_Transient)) continue;
		void* Value = Property->ContainerPtrToValuePtr<void>(Memory);
		const FString Path = Prefix + TEXT(".") + Property->GetName();
		if (FStructProperty* Nested = CastField<FStructProperty>(Property))
		{
			Gather(Nested->Struct, Value, Path, Fields);
			continue;
		}
		EComposableCameraInstanceFieldType Type;
		if (CastField<FBoolProperty>(Property)) Type = EComposableCameraInstanceFieldType::Boolean;
		else if (CastField<FEnumProperty>(Property)) Type = EComposableCameraInstanceFieldType::Integer;
		else if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
		{
			// Integer channels are int32; do not silently narrow arbitrary 64-bit inputs.
			if (Numeric->IsInteger() && Property->GetElementSize() > sizeof(int32)) continue;
			Type = Numeric->IsFloatingPoint() ? EComposableCameraInstanceFieldType::Number : EComposableCameraInstanceFieldType::Integer;
		}
		else if (CastField<FObjectPropertyBase>(Property)) Type = EComposableCameraInstanceFieldType::Object;
		else continue;
		Fields.Add({ FName(*Path), Property, Value, Type });
	}
}
}

double FComposableCameraInstanceField::ReadNumber() const
{
	if (const FBoolProperty* Boolean = CastField<FBoolProperty>(Property)) return Boolean->GetPropertyValue(Value) ? 1.0 : 0.0;
	if (const FEnumProperty* Enum = CastField<FEnumProperty>(Property)) return static_cast<double>(Enum->GetUnderlyingProperty()->GetSignedIntPropertyValue(Value));
	if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
		return Numeric->IsFloatingPoint() ? Numeric->GetFloatingPointPropertyValue(Value) : static_cast<double>(Numeric->GetSignedIntPropertyValue(Value));
	return 0.0;
}

void FComposableCameraInstanceField::WriteNumber(double Number) const
{
	if (!FMath::IsFinite(Number)) return;
	if (FBoolProperty* Boolean = CastField<FBoolProperty>(Property)) Boolean->SetPropertyValue(Value, Number != 0.0);
	else if (FEnumProperty* Enum = CastField<FEnumProperty>(Property)) Enum->GetUnderlyingProperty()->SetIntPropertyValue(Value, static_cast<int64>(Number));
	else if (FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
	{
		if (Numeric->IsFloatingPoint()) Numeric->SetFloatingPointPropertyValue(Value, Number);
		else Numeric->SetIntPropertyValue(Value, static_cast<int64>(Number));
	}
}

void FComposableCameraInstanceField::WriteObject(UObject* Object) const
{
	if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		ObjectProperty->SetObjectPropertyValue(Value, Object && Object->IsA(ObjectProperty->PropertyClass) ? Object : nullptr);
}

void UComposableCameraInstanceData::InitializeFromType(UComposableCameraTypeAsset* Type)
{
	bComposition = false;
	SourcePreset = nullptr;
	BuiltInType = nullptr;
	Subjects.Reset();
	Camera = FComposableCameraTypeAssetReference();
	Camera.TypeAsset = Type;
	Camera.RebuildBagsFromTypeAsset();
	if (!Type) return;
	for (const FComposableCameraExposedParameter& Parameter : Type->ExposedParameters)
		Camera.Parameters.SetValueSerializedString(Parameter.ParameterName, Type->GetExposedParameterDefaultValue(Parameter));
	for (const FComposableCameraInternalVariable& Variable : Type->ExposedVariables)
		Camera.Variables.SetValueSerializedString(Variable.VariableName, Variable.InitialValueString);
	RefreshSubjectSlots();
}

void UComposableCameraInstanceData::InitializeFromShot(const FComposableCameraShot& InShot, UComposableCameraShotAsset* Preset)
{
	BuiltInType = NewObject<UComposableCameraTypeAsset>(this, NAME_None, RF_Transactional);
	UComposableCameraCompositionFramingNode* Node = NewObject<UComposableCameraCompositionFramingNode>(BuiltInType, NAME_None, RF_Transactional);
	BuiltInType->NodeTemplates.Add(Node);
	BuiltInType->NodePinOverrides.AddDefaulted();
	FComposableCameraExecEntry Entry;
	Entry.EntryType = EComposableCameraExecEntryType::CameraNode;
	Entry.CameraNodeIndex = 0;
	BuiltInType->FullExecChain.Add(Entry);
	BuiltInType->ExecutionOrder.Add(0);
	Camera = FComposableCameraTypeAssetReference();
	Camera.TypeAsset = BuiltInType;
	Camera.RebuildBagsFromTypeAsset();
	bComposition = true;
	SourcePreset = Preset;
	Shot = InShot;
	Subjects.Reset();
	EnsureSubjectIdentities();
	// Sequence actor identity is supplied exclusively by Subjects, never by a preset's world paths.
	for (FComposableCameraShotTarget& Target : Shot.Targets) Target.Target.Actor.Reset();
	RefreshSubjectSlots();
}

void UComposableCameraInstanceData::CopyConfiguration(const UComposableCameraInstanceData& Source)
{
	Camera = Source.Camera;
	bComposition = Source.bComposition;
	Shot = Source.Shot;
	Subjects = Source.Subjects;
	SourcePreset = Source.SourcePreset;
}

void UComposableCameraInstanceData::CopyForAuthoring(const UComposableCameraInstanceData& Source)
{
	CopyConfiguration(Source);
	BuiltInType = Source.BuiltInType ? DuplicateObject<UComposableCameraTypeAsset>(Source.BuiltInType, this) : nullptr;
	if (BuiltInType) Camera.TypeAsset = BuiltInType;
	EnsureSubjectIdentities();
	RefreshSubjectSlots();
}

void UComposableCameraInstanceData::RefreshSubjectSlots()
{
	TArray<FComposableCameraInstanceField> Fields;
	GatherFields(Fields);
	for (const auto& Field : Fields)
	{
		const auto* Property = CastField<FObjectPropertyBase>(Field.Property);
		if (!Property || !Property->PropertyClass->IsChildOf(AActor::StaticClass())) continue;
		if (!Subjects.ContainsByPredicate([&](const auto& Slot) { return Slot.Input == Field.Path; }))
			Subjects.AddDefaulted_GetRef().Input = Field.Path;
	}
	// Removed fields retain their binding, just like orphaned animation channels. Undo or
	// restoring a graph input can recover the binding without silently assigning it elsewhere.
}

void UComposableCameraInstanceData::EnsureSubjectIdentities()
{
	TSet<FGuid> Seen;
	for (FComposableCameraShotTarget& Target : Shot.Targets)
	{
		if (!Target.SubjectId.IsValid() || Seen.Contains(Target.SubjectId)) Target.SubjectId = FGuid::NewGuid();
		Seen.Add(Target.SubjectId);
	}
}

void UComposableCameraInstanceData::GatherFields(TArray<FComposableCameraInstanceField>& OutFields)
{
	using namespace UE::ComposableCameras::InstanceData;
	OutFields.Reset();
	Gather(const_cast<UPropertyBag*>(Camera.Parameters.GetPropertyBagStruct()), Camera.Parameters.GetMutableValue().GetMemory(), TEXT("Parameters"), OutFields);
	Gather(const_cast<UPropertyBag*>(Camera.Variables.GetPropertyBagStruct()), Camera.Variables.GetMutableValue().GetMemory(), TEXT("Variables"), OutFields);
	if (!bComposition) return;
	Gather(FComposableCameraShot::StaticStruct(), &Shot, TEXT("Shot"), OutFields);
	for (FComposableCameraShotTarget& Target : Shot.Targets)
	{
		if (!Target.SubjectId.IsValid()) continue;
		Gather(FComposableCameraShotTarget::StaticStruct(), &Target,
			TEXT("Shot.Targets.") + Target.SubjectId.ToString(EGuidFormats::Digits), OutFields);
	}
}

void UComposableCameraInstanceData::PostLoad()
{
	Super::PostLoad();
	EnsureSubjectIdentities();
	Camera.RebuildBagsFromTypeAsset();
	RefreshSubjectSlots();
}

#if WITH_EDITOR
void UComposableCameraInstanceData::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	Super::PostEditChangeProperty(Event);
	EnsureSubjectIdentities();
	if (Event.GetPropertyName() == GET_MEMBER_NAME_CHECKED(FComposableCameraTypeAssetReference, TypeAsset))
		Camera.RebuildBagsFromTypeAsset();
	RefreshSubjectSlots();
	if (UMovieSceneComposableCameraInstanceTrack* Track = GetTypedOuter<UMovieSceneComposableCameraInstanceTrack>()) Track->MarkAsChanged();
}
#endif
