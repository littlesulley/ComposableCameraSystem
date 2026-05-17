// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FComposableCameraShot;
class UObject;
class SShotEditorRoot;
class SDockTab;
class FSpawnTabArgs;
class UMovieSceneComposableCameraShotSection;

/**
 * Static API + module-lifetime registry for the Shot Editor (Phase D of
 * Shot-Based Keyframing). The editor is a **single-instance nomad tab**
 * registered with `FGlobalTabmanager` - re-invoking `OpenForShot` while
 * the tab is already open swaps the active Shot context (per Q6 design),
 * it does not spawn a second window.
 *
 * Triggered by:
 * - `UComposableCameraCompositionFramingNode::OpenShotEditor` (V1 - 
 * Details-panel "Open Shot Editor" CallInEditor button on the node).
 * - Phase E LS Shot Section right-click -> "Open Shot Editor" (TBD).
 *
 * Routed via `FOpenShotEditor::OpenShotEditorDelegate` (declared in
 * `Public/EditorHooks/EditorHooks.h` on the runtime module side); this
 * editor module binds the delegate to `OpenForShot` in `StartupModule`.
 *
 * Lifetime model:
 * - Tab is registered/unregistered in module Startup/Shutdown.
 * - The single live `SShotEditorRoot` is held via TWeakPtr - when the
 * tab is closed, the widget is destroyed and OpenForShot's next
 * invocation will respawn it.
 * - The active `FComposableCameraShot*` is a raw pointer; the host
 * UObject is the lifetime anchor (TWeakObjectPtr inside the widget
 * guards against host destruction between frames).
 */
class COMPOSABLECAMERASYSTEMEDITOR_API FComposableCameraShotEditor
{
public:
	/** Stable tab identifier. Used by FGlobalTabmanager + Editor layout
	 * serialization, so it must not change between releases without a
	 * layout-migration plan. */
	static const FName TabId;

	/**
	 * Register the nomad tab spawner with FGlobalTabmanager and bind the
	 * runtime -> editor delegate hook (FOpenShotEditor) so the node's
	 * CallInEditor button routes here.
	 *
	 * Call from `FComposableCameraSystemEditorModule::StartupModule`.
	 */
	static void RegisterTabSpawner();

	/** Inverse of RegisterTabSpawner. Unregisters the tab + unbinds the
	 * delegate. Call from `ShutdownModule`. */
	static void UnregisterTabSpawner();

	/**
	 * Open the Shot Editor tab for `Shot` (owned by `HostObject`). If the
	 * tab is already open, just swap context - single-instance per Q6
	 * design. Idempotent: calling with the same (Shot, HostObject) is a
	 * no-op aside from focusing the tab.
	 *
	 * Both arguments may be nullptr - in that case the tab still opens
	 * (or focuses) but shows the "No Shot loaded" placeholder.
	 */
	static void OpenForShot(FComposableCameraShot* Shot, UObject* HostObject);

	/**
	 * Sequencer-driven entry - open the Shot Editor for a `UMovieSceneComposableCameraShotSection`.
	 * Resolves the section's editable Shot:
	 * - Inline -> host = the Section itself, Shot = `&Section->InlineShot`.
	 * - AssetReference + valid asset -> host = the Section itself,
	 * Shot = `&Section->ShotOverrides` seeded from the asset.
	 * - AssetReference + null/unresolved asset -> still opens the editor
	 * with (nullptr Shot, nullptr host), which falls through to the
	 * "no shot loaded" placeholder so the user knows their click landed.
	 *
	 * Used by the Sequencer selection sync (single-click on a Shot Section
	 * auto-swaps the editor's context).
	 */
	static void OpenForShotSection(UMovieSceneComposableCameraShotSection* Section);

	/**
	 * Return the host UObject the *currently live* Shot Editor tab is bound
	 * to, or nullptr if no tab is open / no Shot is loaded. Single-instance
	 * tab -> at most one live host at any time.
	 *
	 * Editor-side customizations (e.g. `FComposableCameraTargetInfoCustomization`)
	 * use this to answer "which UObject hosts the Shot whose data the
	 * Details view is currently editing?" when the Details view is the Shot
	 * Editor's `IStructureDetailsView`. That view wraps the Shot via a raw
	 * `FStructOnScope` with no host UObject, so `IPropertyHandle::GetOuterObjects`
	 * returns empty - the active-host lookup fills the gap. For the
	 * Sequencer-Section-Details path the customization can use
	 * `GetOuterObjects` directly; this static lookup is the fallback when
	 * that yields nothing.
	 */
	static UObject* GetCurrentLiveHost();

private:
	/** FOnSpawnTab callback registered with FGlobalTabmanager. */
	static TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args);
};
