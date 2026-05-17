// Copyright 2026 Sulley. All Rights Reserved.

#include "ComposableCameraEditorStyle.h"

#include "Interfaces/IPluginManager.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/SlateTypes.h"
#include "Styling/StyleColors.h"

TSharedPtr<FComposableCameraEditorStyle> FComposableCameraEditorStyle::Singleton;

// FComposableCameraEditorColors 
//
// The literals below are the same values that used to live inline in the
// schema's GetPinTypeColor switch and on each sentinel / variable / regular
// node's GetNodeTitleColor override. Keeping all of them in one place makes
// the palette inspectable at a glance and gives design-doc readers one
// symbol to reference per color.

const FLinearColor FComposableCameraEditorColors::PinExec = FLinearColor::White;
const FLinearColor FComposableCameraEditorColors::PinBool = FLinearColor(0.9f, 0.0f, 0.0f, 1.0f);
const FLinearColor FComposableCameraEditorColors::PinInt = FLinearColor(0.0f, 0.8f, 0.6f, 1.0f);
const FLinearColor FComposableCameraEditorColors::PinFloat = FLinearColor(0.35f, 0.85f, 0.0f, 1.0f);
const FLinearColor FComposableCameraEditorColors::PinVector = FLinearColor(1.0f, 0.85f, 0.0f, 1.0f);
const FLinearColor FComposableCameraEditorColors::PinRotator = FLinearColor(0.5f, 0.5f, 1.0f, 1.0f);
const FLinearColor FComposableCameraEditorColors::PinTransform = FLinearColor(1.0f, 0.5f, 0.0f, 1.0f);
const FLinearColor FComposableCameraEditorColors::PinStructGeneric = FLinearColor(0.0f, 0.6f, 0.9f, 1.0f);
const FLinearColor FComposableCameraEditorColors::PinObject = FLinearColor(0.0f, 0.4f, 0.9f, 1.0f);
const FLinearColor FComposableCameraEditorColors::PinName = FLinearColor(1.0f, 0.7529f, 0.7960f, 1.0f);
const FLinearColor FComposableCameraEditorColors::PinByteEnum = FLinearColor(0.0f, 0.7490f, 1.0f, 1.0f);
const FLinearColor FComposableCameraEditorColors::PinDelegate = FLinearColor(1.0f, 0.0f, 0.0f, 1.0f);
const FLinearColor FComposableCameraEditorColors::PinDefault = FLinearColor::White;

const FLinearColor FComposableCameraEditorColors::CameraNodeTitle = FLinearColor(FColor(20, 150, 140));
const FLinearColor FComposableCameraEditorColors::ComputeNodeTitle = FLinearColor(0.75f, 0.5f, 0.15f);
const FLinearColor FComposableCameraEditorColors::VariableNodeTitle = FLinearColor(0.45f, 0.25f, 0.6f);
const FLinearColor FComposableCameraEditorColors::StartNodeTitle = FLinearColor(0.1f, 0.6f, 0.1f);
const FLinearColor FComposableCameraEditorColors::OutputNodeTitle = FLinearColor(0.7f, 0.1f, 0.1f);
const FLinearColor FComposableCameraEditorColors::BeginPlayStartNodeTitle = FLinearColor(0.85f, 0.55f, 0.1f);

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

	// Content Browser thumbnails and class icons 
	// The engine resolves these by the naming convention
	// "ClassIcon.<ClassName>" / "ClassThumbnail.<ClassName>" (without the U
	// prefix). Registering them here makes the Content Browser pick them up
	// automatically - no GetThumbnailBrush() override needed.

	// Camera Type Asset
	Set("ClassIcon.ComposableCameraTypeAsset",
		new IMAGE_BRUSH_SVG("Icons/ContentBrowser-ComposableCameraType", Icon16x16));
	Set("ClassThumbnail.ComposableCameraTypeAsset",
		new IMAGE_BRUSH_SVG("Icons/ContentBrowser-ComposableCameraType", Icon64x64));

	// Camera Patch Type Asset
	Set("ClassIcon.ComposableCameraPatchTypeAsset",
		new IMAGE_BRUSH_SVG("Icons/ContentBrowser-ComposableCameraPatch", Icon16x16));
	Set("ClassThumbnail.ComposableCameraPatchTypeAsset",
		new IMAGE_BRUSH_SVG("Icons/ContentBrowser-ComposableCameraPatch", Icon64x64));

	// Transition Table Data Asset
	Set("ClassIcon.ComposableCameraTransitionTableDataAsset",
		new IMAGE_BRUSH_SVG("Icons/ContentBrowser-ComposableCameraTransitionTable", Icon16x16));
	Set("ClassThumbnail.ComposableCameraTransitionTableDataAsset",
		new IMAGE_BRUSH_SVG("Icons/ContentBrowser-ComposableCameraTransitionTable", Icon64x64));

	// Node Modifier Data Asset
	Set("ClassIcon.ComposableCameraNodeModifierDataAsset",
		new IMAGE_BRUSH_SVG("Icons/ContentBrowser-ComposableCameraModifier", Icon16x16));
	Set("ClassThumbnail.ComposableCameraNodeModifierDataAsset",
		new IMAGE_BRUSH_SVG("Icons/ContentBrowser-ComposableCameraModifier", Icon64x64));

	// Transition Data Asset
	Set("ClassIcon.ComposableCameraTransitionDataAsset",
		new IMAGE_BRUSH_SVG("Icons/ContentBrowser-ComposableCameraTransition", Icon16x16));
	Set("ClassThumbnail.ComposableCameraTransitionDataAsset",
		new IMAGE_BRUSH_SVG("Icons/ContentBrowser-ComposableCameraTransition", Icon64x64));

	// Shot Asset - reusable framing preset (Phase E of Shot-Based Keyframing).
	// Visual: small camera + framing rectangle with rule-of-thirds grid + an
	// anchor dot in teal-green accent matching the asset's Content Browser
	// color (FColor(64, 192, 160) #40C0A0).
	Set("ClassIcon.ComposableCameraShotAsset",
		new IMAGE_BRUSH_SVG("Icons/ContentBrowser-ComposableCameraShot", Icon16x16));
	Set("ClassThumbnail.ComposableCameraShotAsset",
		new IMAGE_BRUSH_SVG("Icons/ContentBrowser-ComposableCameraShot", Icon64x64));

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
