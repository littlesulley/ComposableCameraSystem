// Copyright Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/NotifyHook.h"
#include "Widgets/SCompoundWidget.h"
#include "UObject/WeakObjectPtr.h"

struct FComposableCameraShot;
struct FShotEditorListEntry;
class STextBlock;
class SShotEditorViewport;
class IStructureDetailsView;
class FStructOnScope;
class SComboButton;
class SWidget;
class ITableRow;
class STableViewBase;
template<typename ItemType> class SListView;

// Forward decl must match the definition's base type (`: uint8`) - C++
// rejects mismatched underlying types between fwd-decl and definition.
enum class EShotEditorMode: uint8;

/**
 * Root widget for the Shot Editor - owns the multi-region layout
 * (left: Shot outliner, center: 3D viewport, right: Details panel) and
 * tracks the currently active Shot context. Phase D.1 ships **placeholder
 * regions only** (text labels saying "Outliner / Viewport / Details");
 * Phase D.2 fills the viewport with a real preview scene, D.3 fills
 * Outliner + Details with real authoring widgets, D.4 wires up drag
 * interaction, D.5 hooks into Sequencer Section selection (Phase E
 * dependency).
 *
 * Lifetime: held by the Shot Editor's SDockTab. Construct() runs once
 * when the tab is spawned; the widget persists across SetActiveShot()
 * calls (single-instance auto-switch per Q6 design).
 *
 * The active Shot pointer is RAW (not a TSharedPtr / smart wrapper)
 * because the data lives inside a UObject UPROPERTY (the host node's
 * `Shot`) - its lifetime is bounded by the host's. We hold a
 * TWeakObjectPtr to the host so we can detect host destruction (e.g.
 * Camera Type Asset closed, GC swept) and gracefully degrade to the
 * "No Shot loaded" placeholder.
 */
class COMPOSABLECAMERASYSTEMEDITOR_API SShotEditorRoot: public SCompoundWidget, public FNotifyHook // bridges struct-detail-view edits to host UObject's
	 // Modify() / PostEditChangeProperty so undo + host
	 // listeners (graph-node refresh, etc.) work.
{
public:
	SLATE_BEGIN_ARGS(SShotEditorRoot) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Currently bound host UObject, or nullptr when no Shot is loaded. Used
	 * by editor-side customizations that need the Section / ShotAsset
	 * context the IStructureDetailsView's `FStructOnScope` doesn't carry. */
	UObject* GetActiveHost() const { return ActiveHost.Get(); }

	/**
	 * Bind the editor to a specific Shot owned by HostObject. Triggers
	 * `OnActiveShotChanged` which (in Phase D.1) just refreshes the host
	 * name label, and (in later Phase D.x steps) will rebuild preview
	 * proxies, refresh AutoBounds caches, retrigger SolveShot, and update
	 * the Details panel.
	 *
	 * Both args may be nullptr - clears the active context and shows the
	 * "No Shot loaded" placeholder.
	 */
	void SetActiveShot(FComposableCameraShot* Shot, UObject* HostObject);

	// SWidget interface - used to detect host destruction between frames
	// (the host's TWeakObjectPtr can go stale at any GC pass).
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** Hotkeys: 1 = Drag, 2 = Free, 3 = Lock - only fire when no descendant
	 * text-input has captured keyboard focus (Slate's normal focus chain
	 * ensures this - text boxes consume the key first). */
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

	// FNotifyHook - bridges struct-detail-view edits to the host UObject.
	// - NotifyPreChange: host->Modify() (records undo snapshot).
	// - NotifyPostChange: host->PostEditChangeProperty() with the host's
	// outer "Shot" property so host-level listeners
	// (graph-node visualization, Build pipeline, etc.)
	// react to Shot mutations identically to a direct
	// host-level edit.
	virtual void NotifyPreChange(FProperty* PropertyAboutToChange) override;
	virtual void NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent,
		FProperty* PropertyThatChanged) override;

private:
	/** Shot data being edited. NOT owned - host UObject's UPROPERTY owns
	 * it. nullptr when no Shot is bound (e.g. tab restored from saved
	 * layout before any node has triggered the open flow). */
	FComposableCameraShot* ActiveShot { nullptr };

	/** Weak ref to the host UObject (CompositionFramingNode in V1, future
	 * LS Shot Section in Phase E). Used as a liveness guard - when this
	 * goes stale we clear ActiveShot to avoid dangling-pointer reads. */
	TWeakObjectPtr<UObject> ActiveHost;

	/** Slot for the host context chain label ("Asset -> Node" or
	 * "LS->Track -> Section") so it can be re-driven on context swap
	 * without rebuilding the whole layout. */
	TSharedPtr<STextBlock> HostNameLabel;

	/** "Shots in Sequence" dropdown - visible only when the active host is a
	 * `UMovieSceneComposableCameraShotSection`. Lists every Shot section in
	 * the host's parent LevelSequence so the designer can jump between them
	 * without round-tripping through Sequencer. Hidden in the
	 * CameraTypeAsset / standalone-ShotAsset case. See Section 23.12 of EditorDesignDoc. */
	TSharedPtr<SComboButton> LSShotsCombo;

	/** "Recent" dropdown - module-lifetime history of the last 20 hosts the
	 * Shot Editor was bound to. Always visible (greyed out when history is
	 * empty). Backed by `FShotEditorHistory` singleton. */
	TSharedPtr<SComboButton> HistoryCombo;

	/** The 3D preview viewport (Phase D.2). Forwarded SetActiveShot calls
	 * drive proxy rebuilds + solver-driven camera updates inside this widget. */
	TSharedPtr<SShotEditorViewport> Viewport;

	/** Right-pane structure details view (Phase D.3). Bound to the active
	 * Shot via FStructOnScope wrapping the raw `FComposableCameraShot*`
	 * inside the host UObject. The wrapper's `bOwnsMemory=false` because
	 * the host UObject owns the actual struct memory. NotifyHook (this
	 * widget) bridges edit events to host->Modify / PostEditChangeProperty
	 * so undo + host-level listeners stay in sync. */
	TSharedPtr<IStructureDetailsView> StructureDetailsView;

	/** Rebuilds the StructureDetailsView's data binding from the current
	 * ActiveShot + ActiveHost. Called from OnActiveShotChanged. Clears
	 * the panel when no Shot is bound. */
	void RefreshDetailsView();

	/** Update label text + (later) preview proxies / Details panel when
	 * ActiveShot or ActiveHost changes. */
	void OnActiveShotChanged();

	/**
	 * Compose the host-context chain shown in the header label:
	 * - Section host -> "{LS} -> {Track} -> {Section}"
	 * - CompositionFraming -> "{TypeAsset} -> {Node}"
	 * - ShotAsset host -> "{ShotAsset}"
	 * - Other / unknown -> "{HostName} ({HostClass})" fallback
	 * - No host bound -> instructional placeholder
	 *
	 * Used by both the header label and as the snapshot string pushed onto
	 * `FShotEditorHistory` when the editor binds a new context.
	 */
	FText BuildHostContextChain() const;

	/** Build the dropdown menu listing every Shot section in the active
	 * host's parent LevelSequence. Each entry routes through
	 * `FComposableCameraShotEditor::OpenForShotSection` so the standard
	 * context-swap path runs (Section->ResolveShotEditorHost). Returns an
	 * empty placeholder when the host isn't a Section or its LS is
	 * unresolvable. */
	TSharedRef<SWidget> BuildLSShotsMenu();

	/** Build the dropdown menu listing the last 20 hosts the Shot Editor
	 * was bound to (most-recent-first). Stale entries (host GC'd) render
	 * in the muted style and are non-interactive. */
	TSharedRef<SWidget> BuildHistoryMenu();

	/** Visibility predicate for `LSShotsCombo` - `EVisibility::Visible` only
	 * when ActiveHost resolves to a `UMovieSceneComposableCameraShotSection`. */
	EVisibility GetLSShotsComboVisibility() const;

	/** Apply a mode change with the standard Free-leaving reverse-solve
	 * dialog. Shared between SSegmentedControl's OnValueChanged and the
	 * hotkey handler so both paths produce identical UX. No-op when
	 * Viewport is invalid or NewMode == current mode. */
	void TrySetMode(EShotEditorMode NewMode);

	// Asset toolbar (Save / Browse / Refresh) 
	//
	// Standard `FAssetEditorToolkit`-style chrome built inside the nomad
	// tab to give the Shot Editor the visual + functional feel of an asset
	// editor without inheriting the full toolkit (the Shot Editor's
	// single-instance + multi-host-type model doesn't fit `FBaseAssetToolkit`
	// - see EditorDesignDoc Section 23.1). The toolbar walks the active host's
	// outer chain to act on its containing asset / package.

	/** Build the top-of-window asset toolbar (Save / Browse to Asset /
	 * Refresh). Wrapped in `FToolBarBuilder` so the look matches the
	 * engine's standard asset-editor toolbars. */
	TSharedRef<class SWidget> BuildAssetToolbar();

	/** Save the package containing `ActiveHost` (CameraTypeAsset for
	 * CompositionFramingNode hosts, LevelSequence for Section hosts,
	 * ShotAsset itself when host IS a ShotAsset). Routes through
	 * `FEditorFileUtils::PromptForCheckoutAndSave` so the standard
	 * source-control / dirty checks fire. */
	void OnSaveClicked();
	bool CanSave() const;

	/** Sync the Content Browser to the outermost asset containing
	 * `ActiveHost`. Walks the outer chain until finding an object whose
	 * outer is the package, treating that as the asset to focus. */
	void OnBrowseClicked();
	bool CanBrowse() const;

	/** Force-rebuild the StructureDetailsView's binding to the active
	 * Shot - useful as a recovery action when a host-level edit didn't
	 * propagate cleanly to the panel. */
	void OnRefreshClicked();

	void OnCopyViewportCameraTransformClicked();
	bool CanCopyViewportCameraTransform() const;

	// Shot outliner (Polish E.4) 
	//
	// Always-visible left-pane list of Shot sections in the active host's
	// LevelSequence - single-click an entry to swap context. Faster than
	// menu-traversing the header's "Shots" dropdown for multi-Shot
	// authoring sessions, and the "Current" entry is visually distinct
	// instead of being prefixed with `"Current - "` plain text.
	//
	// Empty / non-Section host context: the pane shows a placeholder
	// `(no Shot sections in scope)` row - splitter geometry stays stable
	// rather than collapsing the slot, so layout doesn't jitter when the
	// designer swaps between Section and CompositionFramingNode hosts.

	/** Build the left-pane Shot outliner (header + SListView). */
	TSharedRef<SWidget> BuildShotOutliner();

	/** Walk the active LS for Shot sections and rebuild `ShotListItems`.
	 * Called from `OnActiveShotChanged()` (covers context swap) and from
	 * Tick() on a `~0.5s` throttle (covers external LS edits - section
	 * add / remove / reorder via Sequencer). */
	void RefreshShotListItems();

	/** SListView's OnGenerateRow - renders one outliner row. */
	TSharedRef<ITableRow> MakeShotListRow(TSharedPtr<FShotEditorListEntry> Entry,
		const TSharedRef<STableViewBase>& OwnerTable);

	/** SListView's `OnMouseButtonClick` - fires on every item click
	 * independently of selection state, so the swap path never races
	 * with Slate's selection machinery. Picks the standard
	 * `OpenForShotSection` route to swap context.
	 *
	 * Going through `OnMouseButtonClick` rather than `OnSelectionChanged`:
	 * the latter has multi-source signal noise (user clicks, programmatic
	 * Direct sets, keyboard navigation) and Slate's internal
	 * `Private_SetItemSelection` from the click commits BEFORE our handler
	 * runs - any subsequent programmatic set in the same frame wins
	 * unpredictably depending on engine version. `OnMouseButtonClick`
	 * only fires on actual mouse clicks, fully decoupling the swap
	 * decision from the selection visual. */
	void OnShotListMouseButtonClick(TSharedPtr<FShotEditorListEntry> ClickedEntry);

	/** List items, refreshed via `RefreshShotListItems`. Strong refs so
	 * the SListView can sample row data without re-walking the LS. */
	TArray<TSharedPtr<FShotEditorListEntry>> ShotListItems;

	/** The list widget itself - kept as a member so we can call
	 * `RequestListRefresh()` after rebuilding `ShotListItems` and
	 * `SetItemSelection` to highlight the current Shot. */
	TSharedPtr<SListView<TSharedPtr<FShotEditorListEntry>>> ShotListView;

	/** Time accumulator for Tick-based list refresh throttle. The walk
	 * is cheap (handful of sections in a typical LS) but doing it 60x
	 * per second is wasteful when nothing's changed. */
	float ShotListRefreshAccum { 0.f };
};
