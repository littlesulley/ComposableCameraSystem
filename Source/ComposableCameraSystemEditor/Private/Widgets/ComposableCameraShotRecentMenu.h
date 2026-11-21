// Copyright 2026 Sulley. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Textures/SlateIcon.h"

class SWidget;

namespace ComposableCameraSystem::ShotRecentMenu
{
/** Menu-open snapshot only: no retained UObject, asset loading or history mutation. */
struct FPresentation
{
	FText Title;
	FText Kind;
	FText Context;
	FText Detail;
	FText ToolTip;
	FSlateIcon Icon;
};

FPresentation Describe(const UObject* Host, const FText& FallbackLabel);
TSharedRef<SWidget> MakeEntry(const FPresentation& Presentation, bool bCurrent);
}
