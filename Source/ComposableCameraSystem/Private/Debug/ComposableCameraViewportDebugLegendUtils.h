// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Debug/ComposableCameraViewportDebug.h"

namespace ComposableCameraViewportDebugLegend
{
	inline const TCHAR* GetEntryClassToken(const FComposableCameraViewportDebugLegendEntry& Entry)
	{
		if (!Entry.CVarName)
		{
			return nullptr;
		}

		const TCHAR* Token = Entry.CVarName;
		for (const TCHAR* It = Entry.CVarName; *It != TCHAR('\0'); ++It)
		{
			if (*It == TCHAR('.'))
			{
				Token = It + 1;
			}
		}
		return Token;
	}

	inline bool EntryMatchesClassName(const FComposableCameraViewportDebugLegendEntry& Entry, const FString& ClassName)
	{
		const TCHAR* Token = GetEntryClassToken(Entry);
		return Token
			&& Token[0] != TCHAR('\0')
			&& ClassName.Contains(Token, ESearchCase::IgnoreCase);
	}

	inline bool EntryMatchesClass(const FComposableCameraViewportDebugLegendEntry& Entry, const UClass* Class)
	{
		for (const UClass* Candidate = Class; Candidate; Candidate = Candidate->GetSuperClass())
		{
			if (EntryMatchesClassName(Entry, Candidate->GetName()))
			{
				return true;
			}
		}
		return false;
	}
}
