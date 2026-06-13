// Copyright 2026 Sulley. All Rights Reserved.

#include "Customizations/ComposableCameraShotSectionDetails.h"

#include "DetailLayoutBuilder.h"
#include "MovieScene/MovieSceneComposableCameraShotSection.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"

TSharedRef<IDetailCustomization> FComposableCameraShotSectionDetails::MakeInstance()
{
	return MakeShared<FComposableCameraShotSectionDetails>();
}

void FComposableCameraShotSectionDetails::Register(FPropertyEditorModule& PropertyEditorModule)
{
	PropertyEditorModule.RegisterCustomClassLayout(UMovieSceneComposableCameraShotSection::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FComposableCameraShotSectionDetails::MakeInstance));
}

void FComposableCameraShotSectionDetails::Unregister(FPropertyEditorModule& PropertyEditorModule)
{
	if (UObjectInitialized())
	{
		PropertyEditorModule.UnregisterCustomClassLayout(UMovieSceneComposableCameraShotSection::StaticClass()->GetFName());
	}
}

void FComposableCameraShotSectionDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	auto HideTopLevel = [&DetailBuilder](FName PropertyName)
	{
		const TSharedRef<IPropertyHandle> Handle = DetailBuilder.GetProperty(PropertyName, UMovieSceneComposableCameraShotSection::StaticClass());
		if (Handle->IsValidHandle())
		{
			Handle->MarkHiddenByCustomization();
		}
	};

	// `TargetActorOverrides` - same data the right-click "Bind Target Actors "
	// submenu writes. Hide the bare TArray editor; the menu is the canonical UX.
	HideTopLevel(GET_MEMBER_NAME_CHECKED(UMovieSceneComposableCameraShotSection, TargetActorOverrides));

	// `EnterTransition` - same data the right-click "Set Enter Transition "
	// submenu writes. Hide the soft-pointer slot; the menu is the canonical UX.
	HideTopLevel(GET_MEMBER_NAME_CHECKED(UMovieSceneComposableCameraShotSection, EnterTransition));
}
