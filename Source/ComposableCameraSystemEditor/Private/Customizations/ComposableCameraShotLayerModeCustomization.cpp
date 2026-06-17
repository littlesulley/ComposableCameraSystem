// Copyright 2026 Sulley. All Rights Reserved.

#include "Customizations/ComposableCameraShotLayerModeCustomization.h"

#include "Customizations/ComposableCameraShotModeVisibility.h"
#include "DataAssets/ComposableCameraShot.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "Layout/Visibility.h"
#include "Misc/Attribute.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "ShotLayerModeCustomization"

namespace
{
enum class EShotLayerCustomizationKind : uint8
{
	Unknown,
	Aim,
	Lens,
	Focus
};

EShotLayerCustomizationKind ResolveKind(const TSharedRef<IPropertyHandle>& StructPropertyHandle)
{
	const FStructProperty* AsStructProp = CastField<FStructProperty>(StructPropertyHandle->GetProperty());
	if (!AsStructProp)
	{
		return EShotLayerCustomizationKind::Unknown;
	}

	if (AsStructProp->Struct == FShotAim::StaticStruct())
	{
		return EShotLayerCustomizationKind::Aim;
	}
	if (AsStructProp->Struct == FShotLens::StaticStruct())
	{
		return EShotLayerCustomizationKind::Lens;
	}
	if (AsStructProp->Struct == FShotFocus::StaticStruct())
	{
		return EShotLayerCustomizationKind::Focus;
	}
	return EShotLayerCustomizationKind::Unknown;
}

FName GetModePropertyName(EShotLayerCustomizationKind Kind)
{
	switch (Kind)
	{
	case EShotLayerCustomizationKind::Aim:
		return GET_MEMBER_NAME_CHECKED(FShotAim, Mode);
	case EShotLayerCustomizationKind::Lens:
		return GET_MEMBER_NAME_CHECKED(FShotLens, FOVMode);
	case EShotLayerCustomizationKind::Focus:
		return GET_MEMBER_NAME_CHECKED(FShotFocus, Mode);
	default:
		return NAME_None;
	}
}

bool IsFieldVisibleForMode(EShotLayerCustomizationKind Kind, uint8 ModeVal, FName PropertyName)
{
	using namespace ComposableCameraSystem::ShotDetailsVisibility;

	switch (Kind)
	{
	case EShotLayerCustomizationKind::Aim:
		return IsAimFieldVisible(static_cast<EShotAimMode>(ModeVal), PropertyName);
	case EShotLayerCustomizationKind::Lens:
		return IsLensFieldVisible(static_cast<EShotFOVMode>(ModeVal), PropertyName);
	case EShotLayerCustomizationKind::Focus:
		return IsFocusFieldVisible(static_cast<EShotFocusMode>(ModeVal), PropertyName);
	default:
		return true;
	}
}

TAttribute<EVisibility> MakeLayerVisibility(TWeakPtr<IPropertyHandle> WeakStruct,
	EShotLayerCustomizationKind Kind,
	FName PropertyName)
{
	return TAttribute<EVisibility>::CreateLambda([WeakStruct, Kind, PropertyName]() -> EVisibility
	{
		const FName ModePropertyName = GetModePropertyName(Kind);
		if (ModePropertyName.IsNone())
		{
			return EVisibility::Visible;
		}

		TSharedPtr<IPropertyHandle> Pin = WeakStruct.Pin();
		if (!Pin.IsValid())
		{
			return EVisibility::Collapsed;
		}

		TSharedPtr<IPropertyHandle> ModeHnd = Pin->GetChildHandle(ModePropertyName);
		if (!ModeHnd.IsValid())
		{
			return EVisibility::Collapsed;
		}

		uint8 ModeVal = 0;
		if (ModeHnd->GetValue(ModeVal) != FPropertyAccess::Success)
		{
			return EVisibility::Collapsed;
		}

		return IsFieldVisibleForMode(Kind, ModeVal, PropertyName)
			? EVisibility::Visible: EVisibility::Collapsed;
	});
}
} // namespace

TSharedRef<IPropertyTypeCustomization> FShotLayerModeCustomization::MakeInstance()
{
	return MakeShared<FShotLayerModeCustomization>();
}

void FShotLayerModeCustomization::Register(FPropertyEditorModule& PropertyEditorModule)
{
	PropertyEditorModule.RegisterCustomPropertyTypeLayout(FShotAim::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(
			&FShotLayerModeCustomization::MakeInstance));
	PropertyEditorModule.RegisterCustomPropertyTypeLayout(FShotLens::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(
			&FShotLayerModeCustomization::MakeInstance));
	PropertyEditorModule.RegisterCustomPropertyTypeLayout(FShotFocus::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(
			&FShotLayerModeCustomization::MakeInstance));
}

void FShotLayerModeCustomization::Unregister(FPropertyEditorModule& PropertyEditorModule)
{
	if (UObjectInitialized())
	{
		PropertyEditorModule.UnregisterCustomPropertyTypeLayout(FShotAim::StaticStruct()->GetFName());
		PropertyEditorModule.UnregisterCustomPropertyTypeLayout(FShotLens::StaticStruct()->GetFName());
		PropertyEditorModule.UnregisterCustomPropertyTypeLayout(FShotFocus::StaticStruct()->GetFName());
	}
}

void FShotLayerModeCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& /*StructCustomizationUtils*/)
{
	StructHandle = StructPropertyHandle;

	HeaderRow.NameContent()
		[StructPropertyHandle->CreatePropertyNameWidget()];
}

void FShotLayerModeCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle,
	IDetailChildrenBuilder& StructBuilder,
	IPropertyTypeCustomizationUtils& /*StructCustomizationUtils*/)
{
	const EShotLayerCustomizationKind Kind = ResolveKind(StructPropertyHandle);
	uint32 NumChildren = 0;
	StructPropertyHandle->GetNumChildren(NumChildren);

	for (uint32 i = 0; i < NumChildren; ++i)
	{
		TSharedPtr<IPropertyHandle> ChildHandle = StructPropertyHandle->GetChildHandle(i);
		if (!ChildHandle.IsValid() || !ChildHandle->GetProperty())
		{
			continue;
		}

		const FName PropName = ChildHandle->GetProperty()->GetFName();
		IDetailPropertyRow& Row = StructBuilder.AddProperty(ChildHandle.ToSharedRef());
		Row.Visibility(MakeLayerVisibility(StructHandle, Kind, PropName));
	}
}

#undef LOCTEXT_NAMESPACE
