// Copyright Sulley. All rights reserved.

#include "Customizations/ComposableCameraShotAnchorIndexCustomization.h"

#include "Customizations/ComposableCameraShotTargetIndexCombo.h"
#include "DataAssets/ComposableCameraShot.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IPropertyTypeCustomization.h"
#include "Layout/Visibility.h"
#include "Misc/Attribute.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "ShotAnchorIndexCustomization"

TSharedRef<IPropertyTypeCustomization> FShotAnchorIndexCustomization::MakeInstance()
{
	return MakeShared<FShotAnchorIndexCustomization>();
}

void FShotAnchorIndexCustomization::Register(FPropertyEditorModule& PropertyEditorModule)
{
	// Same factory registered for both struct types — the customization
	// detects which is in play via `StructPropertyHandle->GetProperty()` at
	// render time. AnchorTargetWeight gets a no-gate visibility (always
	// shown), AnchorSpec gates on `Mode == SingleTarget`.
	PropertyEditorModule.RegisterCustomPropertyTypeLayout(
		FComposableCameraAnchorSpec::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(
			&FShotAnchorIndexCustomization::MakeInstance));
	PropertyEditorModule.RegisterCustomPropertyTypeLayout(
		FComposableCameraAnchorTargetWeight::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(
			&FShotAnchorIndexCustomization::MakeInstance));
}

void FShotAnchorIndexCustomization::Unregister(FPropertyEditorModule& PropertyEditorModule)
{
	if (UObjectInitialized())
	{
		PropertyEditorModule.UnregisterCustomPropertyTypeLayout(
			FComposableCameraAnchorSpec::StaticStruct()->GetFName());
		PropertyEditorModule.UnregisterCustomPropertyTypeLayout(
			FComposableCameraAnchorTargetWeight::StaticStruct()->GetFName());
	}
}

void FShotAnchorIndexCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> StructPropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& /*StructCustomizationUtils*/)
{
	StructHandle = StructPropertyHandle;

	// Default-shape header — engine renders the struct name + expand caret.
	HeaderRow
		.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget()
		];
}

void FShotAnchorIndexCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> StructPropertyHandle,
	IDetailChildrenBuilder& StructBuilder,
	IPropertyTypeCustomizationUtils& /*StructCustomizationUtils*/)
{
	// Identify which struct we're customizing — the visibility gate differs.
	// AnchorSpec gates on Mode == SingleTarget; AnchorTargetWeight is always
	// editable when its array entry exists. Use the property's struct type
	// to dispatch (rather than checking the field's owning struct via
	// reflection) — the property *is* the struct we customize.
	const FStructProperty* AsStructProp = CastField<FStructProperty>(StructPropertyHandle->GetProperty());
	const bool bIsAnchorSpec = AsStructProp
		&& AsStructProp->Struct == FComposableCameraAnchorSpec::StaticStruct();

	uint32 NumChildren = 0;
	StructPropertyHandle->GetNumChildren(NumChildren);

	for (uint32 i = 0; i < NumChildren; ++i)
	{
		TSharedPtr<IPropertyHandle> ChildHandle = StructPropertyHandle->GetChildHandle(i);
		if (!ChildHandle.IsValid() || !ChildHandle->GetProperty())
		{
			continue;
		}

		// Both struct types use the property name "TargetIndex" for the
		// integer slot — different `GET_MEMBER_NAME_CHECKED` macros would
		// resolve to the same FName, but checking via raw name avoids
		// duplicating the handler.
		const FName PropName = ChildHandle->GetProperty()->GetFName();
		const bool bIsTargetIndex = (PropName == TEXT("TargetIndex"));

		if (bIsTargetIndex)
		{
			TWeakPtr<IPropertyHandle> WeakStruct = StructHandle;
			TAttribute<EVisibility> VisAttr = bIsAnchorSpec
				? TAttribute<EVisibility>::CreateLambda([WeakStruct]() -> EVisibility
				{
					// AnchorSpec gates: only SingleTarget reads TargetIndex.
					// WeightedWorldCentroid uses WeightedTargets; FixedWorldPosition
					// uses WorldPosition. Hide the field outside SingleTarget so
					// the Anchor category isn't cluttered with unused rows.
					TSharedPtr<IPropertyHandle> Pin = WeakStruct.Pin();
					if (!Pin.IsValid())
					{
						return EVisibility::Collapsed;
					}
					TSharedPtr<IPropertyHandle> ModeHnd = Pin->GetChildHandle(
						GET_MEMBER_NAME_CHECKED(FComposableCameraAnchorSpec, Mode));
					if (!ModeHnd.IsValid())
					{
						return EVisibility::Collapsed;
					}
					uint8 ModeVal = 0;
					if (ModeHnd->GetValue(ModeVal) != FPropertyAccess::Success)
					{
						return EVisibility::Collapsed;
					}
					return ModeVal == static_cast<uint8>(EShotAnchorMode::SingleTarget)
						? EVisibility::Visible
						: EVisibility::Collapsed;
				})
				: TAttribute<EVisibility>(EVisibility::Visible);

			StructBuilder.AddCustomRow(ChildHandle->GetPropertyDisplayName())
				.Visibility(VisAttr)
				.NameContent()
				[
					ChildHandle->CreatePropertyNameWidget()
				]
				.ValueContent()
				.MinDesiredWidth(220.f)
				[
					FShotTargetIndexCombo::Build(ChildHandle.ToSharedRef())
				];
		}
		else
		{
			StructBuilder.AddProperty(ChildHandle.ToSharedRef());
		}
	}
}

#undef LOCTEXT_NAMESPACE
