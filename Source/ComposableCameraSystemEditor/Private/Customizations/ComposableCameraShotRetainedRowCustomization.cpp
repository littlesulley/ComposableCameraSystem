// Copyright 2026 Sulley. All Rights Reserved.
#include "Customizations/ComposableCameraShotRetainedRowCustomization.h"
#include "Customizations/ComposableCameraShotModeVisibility.h"
#include "Customizations/ComposableCameraShotTargetIndexCombo.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IPropertyRowGenerator.h"
#include "PropertyHandle.h"
#include "UObject/UnrealType.h"

namespace
{
TAttribute<EVisibility> DirectVisibility(const TSharedPtr<IPropertyHandle>& Handle)
{
	using namespace ComposableCameraSystem::ShotDetailsVisibility;
	const FProperty* Property = Handle ? Handle->GetProperty() : nullptr;
	if (!Property) return EVisibility::Visible;
	const UStruct* Owner = Property->GetOwnerStruct();
	if (Owner != FShotPlacement::StaticStruct() && Owner != FShotAim::StaticStruct()
		&& Owner != FShotLens::StaticStruct() && Owner != FShotFocus::StaticStruct()
		&& Owner != FComposableCameraAnchorSpec::StaticStruct()) return EVisibility::Visible;
	const auto Parent = Handle->GetParentHandle();
	const FStructProperty* ParentStruct = Parent ? CastField<FStructProperty>(Parent->GetProperty()) : nullptr;
	// An array element can report the array field's owning struct; it is not a direct member of that struct.
	if (!ParentStruct || ParentStruct->Struct != Owner) return EVisibility::Visible;
	const auto Mode = Parent ? Parent->GetChildHandle(Owner == FShotLens::StaticStruct()
		? GET_MEMBER_NAME_CHECKED(FShotLens, FOVMode) : FName(TEXT("Mode"))) : nullptr;
	const auto Basis = Owner == FShotPlacement::StaticStruct() && Parent
		? Parent->GetChildHandle(GET_MEMBER_NAME_CHECKED(FShotPlacement, BasisFrame)) : nullptr;
	const FName Name = Property->GetFName();
	return TAttribute<EVisibility>::CreateLambda([Owner, Name, Mode, Basis]()
	{
		uint8 Value = 0, BasisValue = 0;
		if (!Mode || Mode->GetValue(Value) != FPropertyAccess::Success) return EVisibility::Collapsed;
		bool bVisible = true;
		if (Owner == FShotPlacement::StaticStruct())
		{
			if (!Basis || Basis->GetValue(BasisValue) != FPropertyAccess::Success) return EVisibility::Collapsed;
			bVisible = IsPlacementFieldVisible(static_cast<EShotPlacementMode>(Value), static_cast<EShotPlacementBasisFrame>(BasisValue), Name);
		}
		else if (Owner == FShotAim::StaticStruct()) bVisible = IsAimFieldVisible(static_cast<EShotAimMode>(Value), Name);
		else if (Owner == FShotLens::StaticStruct()) bVisible = IsLensFieldVisible(static_cast<EShotFOVMode>(Value), Name);
		else if (Owner == FShotFocus::StaticStruct()) bVisible = IsFocusFieldVisible(static_cast<EShotFocusMode>(Value), Name);
		else bVisible = IsAnchorFieldVisible(static_cast<EShotAnchorMode>(Value), Name);
		return bVisible ? EVisibility::Visible : EVisibility::Collapsed;
	});
}
}

TSharedRef<IPropertyTypeCustomization> FShotEditorRetainedRowCustomization::MakeInstance()
{
	return MakeShared<FShotEditorRetainedRowCustomization>();
}

void FShotEditorRetainedRowCustomization::Register(IPropertyRowGenerator& Generator)
{
	for (const UScriptStruct* Struct : { FShotPlacement::StaticStruct(), FShotAim::StaticStruct(), FShotLens::StaticStruct(),
		FShotFocus::StaticStruct(), FComposableCameraAnchorSpec::StaticStruct() })
		Generator.RegisterInstancedCustomPropertyTypeLayout(Struct->GetFName(),
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FShotEditorRetainedRowCustomization::MakeInstance));
}

void FShotEditorRetainedRowCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> Handle,
	FDetailWidgetRow& Header, IPropertyTypeCustomizationUtils&)
{
	Header.NameContent()[Handle->CreatePropertyNameWidget()];
}

void FShotEditorRetainedRowCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> Handle,
	IDetailChildrenBuilder& Builder, IPropertyTypeCustomizationUtils&)
{
	uint32 Count = 0;
	Handle->GetNumChildren(Count);
	for (uint32 Index = 0; Index < Count; ++Index)
	{
		const auto Child = Handle->GetChildHandle(Index);
		if (!Child || !Child->GetProperty()) continue;
		const FName Name = Child->GetProperty()->GetFName();
		if (Name == GET_MEMBER_NAME_CHECKED(FShotPlacement, BasisActorIndex)
			|| Name == GET_MEMBER_NAME_CHECKED(FShotPlacement, BasisSecondaryTargetIndex)
			|| Name == GET_MEMBER_NAME_CHECKED(FComposableCameraAnchorSpec, TargetIndex))
			Builder.AddCustomRow(Child->GetPropertyDisplayName()).PropertyHandleList({ Child })
				.NameContent()[Child->CreatePropertyNameWidget()]
				.ValueContent().MinDesiredWidth(220.f)[FShotTargetIndexCombo::Build(Child.ToSharedRef())];
		else Builder.AddProperty(Child.ToSharedRef());
		// No tree Visibility attributes: Slate wrappers own visibility. Native metadata still owns enabled state/ranges.
	}
}

TAttribute<EVisibility> FShotEditorRetainedRowCustomization::MakeVisibility(const TSharedPtr<IPropertyHandle>& Handle)
{
	TArray<TAttribute<EVisibility>> Conditions;
	for (auto Current = Handle; Current; Current = Current->GetParentHandle())
	{
		const auto Condition = DirectVisibility(Current);
		if (Condition.IsBound()) Conditions.Add(Condition);
	}
	return TAttribute<EVisibility>::CreateLambda([Conditions = MoveTemp(Conditions)]()
	{
		for (const auto& Condition : Conditions)
			if (Condition.Get() == EVisibility::Collapsed) return EVisibility::Collapsed;
		return EVisibility::Visible;
	});
}
