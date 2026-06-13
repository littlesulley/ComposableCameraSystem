// Copyright 2026 Sulley. All Rights Reserved.

#include "Customizations/ComposableCameraShotPlacementCustomization.h"

#include "Customizations/ComposableCameraShotModeVisibility.h"
#include "Customizations/ComposableCameraShotTargetIndexCombo.h"
#include "DataAssets/ComposableCameraShot.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "IPropertyTypeCustomization.h"
#include "Layout/Visibility.h"
#include "Misc/Attribute.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"

#define LOCTEXT_NAMESPACE "ShotPlacementCustomization"

TSharedRef<IPropertyTypeCustomization> FShotPlacementCustomization::MakeInstance()
{
	return MakeShared<FShotPlacementCustomization>();
}

void FShotPlacementCustomization::Register(FPropertyEditorModule& PropertyEditorModule)
{
	PropertyEditorModule.RegisterCustomPropertyTypeLayout(FShotPlacement::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(
			&FShotPlacementCustomization::MakeInstance));
}

void FShotPlacementCustomization::Unregister(FPropertyEditorModule& PropertyEditorModule)
{
	if (UObjectInitialized())
	{
		PropertyEditorModule.UnregisterCustomPropertyTypeLayout(FShotPlacement::StaticStruct()->GetFName());
	}
}

void FShotPlacementCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& /*StructCustomizationUtils*/)
{
	StructHandle = StructPropertyHandle;

	// Default-shape header - just the struct name, leave the engine's expand
	// caret untouched.
	HeaderRow.NameContent()
		[StructPropertyHandle->CreatePropertyNameWidget()];
}

void FShotPlacementCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle,
	IDetailChildrenBuilder& StructBuilder,
	IPropertyTypeCustomizationUtils& /*StructCustomizationUtils*/)
{
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
		if (PropName == GET_MEMBER_NAME_CHECKED(FShotPlacement, BasisActorIndex))
		{
			// `AddCustomRow` does NOT auto-evaluate the field's UPROPERTY
			// `EditCondition` meta - `AddProperty` does, but a custom row
			// has to wire it up manually. Mirror the field's authored
			// condition (`Mode == AnchorOrbit && BasisFrame == InheritFromActor`)
			// via a Visibility attribute so the row drops out of the panel
			// when the field is irrelevant. Visibility (rather than
			// `.IsEnabled`) matches the user-facing "this field doesn't
			// apply right now" intent - a greyed-out row would clutter the
			// Placement category in modes where BasisActorIndex has no
			// effect at all.
			TWeakPtr<IPropertyHandle> WeakStruct = StructHandle;
			TAttribute<EVisibility> VisAttr =
				TAttribute<EVisibility>::CreateLambda([WeakStruct, PropName]() -> EVisibility
				{
					TSharedPtr<IPropertyHandle> Pin = WeakStruct.Pin();
					if (!Pin.IsValid())
					{
						return EVisibility::Collapsed;
					}
					TSharedPtr<IPropertyHandle> ModeHnd = Pin->GetChildHandle(GET_MEMBER_NAME_CHECKED(FShotPlacement, Mode));
					TSharedPtr<IPropertyHandle> BasisHnd = Pin->GetChildHandle(GET_MEMBER_NAME_CHECKED(FShotPlacement, BasisFrame));
					if (!ModeHnd.IsValid() || !BasisHnd.IsValid())
					{
						return EVisibility::Collapsed;
					}
					uint8 ModeVal = 0;
					uint8 BasisVal = 0;
					if (ModeHnd->GetValue(ModeVal) != FPropertyAccess::Success
						|| BasisHnd->GetValue(BasisVal) != FPropertyAccess::Success)
					{
						return EVisibility::Collapsed;
					}
					return ComposableCameraSystem::ShotDetailsVisibility::IsPlacementFieldVisible(
							static_cast<EShotPlacementMode>(ModeVal),
							static_cast<EShotPlacementBasisFrame>(BasisVal),
							PropName)
						? EVisibility::Visible: EVisibility::Collapsed;
				});

			StructBuilder.AddCustomRow(ChildHandle->GetPropertyDisplayName())
				.Visibility(VisAttr)
				.NameContent()
				[ChildHandle->CreatePropertyNameWidget()]
				.ValueContent()
				.MinDesiredWidth(220.f)
				[FShotTargetIndexCombo::Build(ChildHandle.ToSharedRef())];
		}
		else
		{
			IDetailPropertyRow& Row = StructBuilder.AddProperty(ChildHandle.ToSharedRef());
			TWeakPtr<IPropertyHandle> WeakStruct = StructHandle;
			Row.Visibility(TAttribute<EVisibility>::CreateLambda([WeakStruct, PropName]() -> EVisibility
			{
				TSharedPtr<IPropertyHandle> Pin = WeakStruct.Pin();
				if (!Pin.IsValid())
				{
					return EVisibility::Collapsed;
				}
				TSharedPtr<IPropertyHandle> ModeHnd =
					Pin->GetChildHandle(GET_MEMBER_NAME_CHECKED(FShotPlacement, Mode));
				TSharedPtr<IPropertyHandle> BasisHnd =
					Pin->GetChildHandle(GET_MEMBER_NAME_CHECKED(FShotPlacement, BasisFrame));
				if (!ModeHnd.IsValid() || !BasisHnd.IsValid())
				{
					return EVisibility::Collapsed;
				}
				uint8 ModeVal = 0;
				uint8 BasisVal = 0;
				if (ModeHnd->GetValue(ModeVal) != FPropertyAccess::Success
					|| BasisHnd->GetValue(BasisVal) != FPropertyAccess::Success)
				{
					return EVisibility::Collapsed;
				}
				return ComposableCameraSystem::ShotDetailsVisibility::IsPlacementFieldVisible(
						static_cast<EShotPlacementMode>(ModeVal),
						static_cast<EShotPlacementBasisFrame>(BasisVal),
						PropName)
					? EVisibility::Visible: EVisibility::Collapsed;
			}));
		}
	}
}

#undef LOCTEXT_NAMESPACE
