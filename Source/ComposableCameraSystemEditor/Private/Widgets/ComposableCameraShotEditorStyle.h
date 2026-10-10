// Copyright 2026 Sulley. All Rights Reserved.
#pragma once

#include "Brushes/SlateRoundedBoxBrush.h"
#include "ComposableCameraEditorStyle.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/StyleColors.h"
#include "Styling/SlateTypes.h"
#include "Styling/SegmentedControlStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace ComposableCameraSystem::ShotEditorStyle
{
inline FLinearColor Accent() { return FComposableCameraEditorColors::CameraNodeTitle; }
inline FSlateColor Muted() { return FStyleColors::Foreground; }
inline const FSlateBrush* NavigationBrush() { return FAppStyle::GetBrush("Brushes.Background"); }
inline const FButtonStyle* TabStyle()
{
	// White fills allow the shared camera accent / native Secondary palette to tint a retained button.
	static const FButtonStyle Style = FButtonStyle(FAppStyle::Get().GetWidgetStyle<FButtonStyle>("Button"))
		.SetNormal(FSlateRoundedBoxBrush(FLinearColor::White, 4.f))
		.SetHovered(FSlateRoundedBoxBrush(FLinearColor(.8f, .8f, .8f), 4.f))
		.SetPressed(FSlateRoundedBoxBrush(FLinearColor(.65f, .65f, .65f), 4.f));
	return &Style;
}
inline FLinearColor TabColor(bool bSelected)
{
	return bSelected ? Accent() : FStyleColors::Secondary.GetSpecifiedColor();
}
inline FLinearColor SoftAccent(float Strength)
{
	return FMath::Lerp(FStyleColors::Secondary.GetSpecifiedColor(), Accent(), Strength);
}
inline FLinearColor SecondaryTabColor(bool bSelected)
{
	return bSelected ? SoftAccent(.65f) : FStyleColors::Secondary.GetSpecifiedColor();
}
inline FLinearColor HeaderColor() { return FLinearColor(.085f, .09f, .10f, 1.f); }
inline const FButtonStyle* ActionStyle()
{
	return &FAppStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
}
inline const FSegmentedControlStyle* ModeStyle()
{
	static const FSegmentedControlStyle Style = []()
	{
		FSegmentedControlStyle Result = FAppStyle::Get().GetWidgetStyle<FSegmentedControlStyle>("SegmentedControl");
		for (FCheckBoxStyle* Control : { &Result.FirstControlStyle, &Result.ControlStyle, &Result.LastControlStyle })
		{
			Control->CheckedImage.TintColor = Accent() * FLinearColor(.65f, .65f, .65f, 1.f);
			Control->CheckedHoveredImage.TintColor = Accent() * FLinearColor(.85f, .85f, .85f, 1.f);
			Control->CheckedPressedImage.TintColor = Accent() * FLinearColor(.35f, .35f, .35f, 1.f);
			Control->UncheckedHoveredImage.TintColor = FStyleColors::Hover;
			Control->UncheckedPressedImage.TintColor = FStyleColors::Secondary;
			Control->SetCheckedForegroundColor(FStyleColors::ForegroundHover)
				.SetCheckedHoveredForegroundColor(FStyleColors::ForegroundHover)
				.SetCheckedPressedForegroundColor(FStyleColors::ForegroundHover);
		}
		return Result;
	}();
	return &Style;
}
inline TSharedRef<SWidget> Action(TSharedRef<SButton> Button, float Width = 156.f)
{
	Button->SetButtonStyle(ActionStyle());
	Button->SetContentPadding(FMargin(6.f, 0.f));
	Button->SetHAlign(HAlign_Center);
	Button->SetVAlign(VAlign_Center);
	return SNew(SBox).HAlign(HAlign_Left).VAlign(VAlign_Center)
		[SNew(SBox).WidthOverride(Width).HeightOverride(24.f)[Button]];
}
inline TSharedRef<SWidget> GroupHeader(const FText& Label)
{
	return SNew(SBorder).BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(HeaderColor()).Padding(8.f, 4.f)
		[SNew(STextBlock).Text(Label).Font(FAppStyle::GetFontStyle("BoldFont"))
			.ColorAndOpacity(FStyleColors::ForegroundHover)];
}
inline TSharedRef<SWidget> SubsectionHeader(const FText& Label)
{
	// Match the retained native struct/array rows (e.g. preview transforms).
	return SNew(SBox).MinDesiredHeight(24.f).VAlign(VAlign_Center).Padding(0.f, 1.f)
		[SNew(STextBlock).Text(Label).Font(FAppStyle::GetFontStyle("NormalFont"))];
}
inline float ColumnWidth(float Available)
{
	Available = FMath::Max(160.f, Available);
	return Available >= 800.f ? (Available - 24.f) * .5f : Available;
}
inline TSharedRef<SWidget> Group(const FText& Label, TSharedRef<SWidget> Content)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
		[GroupHeader(Label)]
		+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 0.f)[Content];
}
}
