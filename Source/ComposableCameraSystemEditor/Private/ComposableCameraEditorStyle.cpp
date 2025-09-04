// Copyright Sulley. All rights reserved.

#include "ComposableCameraEditorStyle.h"

#include "Interfaces/IPluginManager.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/SlateTypes.h"
#include "Styling/StyleColors.h"

TSharedPtr<FComposableCameraEditorStyle> FComposableCameraEditorStyle::Singleton;

FComposableCameraEditorStyle::FComposableCameraEditorStyle()
	: FSlateStyleSet("ComposableCameraEditorStyle")
{
	const FVector2D Icon16x16(16.0f, 16.0f);
	const FVector2D Icon20x20(20.0f, 20.0f);
	const FVector2D Icon24x24(24.0f, 24.0f);
	const FVector2D Icon40x40(40.0f, 40.0f);
	const FVector2D Icon48x48(48.0f, 48.0f);
	const FVector2D Icon64x64(64.0f, 64.0f);
	
	const FString ContentDir = IPluginManager::Get().FindPlugin(TEXT("ComposableCameraSystem"))->GetContentDir();
	SetContentRoot(ContentDir);
	SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));
	
	Set("CameraContextParameter.VariableBrowser", new IMAGE_BRUSH_SVG("Icons/CameraParameter-Variable", Icon16x16));

	FSlateStyleRegistry::RegisterSlateStyle(*this);
}

FComposableCameraEditorStyle::~FComposableCameraEditorStyle()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*this);
}

TSharedRef<FComposableCameraEditorStyle> FComposableCameraEditorStyle::Get()
{
	if (!Singleton.IsValid())
	{
		Singleton = MakeShareable(new FComposableCameraEditorStyle);
	}
	return Singleton.ToSharedRef();
}
