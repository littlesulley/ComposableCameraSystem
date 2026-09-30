// Copyright 2026 Sulley. All Rights Reserved.

#include "Customizations/ComposableCameraMeshProfileCustomization.h"

#include "DataAssets/ComposableCameraMeshProfile.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "Widgets/Text/STextBlock.h"

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
	IDetailCategoryBuilder& CameraCategory = DetailBuilder.EditCategory(
		TEXT("Camera"),
		LOCTEXT("CameraCategory", "Camera"),
		ECategoryPriority::Variable);
	IDetailCategoryBuilder& ModifierCategory = DetailBuilder.EditCategory(
		TEXT("Modifier"),
		LOCTEXT("ModifierCategory", "Modifier"),
		ECategoryPriority::Transform);
	IDetailCategoryBuilder& ActionCategory = DetailBuilder.EditCategory(
		TEXT("Action"),
		LOCTEXT("ActionCategory", "Action"),
		ECategoryPriority::Important);
	IDetailCategoryBuilder& PatchCategory = DetailBuilder.EditCategory(
		TEXT("Patch"),
		LOCTEXT("PatchCategory", "Patch"),
		ECategoryPriority::TypeSpecific);

	const TSharedRef<IPropertyHandle> CameraProperty = DetailBuilder.GetProperty(
		GET_MEMBER_NAME_CHECKED(UComposableCameraMeshProfile, Camera));
	DetailBuilder.HideProperty(CameraProperty);

	const FName CameraChildNames[] =
	{
		GET_MEMBER_NAME_CHECKED(FComposableCameraParameterTableRow, CameraType),
		GET_MEMBER_NAME_CHECKED(FComposableCameraParameterTableRow, TransitionOverride),
		GET_MEMBER_NAME_CHECKED(FComposableCameraParameterTableRow, ActivationParams),
		GET_MEMBER_NAME_CHECKED(FComposableCameraParameterTableRow, Parameters)
	};
	for (const FName ChildName : CameraChildNames)
	{
		if (const TSharedPtr<IPropertyHandle> ChildProperty =
			CameraProperty->GetChildHandle(ChildName))
		{
			CameraCategory.AddProperty(ChildProperty.ToSharedRef());
		}
	}

	ModifierCategory.AddProperty(DetailBuilder.GetProperty(
		GET_MEMBER_NAME_CHECKED(UComposableCameraMeshProfile, ModifierAssets)));

	ActionCategory.AddCustomRow(LOCTEXT("ActionReservedFilter", "Reserved"))
	.WholeRowContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("ActionReserved", "Reserved for future Mesh Profile Action support."))
		.Font(IDetailLayoutBuilder::GetDetailFontItalic())
		.ColorAndOpacity(FSlateColor::UseSubduedForeground())
	];

	PatchCategory.AddCustomRow(LOCTEXT("PatchReservedFilter", "Reserved"))
	.WholeRowContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("PatchReserved", "Reserved for future Mesh Profile Patch support."))
		.Font(IDetailLayoutBuilder::GetDetailFontItalic())
		.ColorAndOpacity(FSlateColor::UseSubduedForeground())
	];
}

#undef LOCTEXT_NAMESPACE
