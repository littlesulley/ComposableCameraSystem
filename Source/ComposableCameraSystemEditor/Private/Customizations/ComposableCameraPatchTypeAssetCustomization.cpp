// Copyright Sulley. All rights reserved.

#include "Customizations/ComposableCameraPatchTypeAssetCustomization.h"

#include "DataAssets/ComposableCameraPatchTypeAsset.h"
#include "DataAssets/ComposableCameraTypeAsset.h"

#include "DetailLayoutBuilder.h"
#include "PropertyEditorModule.h"

TSharedRef<IDetailCustomization> FComposableCameraPatchTypeAssetCustomization::MakeInstance()
{
	return MakeShared<FComposableCameraPatchTypeAssetCustomization>();
}

void FComposableCameraPatchTypeAssetCustomization::Register(FPropertyEditorModule& PropertyEditorModule)
{
	PropertyEditorModule.RegisterCustomClassLayout(
		UComposableCameraPatchTypeAsset::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FComposableCameraPatchTypeAssetCustomization::MakeInstance));
}

void FComposableCameraPatchTypeAssetCustomization::Unregister(FPropertyEditorModule& PropertyEditorModule)
{
	if (UObjectInitialized())
	{
		PropertyEditorModule.UnregisterCustomClassLayout(
			UComposableCameraPatchTypeAsset::StaticClass()->GetFName());
	}
}

void FComposableCameraPatchTypeAssetCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// Hide inherited TypeAsset fields that have no meaning for Patches. The
	// hidden properties remain on the base UCLASS — only the Details panel
	// hides them when the asset is a UComposableCameraPatchTypeAsset.
	//
	// GET_MEMBER_NAME_CHECKED gives us a compile-time-checked FName so a
	// rename on the base class breaks the build here rather than silently
	// going un-hidden at runtime.
	DetailBuilder.HideProperty(
		GET_MEMBER_NAME_CHECKED(UComposableCameraTypeAsset, EnterTransition),
		UComposableCameraTypeAsset::StaticClass());
	DetailBuilder.HideProperty(
		GET_MEMBER_NAME_CHECKED(UComposableCameraTypeAsset, ExitTransition),
		UComposableCameraTypeAsset::StaticClass());
	DetailBuilder.HideProperty(
		GET_MEMBER_NAME_CHECKED(UComposableCameraTypeAsset, bDefaultPreserveCameraPose),
		UComposableCameraTypeAsset::StaticClass());
}
