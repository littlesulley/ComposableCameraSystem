// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace ComposableCameraSystem::ShotEditorMenu
{
inline bool MatchesSearchFilter(const FString& TrackLabel,
	const FString& TitleLabel,
	const FString& TimeLabel,
	const FString& FilterText)
{
	FString Trimmed = FilterText;
	Trimmed.TrimStartAndEndInline();
	if (Trimmed.IsEmpty())
	{
		return true;
	}

	const FString Haystack = FString::Printf(TEXT("%s %s %s"),
		*TrackLabel,
		*TitleLabel,
		*TimeLabel).ToLower();

	TArray<FString> Tokens;
	Trimmed.ParseIntoArrayWS(Tokens);
	for (FString& Token: Tokens)
	{
		Token.TrimStartAndEndInline();
		if (!Token.IsEmpty() && !Haystack.Contains(Token.ToLower()))
		{
			return false;
		}
	}
	return true;
}
} // namespace ComposableCameraSystem::ShotEditorMenu
