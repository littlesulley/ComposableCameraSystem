// Copyright 2026 Sulley. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

namespace ComposableCameraSystem::ShotSubjectLabels
{
/** Display roles follow array order: A..Z, AA..AZ, BA... Never used as binding identities. */
inline void WriteRole(int32 Index, TCHAR (&Role)[8])
{
	if (Index < 0) { Role[0] = TEXT('?'); Role[1] = 0; return; }
	// Seven letters cover every non-negative int32 index; widen before adding one.
	int64 Value = static_cast<int64>(Index) + 1;
	int32 Length = 0;
	do
	{
		--Value;
		Role[Length++] = static_cast<TCHAR>(TEXT('A') + Value % 26);
		Value /= 26;
	} while (Value > 0);
	Role[Length] = 0;
	for (int32 Left = 0, Right = Length - 1; Left < Right; ++Left, --Right) Swap(Role[Left], Role[Right]);
}

inline FText ToText(int32 Index)
{
	TCHAR Role[8];
	WriteRole(Index, Role);
	return FText::Format(NSLOCTEXT("ShotSubjectLabels", "Subject", "Subject {0}"), FText::FromString(Role));
}

/** Allocation-free path for viewport drawing, matching the existing English overlay. */
inline void WriteLabel(int32 Index, TCHAR (&Label)[32])
{
	TCHAR Role[8];
	WriteRole(Index, Role);
	FCString::Snprintf(Label, UE_ARRAY_COUNT(Label), TEXT("Subject %s"), Role);
}
}
