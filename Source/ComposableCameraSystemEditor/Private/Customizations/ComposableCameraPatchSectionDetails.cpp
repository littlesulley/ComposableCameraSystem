// Copyright 2026 Sulley. All Rights Reserved.

#include "Customizations/ComposableCameraPatchSectionDetails.h"

#include "DetailLayoutBuilder.h"
#include "MovieScene/MovieSceneComposableCameraPatchSection.h"
#include "Patches/ComposableCameraPatchTypes.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"

TSharedRef<IDetailCustomization> FComposableCameraPatchSectionDetails::MakeInstance()
{
	return MakeShared<FComposableCameraPatchSectionDetails>();
}

void FComposableCameraPatchSectionDetails::Register(FPropertyEditorModule& PropertyEditorModule)
{
	PropertyEditorModule.RegisterCustomClassLayout(UMovieSceneComposableCameraPatchSection::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FComposableCameraPatchSectionDetails::MakeInstance));
}

void FComposableCameraPatchSectionDetails::Unregister(FPropertyEditorModule& PropertyEditorModule)
{
	if (UObjectInitialized())
	{
		PropertyEditorModule.UnregisterCustomClassLayout(UMovieSceneComposableCameraPatchSection::StaticClass()->GetFName());
	}
}

void FComposableCameraPatchSectionDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// Walk into the inlined Params struct - `meta=(ShowOnlyInnerProperties)` on
	// the Section's `Params` field flattens children into the outer "Patch"
	// category visually, but the reflection still has Params as a real FStructProperty
	// so we resolve children through it. MarkHiddenByCustomization on the child
	// handle removes the corresponding row from the auto-generated layout
	// without disturbing siblings.
	const TSharedRef<IPropertyHandle> ParamsHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMovieSceneComposableCameraPatchSection, Params),
		UMovieSceneComposableCameraPatchSection::StaticClass());
	if (!ParamsHandle->IsValidHandle())
	{
		return;
	}

	auto HideChild = [&ParamsHandle](FName ChildName)
	{
		if (const TSharedPtr<IPropertyHandle> Child = ParamsHandle->GetChildHandle(ChildName); Child.IsValid())
		{
			Child->MarkHiddenByCustomization();
		}
	};

	// ExpirationType pair (bOverride + value). The InlineEditConditionToggle
	// bool has no row of its own - it renders as the checkbox next to the
	// gated value - so hiding the value would normally hide the toggle too,
	// but we mark both for safety in case the InlineEditConditionToggle slot
	// emits anything in some UE 5.6 layout pass we haven't seen.
	HideChild(GET_MEMBER_NAME_CHECKED(FComposableCameraPatchActivateParams, bOverrideExpirationType));
	HideChild(GET_MEMBER_NAME_CHECKED(FComposableCameraPatchActivateParams, ExpirationType));

	// Duration pair (bOverride + value).
	HideChild(GET_MEMBER_NAME_CHECKED(FComposableCameraPatchActivateParams, bOverrideDuration));
	HideChild(GET_MEMBER_NAME_CHECKED(FComposableCameraPatchActivateParams, Duration));
}
