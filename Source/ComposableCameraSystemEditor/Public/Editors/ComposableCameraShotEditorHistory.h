// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/WeakObjectPtr.h"

/**
 * One entry in the Shot Editor's "recently opened" history. Captures the
 * host UObject the editor was bound to (as both a live weak ref AND a
 * stable soft-object path), plus a snapshot of the display label at
 * push-time. Menu construction resolves and removes unavailable hosts;
 * a valid soft path alone does not prove that a Shot still exists.
 */
struct FShotEditorHistoryEntry
{
	/** Weak ref to the host UObject - `UComposableCameraInstanceData`, `UComposableCameraCompositionFramingNode`,
	 * `UMovieSceneComposableCameraShotSection`, or `UComposableCameraShotAsset`.
	 * Goes stale on host destruction. Empty for entries loaded from
	 * the ini before their host asset is back in memory. */
	TWeakObjectPtr<UObject> Host;

	/** Stable soft path used to re-resolve the host across editor restarts
	 * (or after the host's package was unloaded). `FSoftObjectPath` handles
	 * sub-object paths verbatim - Section / Node hosts encode their
	 * parent-package + sub-object route here, and `TryLoad()` walks down
	 * to the right UObject when invoked. */
	FSoftObjectPath HostPath;

	/** Fallback label captured at push-time, retained across ini round-trips. */
	FText DisplayLabel;
};

/**
 * Persistent recently-opened history for the Shot Editor, surfaced as a
 * dropdown on the editor's top bar (Section 14 of EditorDesignDoc).
 * Capacity is hard-capped at `MaxEntries = 20`.
 *
 * Persistence: entries are mirrored into `GEditorPerProjectIni` under
 * `[ComposableCameraSystem.ShotEditorHistory]` (`PathN` / `LabelN`
 * key-pairs, contiguous from N=0). Lazy-loaded on first `Get()` call;
 * each `Push` calls `Save()` so the on-disk copy stays current.
 *
 * Thread safety: editor-module singleton; main-thread only - same as
 * Slate widgets. No locking.
 *
 * Dedupe semantics: pushing an entry whose `Host` (or whose `HostPath`)
 * matches an existing entry removes the older entry, then prepends the
 * new one. So the most-recent-first ordering is preserved without
 * duplicates, even when an entry is re-opened from the persisted list.
 */
class COMPOSABLECAMERASYSTEMEDITOR_API FShotEditorHistory
{
public:
	/** Hard cap on entries - older entries are evicted when the cap is reached. */
	static constexpr int32 MaxEntries = 20;

	/** Singleton accessor. Lazy-constructed on first call; the constructor
	 * loads any persisted entries from `GEditorPerProjectIni`. */
	static FShotEditorHistory& Get();

	/** Push a (host, label) pair onto the history. No-op when Host is unavailable
	 * (including scratch drafts) or the label is empty. Dedupes against entries with the same
	 * Host *or* `FSoftObjectPath` (older one removed, new one prepended).
	 * Capped at MaxEntries. Persists the updated list to disk before
	 * returning.
	 *
	 * Caller is responsible for computing the label - keeps the history
	 * free of presentation logic + lets the same label appear in both the
	 * history dropdown and the active-host row. */
	void Push(UObject* Host, const FText& DisplayLabel);

	/** Read-only view, most-recent-first. Stable for the duration of the
	 * caller's frame (the underlying TArray is owned by the singleton). */
	const TArray<FShotEditorHistoryEntry>& GetEntries() const { return Entries; }

	/** Resolve saved hosts and prune deleted assets/detached subobjects. Call only
	 * when opening Recent, never on Tick: cold entries may load their package.
	 * Refreshes weak refs and renamed paths, preserving most-recent-first order. */
	void PruneUnavailable();

	/** Check current ownership, not merely UObject lifetime. Does not load assets. */
	static bool IsHostAvailable(UObject* Host);

	/** Drop all entries (in memory + on disk). */
	void Clear();

	/** Resolve the live UObject for a history entry. Returns the cached
	 * weak ref when valid; otherwise calls `HostPath.TryLoad()` which may
	 * trigger a synchronous package load. Returns null when neither path
	 * is recoverable or the host is no longer part of its owning asset. */
	static UObject* ResolveHost(const FShotEditorHistoryEntry& Entry);

private:
	friend class FShotEditorHistoryValidityTest;
	/** Tests use an isolated in-memory history without reading/writing user settings. */
	explicit FShotEditorHistory(bool bInPersist = true);

	/** Read contiguous ini key pairs, stopping at the first missing pair. */
	void Load();

	/** Persist recoverable asset paths using contiguous keys; never save transient hosts. */
	void Save();

	TArray<FShotEditorHistoryEntry> Entries;
	bool bPersist = true;
};
