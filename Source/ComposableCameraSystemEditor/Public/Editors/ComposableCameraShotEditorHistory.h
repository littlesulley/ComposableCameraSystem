// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/WeakObjectPtr.h"

/**
 * One entry in the Shot Editor's "recently opened" history. Captures the
 * host UObject the editor was bound to (as both a live weak ref AND a
 * stable soft-object path), plus a snapshot of the display label at
 * push-time so a stale entry (host GC'd, asset unloaded, or only known
 * via a freshly-loaded ini line) still renders as a useful breadcrumb
 * in the dropdown.
 */
struct FShotEditorHistoryEntry
{
	/** Weak ref to the host UObject - `UComposableCameraCompositionFramingNode`,
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

	/** Display string captured at push-time (e.g. "MyLS -> Hero Track->Inline (3 targets) (0.00s - 2.50s, Row 0)").
	 * Survives host destruction + ini round-trips so the menu can show what
	 * *was* there even when the live ref is gone and the asset is unloaded. */
	FText DisplayLabel;
};

/**
 * Persistent recently-opened history for the Shot Editor, surfaced as a
 * dropdown on the editor's header bar (Section 23.12 of EditorDesignDoc).
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

	/** Push a (host, label) pair onto the history. No-op when Host is null
	 * or the label is empty. Dedupes against existing entries with the same
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

	/** Drop all entries (in memory + on disk). */
	void Clear();

	/** Resolve the live UObject for a history entry. Returns the cached
	 * weak ref when valid; otherwise calls `HostPath.TryLoad()` which may
	 * trigger a synchronous package load. Returns null when neither path
	 * is recoverable (asset deleted / moved without a redirector). */
	static UObject* ResolveHost(const FShotEditorHistoryEntry& Entry);

private:
	/** Private ctor so `Get()`'s static local is the only construction
	 * path. Loads persisted entries on first construction. */
	FShotEditorHistory();

	/** Repopulate `Entries` from the contiguous `[ComposableCameraSystem.ShotEditorHistory]`
	 * ini section. Tolerant of trailing gaps (stops at first missing key
	 * pair) so manual edits to the ini don't drop the rest of the list. */
	void Load();

	/** Mirror `Entries` to the ini section, dropping any entries whose
	 * `HostPath` or `DisplayLabel` is empty (defensive - should never
	 * happen given `Push`'s validation, but a corrupt in-memory state
	 * shouldn't yield a corrupt on-disk record). */
	void Save();

	TArray<FShotEditorHistoryEntry> Entries;
};
