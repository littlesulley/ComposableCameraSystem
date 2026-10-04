// Copyright 2026 Sulley. All Rights Reserved.

#include "Customizations/ComposableCameraMeshProfileCustomization.h"

#include "DataAssets/ComposableCameraMeshProfile.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "IDetailGroup.h"
#include "IPropertyUtilities.h"
#include "DetailWidgetRow.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"
#include <initializer_list>

#define LOCTEXT_NAMESPACE "ComposableCameraMeshProfileCustomization"

TSharedRef<IDetailCustomization> FComposableCameraMeshProfileCustomization::MakeInstance()
{
	return MakeShared<FComposableCameraMeshProfileCustomization>();
}

void FComposableCameraMeshProfileCustomization::Register(
	FPropertyEditorModule& PropertyEditorModule)
{
	PropertyEditorModule.RegisterCustomClassLayout(
		UComposableCameraMeshProfile::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(
			&FComposableCameraMeshProfileCustomization::MakeInstance));
}

void FComposableCameraMeshProfileCustomization::Unregister(
	FPropertyEditorModule& PropertyEditorModule)
{
	if (UObjectInitialized())
	{
		PropertyEditorModule.UnregisterCustomClassLayout(
			UComposableCameraMeshProfile::StaticClass()->GetFName());
	}
}

void FComposableCameraMeshProfileCustomization::CustomizeDetails(
	IDetailLayoutBuilder& DetailBuilder)
{
	const FName ConfigNames[] =
	{
		GET_MEMBER_NAME_CHECKED(UComposableCameraMeshProfile, Camera),
		GET_MEMBER_NAME_CHECKED(UComposableCameraMeshProfile, ModifierAssets),
		GET_MEMBER_NAME_CHECKED(UComposableCameraMeshProfile, Action),
		GET_MEMBER_NAME_CHECKED(UComposableCameraMeshProfile, Patch)
	};
	for (FName Name : ConfigNames)
	{
		const TSharedRef<IPropertyHandle> Config = DetailBuilder.GetProperty(Name);
		DetailBuilder.HideProperty(Config);
		// ShowOnlyInnerProperties puts struct children in the default layout independently
		// of their parent. Hide those rows too, then add only the selected family's rows.
		if (CastField<FStructProperty>(Config->GetProperty()))
		{
			uint32 ChildCount = 0;
			Config->GetNumChildren(ChildCount);
			for (uint32 Index = 0; Index < ChildCount; ++Index)
			{
				if (TSharedPtr<IPropertyHandle> Child = Config->GetChildHandle(Index))
				{
					DetailBuilder.HideProperty(Child);
				}
			}
		}
	}

	IDetailCategoryBuilder& ProfileCategory = DetailBuilder.EditCategory(TEXT("Profile"));
	ProfileCategory.SetSortOrder(0);
	const TSharedRef<IPropertyHandle> TypeHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UComposableCameraMeshProfile, Type));
	ProfileCategory.AddProperty(TypeHandle);
	const TWeakPtr<IPropertyUtilities> WeakUtilities = DetailBuilder.GetPropertyUtilities();
	TypeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([WeakUtilities]()
	{
		if (TSharedPtr<IPropertyUtilities> Utilities = WeakUtilities.Pin())
		{
			Utilities->RequestForceRefresh();
		}
	}));

	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	for (const TWeakObjectPtr<UObject>& Object : Objects)
	{
		if (const UComposableCameraMeshProfile* Profile = Cast<UComposableCameraMeshProfile>(Object.Get()); Profile && Profile->NeedsTypeSelection())
		{
			ProfileCategory.AddCustomRow(LOCTEXT("Migration", "Legacy mixed Profile"))
			.WholeRowContent()
			[
				SNew(SButton)
				.Text(LOCTEXT("ConfirmType", "Legacy Camera + Modifier data retained. Confirm selected Type to enable this Profile."))
				.OnClicked_Lambda([Objects, WeakUtilities]()
				{
					const FScopedTransaction Transaction(LOCTEXT("ConvertProfile", "Select Mesh Profile Type"));
					for (const TWeakObjectPtr<UObject>& Candidate : Objects)
					{
						if (UComposableCameraMeshProfile* Selected = Cast<UComposableCameraMeshProfile>(Candidate.Get()))
						{
							Selected->Modify();
							Selected->ConfirmTypeSelection();
							Selected->MarkPackageDirty();
						}
					}
					if (TSharedPtr<IPropertyUtilities> Utilities = WeakUtilities.Pin()) { Utilities->RequestForceRefresh(); }
					return FReply::Handled();
				})
			];
			break;
		}
	}

	uint8 TypeValue = 0;
	if (TypeHandle->GetValue(TypeValue) != FPropertyAccess::Success)
	{
		return; // Mixed multi-selection never edits an arbitrary first Profile's family.
	}
	auto AddChildren = [&DetailBuilder](IDetailCategoryBuilder& Category, FName ParentName, std::initializer_list<FName> Names)
	{
		const TSharedRef<IPropertyHandle> Parent = DetailBuilder.GetProperty(ParentName);
		for (FName Name : Names)
		{
			if (TSharedPtr<IPropertyHandle> Child = Parent->GetChildHandle(Name)) { Category.AddProperty(Child.ToSharedRef()); }
		}
	};
	switch (static_cast<EComposableCameraMeshProfileType>(TypeValue))
	{
	case EComposableCameraMeshProfileType::CameraType:
	{
		IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(TEXT("Camera"));
		Category.SetSortOrder(1);
		DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UComposableCameraMeshProfile, Camera))
			->GetChildHandle(GET_MEMBER_NAME_CHECKED(FComposableCameraParameterTableRow, CameraType))
			->SetInstanceMetaData(TEXT("DisallowedClasses"), TEXT("/Script/ComposableCameraSystem.ComposableCameraPatchTypeAsset"));
		AddChildren(Category, GET_MEMBER_NAME_CHECKED(UComposableCameraMeshProfile, Camera), {
			GET_MEMBER_NAME_CHECKED(FComposableCameraParameterTableRow, CameraType),
			GET_MEMBER_NAME_CHECKED(FComposableCameraParameterTableRow, Parameters)});
		const TSharedRef<IPropertyHandle> Camera = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UComposableCameraMeshProfile, Camera));
		Category.AddProperty(Camera->GetChildHandle(GET_MEMBER_NAME_CHECKED(FComposableCameraParameterTableRow, TransitionOverride)).ToSharedRef(), EPropertyLocation::Advanced);
		IDetailGroup& Activation = Category.AddGroup(TEXT("Activation"), LOCTEXT("Activation", "Activation"), true);
		const TSharedPtr<IPropertyHandle> Params = Camera->GetChildHandle(GET_MEMBER_NAME_CHECKED(FComposableCameraParameterTableRow, ActivationParams));
		for (FName Name : {GET_MEMBER_NAME_CHECKED(FComposableCameraActivateParams, bPreserveCameraPose),
			GET_MEMBER_NAME_CHECKED(FComposableCameraActivateParams, InitialTransform),
			GET_MEMBER_NAME_CHECKED(FComposableCameraActivateParams, bUseInitialTransformRotation),
			GET_MEMBER_NAME_CHECKED(FComposableCameraActivateParams, bFreezeSourceCamera)})
		{
			Activation.AddPropertyRow(Params->GetChildHandle(Name).ToSharedRef());
		}
		break;
	}
	case EComposableCameraMeshProfileType::Modifier:
	{
		IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(TEXT("Modifier"));
		Category.SetSortOrder(1);
		Category.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UComposableCameraMeshProfile, ModifierAssets)));
		break;
	}
	case EComposableCameraMeshProfileType::Action:
	{
		IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(TEXT("Action"));
		Category.SetSortOrder(1);
		AddChildren(Category, GET_MEMBER_NAME_CHECKED(UComposableCameraMeshProfile, Action), {
			GET_MEMBER_NAME_CHECKED(FComposableCameraMeshActionConfig, ActionAsset),
			GET_MEMBER_NAME_CHECKED(FComposableCameraMeshActionConfig, bOnlyForCurrentCamera),
			GET_MEMBER_NAME_CHECKED(FComposableCameraMeshActionConfig, Parameters)});
		break;
	}
	case EComposableCameraMeshProfileType::Patch:
	{
		IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(TEXT("Patch"));
		Category.SetSortOrder(1);
		AddChildren(Category, GET_MEMBER_NAME_CHECKED(UComposableCameraMeshProfile, Patch), {
			GET_MEMBER_NAME_CHECKED(FComposableCameraMeshPatchConfig, PatchAsset),
			GET_MEMBER_NAME_CHECKED(FComposableCameraMeshPatchConfig, ActivationParams),
			GET_MEMBER_NAME_CHECKED(FComposableCameraMeshPatchConfig, Parameters)});
		break;
	}
	default: break;
	}
}

#undef LOCTEXT_NAMESPACE
