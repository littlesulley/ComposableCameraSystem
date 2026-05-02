// Copyright Sulley. All Rights Reserved.

#include "Editors/ComposableCameraShotEditorHistory.h"

#include "Misc/ConfigCacheIni.h"

namespace
{
	/** Section name in `GEditorPerProjectIni`. The dotted-namespace style
	 *  matches the convention used by other CCS editor settings sections. */
	constexpr const TCHAR* kConfigSection = TEXT("ComposableCameraSystem.ShotEditorHistory");
}

FShotEditorHistory::FShotEditorHistory()
{
	Load();
}

FShotEditorHistory& FShotEditorHistory::Get()
{
	static FShotEditorHistory Instance;
	return Instance;
}

void FShotEditorHistory::Push(UObject* Host, const FText& DisplayLabel)
{
	if (!Host || DisplayLabel.IsEmpty())
	{
		return;
	}

	const FSoftObjectPath HostPath(Host);

	// Dedupe against existing entries that match either the live UObject
	// (typical in-session re-open) or the soft path (cross-restart re-open
	// where the previous entry was loaded from disk and the weak ref is
	// still cold). Removing first preserves the "newest at front"
	// invariant when the user re-opens an old entry.
	Entries.RemoveAll(
		[Host, &HostPath](const FShotEditorHistoryEntry& E)
		{
			if (E.Host.Get() == Host)
			{
				return true;
			}
			return !E.HostPath.IsNull() && E.HostPath == HostPath;
		});

	FShotEditorHistoryEntry New;
	New.Host         = Host;
	New.HostPath     = HostPath;
	New.DisplayLabel = DisplayLabel;
	Entries.Insert(MoveTemp(New), 0);

	if (Entries.Num() > MaxEntries)
	{
		Entries.SetNum(MaxEntries);
	}

	Save();
}

UObject* FShotEditorHistory::ResolveHost(const FShotEditorHistoryEntry& Entry)
{
	// Prefer the cached weak ref — when alive it bypasses the asset
	// registry / package loading path entirely.
	if (UObject* Live = Entry.Host.Get())
	{
		return Live;
	}
	// Fallback: load via the persisted soft path. `TryLoad` handles
	// sub-object paths (`Package.Asset:Outer.SubObject`) so Section / Node
	// hosts walk down to their host UObject after the parent package is
	// loaded. Returns null when the asset has been deleted or moved
	// without a redirector.
	if (Entry.HostPath.IsValid())
	{
		return Entry.HostPath.TryLoad();
	}
	return nullptr;
}

void FShotEditorHistory::Clear()
{
	Entries.Reset();
	if (GConfig)
	{
		GConfig->EmptySection(kConfigSection, GEditorPerProjectIni);
		GConfig->Flush(/*bRead=*/false, GEditorPerProjectIni);
	}
}

void FShotEditorHistory::Load()
{
	Entries.Reset();
	if (!GConfig)
	{
		return;
	}

	// Contiguous `PathN` / `LabelN` pairs. First missing key pair signals
	// end-of-list — manual ini edits that punch a hole in the middle just
	// truncate the list there, no crash.
	for (int32 i = 0; i < MaxEntries; ++i)
	{
		const FString PathKey  = FString::Printf(TEXT("Path%d"),  i);
		const FString LabelKey = FString::Printf(TEXT("Label%d"), i);

		FString PathStr;
		FString LabelStr;
		const bool bHasPath = GConfig->GetString(
			kConfigSection, *PathKey,  PathStr,  GEditorPerProjectIni);
		const bool bHasLabel = GConfig->GetString(
			kConfigSection, *LabelKey, LabelStr, GEditorPerProjectIni);
		if (!bHasPath || !bHasLabel)
		{
			break;
		}

		FShotEditorHistoryEntry E;
		E.HostPath     = FSoftObjectPath(PathStr);
		E.DisplayLabel = FText::FromString(LabelStr);
		// Host weak ref intentionally left empty — the asset isn't loaded
		// yet (and shouldn't be force-loaded just to populate the menu).
		// `ResolveHost` lazily promotes the soft path to a live UObject on
		// first click.
		Entries.Add(MoveTemp(E));
	}
}

void FShotEditorHistory::Save()
{
	if (!GConfig)
	{
		return;
	}

	// Wipe the section before re-writing so old keys past the current
	// length don't linger (e.g., list shrinks via Clear or eviction).
	GConfig->EmptySection(kConfigSection, GEditorPerProjectIni);

	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		const FShotEditorHistoryEntry& E = Entries[i];
		if (!E.HostPath.IsValid() || E.DisplayLabel.IsEmpty())
		{
			continue;
		}
		const FString PathKey  = FString::Printf(TEXT("Path%d"),  i);
		const FString LabelKey = FString::Printf(TEXT("Label%d"), i);
		GConfig->SetString(kConfigSection, *PathKey,
			*E.HostPath.ToString(),  GEditorPerProjectIni);
		GConfig->SetString(kConfigSection, *LabelKey,
			*E.DisplayLabel.ToString(), GEditorPerProjectIni);
	}

	GConfig->Flush(/*bRead=*/false, GEditorPerProjectIni);
}
