// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/NotifyHook.h"
#include "Styling/SlateColor.h"
#include "Types/SlateEnums.h"
#include "Widgets/SCompoundWidget.h"
#include "UObject/WeakObjectPtr.h"

struct FComposableCameraShot;
class STextBlock;
class SShotEditorViewport;
class IStructureDetailsView;
class FStructOnScope;
class SComboButton;
class SWidget;

// Forward decl must match the definition's base type (`: uint8`) - C++
// rejects mismatched underlying types between fwd-decl and definition.
enum class EShotEditorMode: uint8;
enum class EShotEditorReverseSolveStatus: uint8;

/**
 * Root widget for the Shot Editor. Owns the compact top bar, 3D preview
 * viewport, right Details panel, and active Shot context.
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
	 * `OnActiveShotChanged`, which refreshes the header, preview proxies,
	 * AutoBounds caches, solver preview, and Details binding.
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
	enum class EQuickControlField : uint8
	{
		Distance,
		FOV,
		Roll,
		PlacementX,
		PlacementY,
		AimX,
		AimY
	};

	/** Shot data being edited. NOT owned - host UObject's UPROPERTY owns
	 * it. nullptr when no Shot is bound (e.g. tab restored from saved
	 * layout before any node has triggered the open flow). */
	FComposableCameraShot* ActiveShot { nullptr };

	/** Weak ref to the host UObject (CompositionFramingNode, Shot Section,
	 * ShotAsset, etc.). Used as a liveness guard - when this
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
	 * CameraTypeAsset / standalone-ShotAsset case. See Section 14 of EditorDesignDoc. */
	TSharedPtr<SComboButton> LSShotsCombo;

	/** "Recent" dropdown - module-lifetime history of the last 20 hosts the
	 * Shot Editor was bound to. Always visible (greyed out when history is
	 * empty). Backed by `FShotEditorHistory` singleton. */
	TSharedPtr<SComboButton> HistoryCombo;

	/** The 3D preview viewport. Forwarded SetActiveShot calls drive proxy
	 * rebuilds + solver-driven camera updates inside this widget. */
	TSharedPtr<SShotEditorViewport> Viewport;

	/** Collapses the viewport-local command strip down to a small Tools
	 * button so it does not cover diagnostic HUD text. */
	bool bViewportToolbarCollapsed = false;

	/** Collapses the compact Quick strip above the full Details panel. */
	bool bQuickControlsCollapsed = true;

	/** Pending target mode requested while leaving Free mode. Applied only
	 * after the status-bar Save / Discard action is resolved. */
	EShotEditorMode PendingFreeExitMode {};
	bool bHasPendingFreeExitMode = false;
	EShotEditorReverseSolveStatus PendingFreeExitStatus {};

	/** Right-pane structure details view. Bound to the active Shot via
	 * FStructOnScope wrapping the raw `FComposableCameraShot*`
	 * inside the host UObject. The wrapper's `bOwnsMemory=false` because
	 * the host UObject owns the actual struct memory. NotifyHook (this
	 * widget) bridges edit events to host->Modify / PostEditChangeProperty
	 * so undo + host-level listeners stay in sync. */
	TSharedPtr<IStructureDetailsView> StructureDetailsView;

	/** Rebuilds the StructureDetailsView's data binding from the current
	 * ActiveShot + ActiveHost. Called from OnActiveShotChanged. Clears
	 * the panel when no Shot is bound. */
	void RefreshDetailsView();

	/** Update label text, preview proxies, and Details panel when ActiveShot
	 * or ActiveHost changes. */
	void OnActiveShotChanged();

	/** Build the right pane: optional quick controls above the full
	 * structure Details view. */
	TSharedRef<SWidget> BuildDetailsPane();

	/** Build the middle viewport pane plus its floating view-command strip. */
	TSharedRef<SWidget> BuildViewportPane();
	TSharedRef<SWidget> BuildViewportFloatingToolbar();
	EVisibility GetViewportToolbarControlsVisibility() const;
	FReply OnViewportToolbarToggleCollapsedClicked();
	void OnQuickControlsExpansionChanged(bool bExpanded);
	void LoadPersistedLayoutState();
	void SavePersistedLayoutState() const;

	/** Compact, experimental mirror of the most-used Shot fields. Writes
	 * through the same host transaction / PostEditChangeProperty path as
	 * Details commits. */
	TSharedRef<SWidget> BuildQuickControls();
	TSharedRef<SWidget> BuildQuickFloatControl(EQuickControlField Field,
		const FText& Label,
		const FText& ToolTip,
		float MinValue,
		float MaxValue);
	TOptional<float> GetQuickControlValue(EQuickControlField Field) const;
	bool IsQuickControlEnabled(EQuickControlField Field) const;
	void CommitQuickControlValue(EQuickControlField Field, float NewValue);
	FProperty* ResolveActiveShotProperty() const;
	void PostActiveShotValueSet();

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

	/** Build the dropdown panel listing every Shot section in the active
	 * host's parent LevelSequence. The panel supports search, track grouping,
	 * current-shot marking, and time/row suffixes. Row clicks route through
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

	/** Apply a mode change. Shared between SSegmentedControl's OnValueChanged
	 * and the hotkey handler so both paths produce identical UX. No-op when
	 * Viewport is invalid or NewMode == current mode. */
	void TrySetMode(EShotEditorMode NewMode);
	void ClearPendingFreeExitMode();
	void QueueFreeExitStatus(EShotEditorMode NewMode);
	void ApplyPendingFreeExitMode(bool bSaveCurrentCamera);

	// Top bar commands and navigation
	//
	// Standard `FAssetEditorToolkit`-style chrome built inside the nomad
	// tab to give the Shot Editor the visual + functional feel of an asset
	// editor without inheriting the full toolkit (the Shot Editor's
	// single-instance + multi-host-type model doesn't fit `FBaseAssetToolkit`
	// - see EditorDesignDoc Section 23.1). The toolbar walks the active host's
	// outer chain to act on its containing asset / package.

	/** Build the single-row top bar: asset commands, active host breadcrumb,
	 * Sequencer Shot dropdown, recents, and viewport mode selector. */
	TSharedRef<class SWidget> BuildHeaderArea();
	TSharedRef<class SWidget> BuildTopBar();
	TSharedRef<class SWidget> BuildStatusBar();
	EVisibility GetStatusBarVisibility() const;
	EVisibility GetStatusBarFreeExitActionsVisibility() const;
	FSlateColor GetStatusBarBackgroundColor() const;
	FSlateColor GetStatusBarTextColor() const;
	FText GetStatusBarText() const;
	FText GetFreeExitStatusSaveTooltip() const;
	bool CanSaveFreeExitStatus() const;
	FReply OnFreeExitStatusSaveClicked();
	FReply OnFreeExitStatusDiscardClicked();
	FReply OnFreeExitStatusStayClicked();

	/** Build the compact asset command group (Save / Browse to Asset /
	 * Refresh). Wrapped in `FToolBarBuilder` so the command
	 * styling stays close to standard engine asset-editor chrome. */
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

	FReply OnResetViewportCameraClicked();
	bool CanResetViewportCamera() const;

	void OnCopyViewportCameraTransformClicked();
	bool CanCopyViewportCameraTransform() const;

	ECheckBoxState GetViewportDiagnosticHudCheckState() const;
	void OnViewportDiagnosticHudToggled(ECheckBoxState NewState);
	bool CanToggleViewportDiagnosticHud() const;

	ECheckBoxState GetViewportCompositionGuidesCheckState() const;
	void OnViewportCompositionGuidesToggled(ECheckBoxState NewState);
	bool CanToggleViewportCompositionGuides() const;

};
