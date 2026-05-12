// Copyright Sulley. All Rights Reserved.

#include "Widgets/SShotEditorRoot.h"

#include "DataAssets/ComposableCameraShot.h"
#include "DataAssets/ComposableCameraShotAsset.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Editor.h"
#include "Editors/ComposableCameraShotEditor.h"
#include "Editors/ComposableCameraShotEditorHistory.h"
#include "FileHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IDetailsView.h"
#include "IStructureDetailsView.h"
#include "LevelSequence.h"
#include "Modules/ModuleManager.h"
#include "MovieScene.h"
#include "MovieScene/MovieSceneComposableCameraShotSection.h"
#include "MovieSceneSection.h"
#include "MovieSceneTrack.h"
#include "MovieSceneBinding.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "Nodes/ComposableCameraCompositionFramingNode.h"
#include "PropertyEditorModule.h"
#include "UObject/Package.h"
#include "UObject/StructOnScope.h"
#include "UObject/UObjectGlobals.h"
#include "Misc/MessageDialog.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SShotEditorViewport.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SShotEditorRoot"

/**
 * One row's payload in the Shot outliner (Polish E.4).
 *
 * Pre-formatted display strings rather than re-running ResolveShotSectionTitle
 * / BuildSectionTimeRowSuffix every paint — Slate ticks rows on every frame
 * regardless of changes, and section title resolution does scope walks.
 *
 * Defined here (rather than in the header) because it's a private detail of
 * the Shot Editor's left pane — no other widget in the editor module needs
 * to construct or read these.
 */
struct FShotEditorListEntry
{
	TWeakObjectPtr<UMovieSceneComposableCameraShotSection> Section;
	FString TrackLabel;
	FString TitleLabel;
	FString TimeRowSuffix;
};

void SShotEditorRoot::Construct(const FArguments& /*InArgs*/)
{
	// Build the structure details view BEFORE the Slate tree — its widget
	// becomes the right-pane content. NotifyHook = this widget so each
	// property edit on the Shot routes through our NotifyPreChange /
	// NotifyPostChange to the host UObject (Modify + PostEditChangeProperty).
	{
		FPropertyEditorModule& PropertyModule =
			FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

		FDetailsViewArgs DetailsArgs;
		DetailsArgs.bAllowSearch       = true;
		DetailsArgs.bShowOptions       = false;   // hide "Filter" / "Diff" UI clutter — single-Shot context
		DetailsArgs.bShowScrollBar     = true;
		DetailsArgs.bUpdatesFromSelection = false;
		DetailsArgs.NotifyHook         = this;
		DetailsArgs.NameAreaSettings   = FDetailsViewArgs::HideNameArea;
		DetailsArgs.bAllowFavoriteSystem = false;

		FStructureDetailsViewArgs StructArgs;
		StructArgs.bShowAssets   = true;
		StructArgs.bShowClasses  = true;
		StructArgs.bShowInterfaces = true;
		StructArgs.bShowObjects  = true;

		StructureDetailsView = PropertyModule.CreateStructureDetailView(
			DetailsArgs, StructArgs, /*StructData=*/nullptr);
	}

	ChildSlot
	[
		SNew(SVerticalBox)

		// Asset toolbar — Save / Browse to Asset / Refresh. Modeled on the
		// FAssetEditorToolkit standard chrome so the Shot Editor reads as
		// a regular UE asset editor despite living inside a nomad tab.
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			BuildAssetToolbar()
		]

		// Header bar: host context chain ("LS → Track → Section" or
		// "Asset → Node") on the left, "Shots in Sequence" + "Recent"
		// dropdowns next to it, 3-state mode segmented control on the right.
		// See EShotEditorMode in SShotEditorViewport.h for per-mode semantics
		// and §23.12 of EditorDesignDoc for the dropdown breakdown.
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(6.f, 4.f)
		[
			SNew(SBorder)
			.BorderBackgroundColor(FLinearColor(0.10f, 0.10f, 0.12f, 1.f))
			.Padding(8.f, 4.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					SAssignNew(HostNameLabel, STextBlock)
					.Text(BuildHostContextChain())
					.ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.95f, 1.f))
				]

				// "Shots" — dropdown of all Shot sections in the active
				// host's parent LevelSequence. Hidden in non-LS contexts.
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(8.f, 0.f, 0.f, 0.f)
				[
					SAssignNew(LSShotsCombo, SComboButton)
					.Visibility(this, &SShotEditorRoot::GetLSShotsComboVisibility)
					.ToolTipText(LOCTEXT("LSShotsTooltip",
						"Jump to another Shot section in this LevelSequence. "
						"Only shown when the active Shot's host is a "
						"Sequencer Section — for CameraTypeAsset / "
						"standalone ShotAsset hosts there's no sibling list."))
					.OnGetMenuContent(this, &SShotEditorRoot::BuildLSShotsMenu)
					.ButtonContent()
					[
						SNew(STextBlock).Text(LOCTEXT("LSShotsLabel", "Shots"))
					]
				]

				// "Recent" — last 20 hosts the editor was bound to (most
				// recent first). Backed by FShotEditorHistory.
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4.f, 0.f, 0.f, 0.f)
				[
					SAssignNew(HistoryCombo, SComboButton)
					.ToolTipText(LOCTEXT("HistoryTooltip",
						"Reopen a recently edited Shot. History is in-memory "
						"for the current editor session (last 20 entries)."))
					.OnGetMenuContent(this, &SShotEditorRoot::BuildHistoryMenu)
					.ButtonContent()
					[
						SNew(STextBlock).Text(LOCTEXT("HistoryLabel", "Recent"))
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(8.f, 0.f, 0.f, 0.f)
				[
					SNew(SSegmentedControl<EShotEditorMode>)
					.Value_Lambda([this]() -> EShotEditorMode
					{
						return Viewport.IsValid()
							? Viewport->GetMode()
							: EShotEditorMode::Drag;
					})
					.OnValueChanged_Lambda([this](EShotEditorMode NewMode)
					{
						TrySetMode(NewMode);
					})
					+ SSegmentedControl<EShotEditorMode>::Slot(EShotEditorMode::Drag)
						.Text(LOCTEXT("ModeDrag", "Drag"))
						.ToolTip(LOCTEXT("ModeDragTip",
							"Drag mode (default): solver drives the camera. "
							"LMB-drag the on-screen handles to author the "
							"Placement / Aim anchor screen positions "
							"(yellow = Placement, cyan = Aim). RMB on a handle "
							"opens a context menu to pick the underlying "
							"target's pivot bone in-viewport."))
					+ SSegmentedControl<EShotEditorMode>::Slot(EShotEditorMode::Free)
						.Text(LOCTEXT("ModeFree", "Free"))
						.ToolTip(LOCTEXT("ModeFreeTip",
							"Free mode: solver pauses, you have full mouse "
							"camera control (orbit / pan / dolly). Handles "
							"are still drawn but track the live projection "
							"of world anchor / target points; they are "
							"greyed out and not interactive. Switching back "
							"to Drag will pop a 'save current camera framing "
							"as Shot params?' dialog (Phase D.4.3)."))
					+ SSegmentedControl<EShotEditorMode>::Slot(EShotEditorMode::Lock)
						.Text(LOCTEXT("ModeLock", "Lock"))
						.ToolTip(LOCTEXT("ModeLockTip",
							"Lock mode: solver drives the camera (same as "
							"Drag) but ALL viewport input is consumed — no "
							"handle drag, no camera control, no scroll-zoom. "
							"Read-only preview state, useful for "
							"screenshots / demos / preventing accidental "
							"edits."))
				]
			]
		]

		// Body: 3-region horizontal splitter (Shot outliner / Viewport / Details).
		// SSplitter so the user can drag region widths. Polish E.4 wires
		// up the left outliner with an SListView of Shot sections in the
		// active LevelSequence — single-click swaps context.
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		.Padding(2.f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)

			+ SSplitter::Slot()
			.Value(0.18f)   // Shot outliner — narrow nav strip
			[
				BuildShotOutliner()
			]

			+ SSplitter::Slot()
			.Value(0.55f)   // Viewport — biggest region
			[
				SAssignNew(Viewport, SShotEditorViewport)
			]

			+ SSplitter::Slot()
			.Value(0.27f)   // Details — moderate width for property editor
			[
				StructureDetailsView->GetWidget().ToSharedRef()
			]
		]
	];

	// Initial outliner population — covers the case where Construct runs
	// before the first SetActiveShot call (host might be already-bound at
	// open-time via FComposableCameraShotEditor::OpenForShotSection's
	// pre-construction context-set).
	RefreshShotListItems();
}

void SShotEditorRoot::SetActiveShot(FComposableCameraShot* Shot, UObject* HostObject)
{
	ActiveShot = Shot;
	ActiveHost = HostObject;
	OnActiveShotChanged();

	// Push the new context onto the in-memory recents list. Skipped when no
	// host is bound (placeholder transitions shouldn't pollute the list).
	// `BuildHostContextChain` reads from ActiveShot/ActiveHost above so the
	// label snapshot reflects the post-swap state.
	if (HostObject)
	{
		FShotEditorHistory::Get().Push(HostObject, BuildHostContextChain());
	}
}

void SShotEditorRoot::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	// Liveness guard: if the host UObject went away (asset closed, GC swept),
	// drop the raw Shot pointer to avoid dangling-pointer reads in subsequent
	// frames + refresh the header label so the user sees the disconnected state.
	if (ActiveShot && !ActiveHost.IsValid())
	{
		ActiveShot = nullptr;
		OnActiveShotChanged();
	}

	// Shot outliner (E.4) refresh throttle — picks up external LS edits
	// (sections added / removed / reordered via Sequencer) on a 0.5s cadence
	// without polling every frame. Per-frame poll would be cheap (few
	// sections) but burns Slate Tick time the editor doesn't need to spend.
	ShotListRefreshAccum += InDeltaTime;
	if (ShotListRefreshAccum >= 0.5f)
	{
		ShotListRefreshAccum = 0.f;
		RefreshShotListItems();
	}
}

void SShotEditorRoot::OnActiveShotChanged()
{
	if (HostNameLabel.IsValid())
	{
		HostNameLabel->SetText(BuildHostContextChain());
	}
	if (Viewport.IsValid())
	{
		Viewport->SetActiveShot(ActiveShot, ActiveHost.Get());
	}

	// Details panel: re-bind to the new Shot (or clear if no Shot bound).
	RefreshDetailsView();

	// Shot outliner (E.4): rebuild the list (LS may have changed) and
	// re-highlight the current entry.
	RefreshShotListItems();
}

void SShotEditorRoot::RefreshDetailsView()
{
	if (!StructureDetailsView.IsValid())
	{
		return;
	}

	if (ActiveShot && ActiveHost.IsValid())
	{
		// FStructOnScope wrapping a raw struct pointer inside a UObject —
		// the constructor `(UScriptStruct*, uint8*)` sets `OwnsMemory=false`
		// so the wrapper does NOT free the memory in its destructor (the
		// host UObject owns it). Liveness is guarded by ActiveHost weak
		// ref + Tick() — if the host is GC'd, Tick() clears ActiveShot
		// and re-triggers RefreshDetailsView with nullptr.
		TSharedPtr<FStructOnScope> StructData = MakeShared<FStructOnScope>(
			FComposableCameraShot::StaticStruct(),
			reinterpret_cast<uint8*>(ActiveShot));
		StructureDetailsView->SetStructureData(StructData);
	}
	else
	{
		StructureDetailsView->SetStructureData(nullptr);
	}
}

void SShotEditorRoot::NotifyPreChange(FProperty* /*PropertyAboutToChange*/)
{
	// Snapshot pre-edit state for undo via the bare-bones path —
	// SaveToTransactionBuffer records the snapshot without firing
	// FCoreUObjectDelegates::OnObjectModified or
	// UMovieSceneSignedObject::MarkAsChanged. Both of those are listened
	// to by Sequencer; their handlers invalidate evaluation caches and
	// re-spawn Spawnables, which arrives as a one-tick ref-pose flash on
	// any Spawnable bound to the section/host. Without this guard,
	// dragging a slider in the Details panel's struct view (Anchor Screen
	// Position, Distance, ManualFOV, etc.) blinks the preview character
	// to A-pose for every frame of the drag.
	if (UObject* Host = ActiveHost.Get())
	{
		SaveToTransactionBuffer(Host, /*bMarkDirty=*/false);
	}
}

void SShotEditorRoot::NotifyPostChange(
	const FPropertyChangedEvent& PropertyChangedEvent, FProperty* /*PropertyThatChanged*/)
{
	// Skip Interactive (per-frame slider drag) so Sequencer's eval cache
	// isn't invalidated mid-drag. The host's downstream listeners fire on
	// commit (ValueSet) — sufficient for graph-node refresh / Build /
	// runtime debug. Solver in the viewport reads ActiveShot directly each
	// tick so live drag visual feedback is unaffected.
	if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive)
	{
		return;
	}

	UObject* Host = ActiveHost.Get();
	if (!Host)
	{
		return;
	}
	FProperty* ShotProp = Host->GetClass()->FindPropertyByName(TEXT("Shot"));
	if (UMovieSceneComposableCameraShotSection* Section =
			Cast<UMovieSceneComposableCameraShotSection>(Host))
	{
		if (ActiveShot == &Section->InlineShot)
		{
			ShotProp = Section->GetClass()->FindPropertyByName(
				GET_MEMBER_NAME_CHECKED(UMovieSceneComposableCameraShotSection, InlineShot));
		}
		else if (ActiveShot == &Section->ShotOverrides)
		{
			ShotProp = Section->GetClass()->FindPropertyByName(
				GET_MEMBER_NAME_CHECKED(UMovieSceneComposableCameraShotSection, ShotOverrides));
		}
	}
	if (ShotProp)
	{
		FPropertyChangedEvent OuterEvent(ShotProp, PropertyChangedEvent.ChangeType);
		Host->PostEditChangeProperty(OuterEvent);
	}
}

namespace
{
	/** Section title resolver — mirrors `FComposableCameraShotSectionInterface::GetSectionTitle`'s
	 *  format ("Inline (N)" / asset name / "(no asset)") so the header label
	 *  reads identically to what Sequencer renders on the section. Replicated
	 *  here rather than going through the SectionInterface because (a) we
	 *  don't have a Painter context to construct one and (b) the label needs
	 *  to be cheap (called every context swap + every menu rebuild). */
	FString ResolveShotSectionTitle(const UMovieSceneComposableCameraShotSection& Section)
	{
		if (Section.Source == EComposableCameraShotSource::Inline)
		{
			return FString::Printf(TEXT("Inline (%d)"),
				Section.InlineShot.Targets.Num());
		}
		const FString AssetName = Section.ShotAssetRef.GetAssetName();
		return AssetName.IsEmpty() ? FString(TEXT("(no asset)")) : AssetName;
	}

	/** Walk a Section's outer chain to its parent LevelSequence. Returns
	 *  null when the chain is broken (Section orphaned mid-edit). */
	ULevelSequence* ResolveLevelSequenceForSection(const UMovieSceneSection* Section)
	{
		if (!Section)
		{
			return nullptr;
		}
		const UMovieScene* MovieScene = Section->GetTypedOuter<UMovieScene>();
		return MovieScene ? Cast<ULevelSequence>(MovieScene->GetOuter()) : nullptr;
	}

	/** Build the "(StartSec - EndSec, Row N)" disambiguation suffix used by
	 *  both the header context label and the LS-shots dropdown entries. The
	 *  suffix lets multiple same-titled sections (e.g. several Inline shots
	 *  on different rows / time windows in one LevelSequence) read
	 *  unambiguously in either surface. Falls back to "(Unbounded, Row N)"
	 *  for sections without closed bounds, or an empty string when the
	 *  parent MovieScene is unresolvable. */
	FString BuildSectionTimeRowSuffix(const UMovieSceneComposableCameraShotSection& Section)
	{
		const UMovieScene* MovieScene = Section.GetTypedOuter<UMovieScene>();
		if (!MovieScene)
		{
			return FString();
		}
		const FFrameRate TickResolution = MovieScene->GetTickResolution();
		const TRange<FFrameNumber> Range = Section.GetRange();
		if (Range.HasLowerBound() && Range.HasUpperBound())
		{
			const double StartSec = TickResolution.AsSeconds(
				FFrameTime(Range.GetLowerBoundValue()));
			const double EndSec   = TickResolution.AsSeconds(
				FFrameTime(Range.GetUpperBoundValue()));
			return FString::Printf(
				TEXT("(%.2fs - %.2fs, Row %d)"),
				StartSec, EndSec, Section.GetRowIndex());
		}
		return FString::Printf(
			TEXT("(Unbounded, Row %d)"), Section.GetRowIndex());
	}
}

FText SShotEditorRoot::BuildHostContextChain() const
{
	if (!ActiveShot)
	{
		return LOCTEXT("NoShotLoaded",
			"No Shot loaded — open a Camera Type Asset, select a CompositionFramingNode, and click the toolbar's 'Shot Editor' button.");
	}

	UObject* Host = ActiveHost.Get();
	if (!Host)
	{
		return LOCTEXT("ActiveHostStale",
			"Host destroyed — Shot context cleared. Reopen from a CompositionFramingNode.");
	}

	// Section host → "{LS} → {Track} → {Section}   (start - end, Row N)".
	// The trailing time/row suffix matches the LS-shots dropdown so the
	// header reads unambiguously when the LevelSequence holds multiple
	// same-titled sections, AND so the snapshot pushed to FShotEditorHistory
	// (which captures whatever this function returns) carries the
	// disambiguation through to the Recent menu as well.
	if (const UMovieSceneComposableCameraShotSection* Section =
			Cast<UMovieSceneComposableCameraShotSection>(Host))
	{
		const UMovieSceneTrack* Track = Section->GetTypedOuter<UMovieSceneTrack>();
		const ULevelSequence*   LS    = ResolveLevelSequenceForSection(Section);
		const FString LSName    = LS    ? LS->GetName()                          : FString(TEXT("?"));
		const FString TrackName = Track ? Track->GetDisplayName().ToString()    : FString(TEXT("?"));
		const FString SectionTitle = ResolveShotSectionTitle(*Section);
		const FString Suffix       = BuildSectionTimeRowSuffix(*Section);
		return FText::FromString(FString::Printf(
			TEXT("%s  →  %s  →  %s   %s"),
			*LSName, *TrackName, *SectionTitle, *Suffix));
	}

	// Standalone ShotAsset host → just the asset name (no parent chain
	// makes sense — the asset is the endpoint).
	if (const UComposableCameraShotAsset* ShotAsset = Cast<UComposableCameraShotAsset>(Host))
	{
		return FText::FromString(ShotAsset->GetName());
	}

	// Camera-graph node host (CompositionFramingNode in V1, possibly other
	// solver-aware nodes in the future) → "{TypeAsset} → {Node}".
	if (const UComposableCameraCameraNodeBase* Node =
			Cast<UComposableCameraCameraNodeBase>(Host))
	{
		const UComposableCameraTypeAsset* TypeAsset =
			Node->GetTypedOuter<UComposableCameraTypeAsset>();
		const FString AssetName = TypeAsset
			? TypeAsset->GetName()
			: Node->GetOutermost()->GetName();
		return FText::FromString(FString::Printf(
			TEXT("%s  →  %s"), *AssetName, *Node->GetName()));
	}

	// Unknown host shape — fall back to the V1.x identity format so the
	// designer at least sees what's bound rather than a blank label.
	return FText::Format(
		LOCTEXT("ActiveHostFmt", "{0}  ({1})"),
		FText::FromString(Host->GetName()),
		FText::FromString(Host->GetClass()->GetName()));
}

EVisibility SShotEditorRoot::GetLSShotsComboVisibility() const
{
	// Only meaningful when the host is a Sequencer Shot Section — for
	// CameraTypeAsset / standalone-ShotAsset hosts there's no sibling list
	// to enumerate. Collapsed (not Hidden) so the layout reclaims the space.
	return Cast<UMovieSceneComposableCameraShotSection>(ActiveHost.Get())
		? EVisibility::Visible
		: EVisibility::Collapsed;
}

TSharedRef<SWidget> SShotEditorRoot::BuildLSShotsMenu()
{
	FMenuBuilder MenuBuilder(/*bShouldCloseAfterSelect=*/true, /*CommandList=*/nullptr);

	UMovieSceneComposableCameraShotSection* CurrentSection =
		Cast<UMovieSceneComposableCameraShotSection>(ActiveHost.Get());
	const ULevelSequence* LS = ResolveLevelSequenceForSection(CurrentSection);
	const UMovieScene*    MovieScene = LS ? LS->GetMovieScene() : nullptr;
	if (!MovieScene)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("LSShotsNoLS", "No LevelSequence resolved."),
			FText::GetEmpty(), FSlateIcon(), FUIAction(),
			NAME_None, EUserInterfaceActionType::None);
		return MenuBuilder.MakeWidget();
	}

	// Walk every Shot section in the MovieScene. Shot tracks live under
	// object bindings (FMovieSceneBinding) since `FComposableCameraShotTrackEditor`
	// extends `BuildObjectBindingTrackMenu`, but we also scan root tracks
	// defensively so future-routing changes don't silently empty the menu.
	int32 EntryCount = 0;
	auto AddShotEntry =
		[this, &MenuBuilder, &EntryCount, CurrentSection](
			UMovieSceneComposableCameraShotSection* Section,
			const FString& TrackLabel)
	{
		if (!Section)
		{
			return;
		}
		const FString Title       = ResolveShotSectionTitle(*Section);
		const FString RangeSuffix = BuildSectionTimeRowSuffix(*Section);
		const bool    bIsCurrent  = (Section == CurrentSection);

		// "Current - " prefix marks the section currently bound to the Shot
		// Editor. Plain ASCII so it renders consistently across menu themes.
		const FText EntryText = FText::FromString(FString::Printf(
			TEXT("%s%s / %s   %s"),
			bIsCurrent ? TEXT("Current - ") : TEXT(""),
			*TrackLabel, *Title, *RangeSuffix));

		const TWeakObjectPtr<UMovieSceneComposableCameraShotSection> WeakSection(Section);
		MenuBuilder.AddMenuEntry(
			EntryText,
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([WeakSection]()
			{
				if (UMovieSceneComposableCameraShotSection* Live = WeakSection.Get())
				{
					FComposableCameraShotEditor::OpenForShotSection(Live);
				}
			})));
		++EntryCount;
	};

	for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
	{
		for (UMovieSceneTrack* Track : Binding.GetTracks())
		{
			if (!Track)
			{
				continue;
			}
			const FString TrackLabel = Track->GetDisplayName().ToString();
			for (UMovieSceneSection* Sec : Track->GetAllSections())
			{
				AddShotEntry(
					Cast<UMovieSceneComposableCameraShotSection>(Sec),
					TrackLabel);
			}
		}
	}

	// Defensive scan of root-level tracks. Currently no Shot tracks live
	// here, but covering this branch keeps the menu honest if track routing
	// ever changes.
	for (UMovieSceneTrack* Track : MovieScene->GetTracks())
	{
		if (!Track)
		{
			continue;
		}
		const FString TrackLabel = Track->GetDisplayName().ToString();
		for (UMovieSceneSection* Sec : Track->GetAllSections())
		{
			AddShotEntry(
				Cast<UMovieSceneComposableCameraShotSection>(Sec),
				TrackLabel);
		}
	}

	if (EntryCount == 0)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("LSShotsEmpty", "No Shot sections in this LevelSequence."),
			FText::GetEmpty(), FSlateIcon(), FUIAction(),
			NAME_None, EUserInterfaceActionType::None);
	}

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SShotEditorRoot::BuildHistoryMenu()
{
	FMenuBuilder MenuBuilder(/*bShouldCloseAfterSelect=*/true, /*CommandList=*/nullptr);

	const TArray<FShotEditorHistoryEntry>& Entries =
		FShotEditorHistory::Get().GetEntries();
	if (Entries.IsEmpty())
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("HistoryEmpty", "History is empty."),
			FText::GetEmpty(), FSlateIcon(), FUIAction(),
			NAME_None, EUserInterfaceActionType::None);
		return MenuBuilder.MakeWidget();
	}

	for (const FShotEditorHistoryEntry& Entry : Entries)
	{
		const TWeakObjectPtr<UObject> WeakHost = Entry.Host;
		const FSoftObjectPath         HostPath = Entry.HostPath;
		const bool bAlive       = WeakHost.IsValid();
		const bool bResolvable  = bAlive || HostPath.IsValid();

		// Tooltip distinguishes three states: live (in-session), persisted
		// (live ref cold but soft path on disk — clicking will load the
		// asset), and unrecoverable (no path captured, e.g. a host whose
		// soft path failed to round-trip through the ini).
		const FText Tooltip = bAlive
			? LOCTEXT("HistoryAliveTip",
				"Click to reopen this Shot.")
			: (HostPath.IsValid()
				? LOCTEXT("HistoryReloadTip",
					"Click to reopen this Shot. The host asset will be loaded if it is not already in memory.")
				: LOCTEXT("HistoryUnrecoverableTip",
					"This entry is unrecoverable; the captured object path is empty."));

		MenuBuilder.AddMenuEntry(
			Entry.DisplayLabel,
			Tooltip,
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([WeakHost, HostPath]()
				{
					// Re-resolve through the singleton so the live-then-
					// path fallback stays single-source.
					FShotEditorHistoryEntry Snapshot;
					Snapshot.Host     = WeakHost;
					Snapshot.HostPath = HostPath;
					UObject* Host = FShotEditorHistory::ResolveHost(Snapshot);
					if (!Host)
					{
						return;
					}
					// Section host runs through OpenForShotSection so the
					// per-section Inline / AssetReference resolution stays
					// in one place; other host shapes resolve their own
					// `Shot` UPROPERTY directly.
					if (UMovieSceneComposableCameraShotSection* Section =
							Cast<UMovieSceneComposableCameraShotSection>(Host))
					{
						FComposableCameraShotEditor::OpenForShotSection(Section);
						return;
					}
					FComposableCameraShot* Shot = nullptr;
					if (UComposableCameraShotAsset* Asset =
							Cast<UComposableCameraShotAsset>(Host))
					{
						Shot = &Asset->Shot;
					}
					else if (UComposableCameraCompositionFramingNode* Composition =
							Cast<UComposableCameraCompositionFramingNode>(Host))
					{
						Shot = &Composition->Shot;
					}
					FComposableCameraShotEditor::OpenForShot(Shot, Host);
				}),
				FCanExecuteAction::CreateLambda([bResolvable]()
				{
					return bResolvable;
				})));
	}

	return MenuBuilder.MakeWidget();
}

void SShotEditorRoot::TrySetMode(EShotEditorMode NewMode)
{
	if (!Viewport.IsValid())
	{
		return;
	}
	const EShotEditorMode OldMode = Viewport->GetMode();
	if (OldMode == NewMode)
	{
		return;
	}

	// Leaving Free mode (to Drag OR Lock) pops a "save current camera
	// framing?" dialog — Free is the only mode where the user has authored a
	// camera pose that diverges from the solver's output, so both other
	// modes need to ask before discarding the user's work.
	if (OldMode == EShotEditorMode::Free
		&& NewMode != EShotEditorMode::Free)
	{
		const EShotEditorReverseSolveStatus Status = Viewport->DiagnoseReverseSolveCurrentCamera();
		const bool bCanSolve = (Status == EShotEditorReverseSolveStatus::Ok);
		const FText Title = LOCTEXT("ReverseSolveTitle",
			"Save camera framing?");
		// Failure body weaves the status reason into the message so
		// designers see *why* Save is unavailable (placement anchor
		// missing, anchor behind camera, etc.) instead of a generic
		// "no resolvable Anchor" string. Reasons live in
		// `ShotEditorReverseSolveStatusToText` so the cpp / UI strings
		// stay in one place.
		const FText Body = bCanSolve
			? LOCTEXT("ReverseSolveBody",
				"You moved the camera in Free mode.\n\n"
				"Yes: save the new framing.\n"
				"No: discard (camera snaps back).\n"
				"Cancel: stay in Free.")
			: FText::Format(LOCTEXT("ReverseSolveBodyUnavailable",
				"You moved the camera in Free mode, but Save is unavailable.\n"
				"Reason: {0}\n\n"
				"Yes/No: discard (camera snaps back).\n"
				"Cancel: stay in Free."),
				ShotEditorReverseSolveStatusToText(Status));
		const EAppReturnType::Type Choice =
			FMessageDialog::Open(EAppMsgType::YesNoCancel, Body, Title);

		if (Choice == EAppReturnType::Cancel)
		{
			return;   // stay in Free
		}
		if (Choice == EAppReturnType::Yes && bCanSolve)
		{
			Viewport->ReverseSolveCurrentCameraToShot();
		}
		// "No" or "Yes-but-can't-solve" both fall through: switch to
		// NewMode, solver re-asserts authored pose on next tick.
	}

	Viewport->SetMode(NewMode);
}

FReply SShotEditorRoot::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if ((InKeyEvent.IsControlDown() || InKeyEvent.IsCommandDown())
		&& InKeyEvent.IsAltDown()
		&& !InKeyEvent.IsShiftDown()
		&& InKeyEvent.GetKey() == EKeys::C
		&& CanCopyViewportCameraTransform())
	{
		OnCopyViewportCameraTransformClicked();
		return FReply::Handled();
	}

	if ((InKeyEvent.IsControlDown() || InKeyEvent.IsCommandDown())
		&& !InKeyEvent.IsShiftDown()
		&& !InKeyEvent.IsAltDown()
		&& InKeyEvent.GetKey() == EKeys::B
		&& CanBrowse())
	{
		OnBrowseClicked();
		return FReply::Handled();
	}

	// Only fire on plain key presses (no modifiers) — leaves Ctrl+1 / Alt+2
	// etc. free for stock editor shortcuts.
	if (InKeyEvent.IsControlDown() || InKeyEvent.IsShiftDown()
		|| InKeyEvent.IsAltDown() || InKeyEvent.IsCommandDown())
	{
		return FReply::Unhandled();
	}

	const FKey Key = InKeyEvent.GetKey();
	const bool bHandled =
		(Key == EKeys::One   && (TrySetMode(EShotEditorMode::Drag),  true)) ||
		(Key == EKeys::Two   && (TrySetMode(EShotEditorMode::Free),  true)) ||
		(Key == EKeys::Three && (TrySetMode(EShotEditorMode::Lock),  true));

	if (bHandled)
	{
		// Restore keyboard focus to this root widget. TrySetMode may pop a
		// modal FMessageDialog (Free→Drag/Lock reverse-solve prompt); when
		// the dialog closes Slate doesn't auto-restore focus, so subsequent
		// hotkeys would fall on deaf ears until the user clicks back into
		// the editor. Force-routing focus back here makes hotkeys feel
		// continuous.
		FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::SetDirectly);
		return FReply::Handled();
	}

	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

// ─── Asset toolbar ───────────────────────────────────────────────────────

namespace
{
	/** Walk the host's outer chain to find the asset (the UObject whose
	 *  outer is the package). Returns null when the host is null or
	 *  somehow lacks a package — neither path should happen in practice
	 *  but the null-guard keeps the toolbar safe under teardown races. */
	UObject* ResolveOutermostAsset(UObject* Host)
	{
		if (!Host)
		{
			return nullptr;
		}
		UObject* Asset = Host;
		while (UObject* Outer = Asset->GetOuter())
		{
			if (Outer->IsA<UPackage>())
			{
				break;
			}
			Asset = Outer;
		}
		// Asset is now the topmost UObject below the package (the "primary
		// asset"). For a CompositionFramingNode host, that's the parent
		// CameraTypeAsset; for a Section host, that's the LevelSequence;
		// for a ShotAsset host, that's the ShotAsset itself.
		return (Asset && Asset->GetOutermost() != Asset) ? Asset : nullptr;
	}
}

TSharedRef<SWidget> SShotEditorRoot::BuildAssetToolbar()
{
	FToolBarBuilder ToolbarBuilder(/*InCommandList=*/nullptr,
		FMultiBoxCustomization::None);
	ToolbarBuilder.SetStyle(&FAppStyle::Get(), "AssetEditorToolbar");

	ToolbarBuilder.BeginSection("Asset");
	{
		// Use the modern `Icons.*` style names — they're registered as
		// 20x20 SVG brushes engine-wide, so Save / Browse / Refresh end up
		// the same size. The legacy `AssetEditor.SaveAsset` brush is a
		// 40x40 PNG meant for the big asset-editor toolbar, while
		// `SystemWideCommands.FindInContentBrowser` is a 20x20 — mixing
		// them gave inconsistent button sizes.
		ToolbarBuilder.AddToolBarButton(
			FUIAction(
				FExecuteAction::CreateSP(this, &SShotEditorRoot::OnSaveClicked),
				FCanExecuteAction::CreateSP(this, &SShotEditorRoot::CanSave)),
			NAME_None,
			LOCTEXT("ToolbarSave", "Save"),
			LOCTEXT("ToolbarSaveTooltip",
				"Save the asset that contains the active Shot's host. For a "
				"CompositionFramingNode host this is the parent CameraTypeAsset; "
				"for a Sequencer Section host it's the LevelSequence; for a "
				"ShotAsset host it's the asset itself."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Save"));

		ToolbarBuilder.AddToolBarButton(
			FUIAction(
				FExecuteAction::CreateSP(this, &SShotEditorRoot::OnBrowseClicked),
				FCanExecuteAction::CreateSP(this, &SShotEditorRoot::CanBrowse)),
			NAME_None,
			LOCTEXT("ToolbarBrowse", "Browse"),
			LOCTEXT("ToolbarBrowseTooltip",
				"Show the active host's containing asset in the Content Browser."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.BrowseContent"));
	}
	ToolbarBuilder.EndSection();

	ToolbarBuilder.BeginSection("View");
	{
		ToolbarBuilder.AddToolBarButton(
			FUIAction(
				FExecuteAction::CreateSP(this, &SShotEditorRoot::OnCopyViewportCameraTransformClicked),
				FCanExecuteAction::CreateSP(this, &SShotEditorRoot::CanCopyViewportCameraTransform)),
			NAME_None,
			LOCTEXT("ToolbarCopyViewportCameraTransform", "Copy Camera"),
			LOCTEXT("ToolbarCopyViewportCameraTransformTooltip",
				"Copy the Shot Editor viewport camera transform as pasteable FTransform text."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Copy"));

		ToolbarBuilder.AddToolBarButton(
			FUIAction(
				FExecuteAction::CreateSP(this, &SShotEditorRoot::OnRefreshClicked)),
			NAME_None,
			LOCTEXT("ToolbarRefresh", "Refresh"),
			LOCTEXT("ToolbarRefreshTooltip",
				"Rebuild the Details panel's binding to the active Shot. "
				"Useful when a host-level edit didn't propagate cleanly."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Refresh"));
	}
	ToolbarBuilder.EndSection();

	return ToolbarBuilder.MakeWidget();
}

void SShotEditorRoot::OnSaveClicked()
{
	UObject* Host = ActiveHost.Get();
	if (!Host)
	{
		return;
	}
	UPackage* Pkg = Host->GetOutermost();
	if (!Pkg)
	{
		return;
	}
	// Standard editor save path — runs source-control checkout / dirty
	// dialog as needed. `bCheckDirty=false` so we save unconditionally
	// (designer hit Save explicitly).
	TArray<UPackage*> Packages = { Pkg };
	FEditorFileUtils::PromptForCheckoutAndSave(
		Packages, /*bCheckDirty=*/false, /*bPromptToSave=*/false);
}

bool SShotEditorRoot::CanSave() const
{
	return ActiveHost.IsValid();
}

void SShotEditorRoot::OnBrowseClicked()
{
	UObject* Asset = ResolveOutermostAsset(ActiveHost.Get());
	if (!Asset || !GEditor)
	{
		return;
	}
	GEditor->SyncBrowserToObjects(TArray<UObject*>{ Asset });
}

bool SShotEditorRoot::CanBrowse() const
{
	return ResolveOutermostAsset(ActiveHost.Get()) != nullptr;
}

void SShotEditorRoot::OnRefreshClicked()
{
	RefreshDetailsView();
}

void SShotEditorRoot::OnCopyViewportCameraTransformClicked()
{
	if (Viewport.IsValid())
	{
		Viewport->CopyCurrentCameraTransformToClipboard();
	}
}

bool SShotEditorRoot::CanCopyViewportCameraTransform() const
{
	return Viewport.IsValid();
}

// ─── Shot outliner (Polish E.4) ──────────────────────────────────────────

TSharedRef<SWidget> SShotEditorRoot::BuildShotOutliner()
{
	return SNew(SBorder)
		.BorderBackgroundColor(FLinearColor(0.10f, 0.10f, 0.12f, 1.f))
		.Padding(4.f)
		[
			SNew(SVerticalBox)

			// Section heading.
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4.f, 4.f, 4.f, 6.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ShotOutlinerHeader", "Shots in Sequence"))
				.ColorAndOpacity(FLinearColor(0.85f, 0.85f, 0.9f, 1.f))
			]

			// Scrollable list of Shot sections in the active LS.
			+ SVerticalBox::Slot()
			.FillHeight(1.f)
			[
				SAssignNew(ShotListView, SListView<TSharedPtr<FShotEditorListEntry>>)
					.ListItemsSource(&ShotListItems)
					.OnGenerateRow(this, &SShotEditorRoot::MakeShotListRow)
					.OnMouseButtonClick(this, &SShotEditorRoot::OnShotListMouseButtonClick)
					.SelectionMode(ESelectionMode::Single)
			]
		];
}

void SShotEditorRoot::RefreshShotListItems()
{
	// Walk the active LS for Shot sections — same enumeration as
	// `BuildLSShotsMenu`. Inlining the walk (rather than extracting a
	// shared helper) keeps the menu independent of the list's pre-cached
	// strings; if either grows divergent display rules later, neither
	// has to fight the other's contract.
	UMovieSceneComposableCameraShotSection* CurrentSection =
		Cast<UMovieSceneComposableCameraShotSection>(ActiveHost.Get());
	const ULevelSequence* LS = ResolveLevelSequenceForSection(CurrentSection);
	const UMovieScene*    MS = LS ? LS->GetMovieScene() : nullptr;

	TArray<TSharedPtr<FShotEditorListEntry>> NewItems;

	if (MS)
	{
		auto AddEntry =
			[&NewItems](
				UMovieSceneComposableCameraShotSection* Section,
				const FString& TrackLabel)
		{
			if (!Section)
			{
				return;
			}
			TSharedPtr<FShotEditorListEntry> Entry =
				MakeShared<FShotEditorListEntry>();
			Entry->Section       = Section;
			Entry->TrackLabel    = TrackLabel;
			Entry->TitleLabel    = ResolveShotSectionTitle(*Section);
			Entry->TimeRowSuffix = BuildSectionTimeRowSuffix(*Section);
			NewItems.Add(Entry);
		};

		for (const FMovieSceneBinding& Binding : MS->GetBindings())
		{
			for (UMovieSceneTrack* Track : Binding.GetTracks())
			{
				if (!Track)
				{
					continue;
				}
				const FString TrackLabel = Track->GetDisplayName().ToString();
				for (UMovieSceneSection* Sec : Track->GetAllSections())
				{
					AddEntry(
						Cast<UMovieSceneComposableCameraShotSection>(Sec),
						TrackLabel);
				}
			}
		}
		for (UMovieSceneTrack* Track : MS->GetTracks())
		{
			if (!Track)
			{
				continue;
			}
			const FString TrackLabel = Track->GetDisplayName().ToString();
			for (UMovieSceneSection* Sec : Track->GetAllSections())
			{
				AddEntry(
					Cast<UMovieSceneComposableCameraShotSection>(Sec),
					TrackLabel);
			}
		}
	}

	// Diff before clobbering so the SListView doesn't refresh on every
	// 0.5s tick when nothing changed (avoids dropped hover state, scroll
	// position resets, and re-paints during otherwise-idle editor frames).
	auto SameEntry =
		[](const TSharedPtr<FShotEditorListEntry>& A,
		   const TSharedPtr<FShotEditorListEntry>& B) -> bool
	{
		return A.IsValid() && B.IsValid()
			&& A->Section == B->Section
			&& A->TrackLabel == B->TrackLabel
			&& A->TitleLabel == B->TitleLabel
			&& A->TimeRowSuffix == B->TimeRowSuffix;
	};
	bool bChanged = NewItems.Num() != ShotListItems.Num();
	if (!bChanged)
	{
		for (int32 i = 0; i < NewItems.Num(); ++i)
		{
			if (!SameEntry(NewItems[i], ShotListItems[i]))
			{
				bChanged = true;
				break;
			}
		}
	}
	if (bChanged)
	{
		ShotListItems = MoveTemp(NewItems);
		if (ShotListView.IsValid())
		{
			ShotListView->RequestListRefresh();
		}
	}

	// NOTE: this method intentionally DOES NOT manage SListView's selection
	// state. The visual "current Shot" indicator is the row's `▶ ` prefix +
	// gold tint (rendered via reactive `TAttribute` lambdas in
	// `MakeShotListRow`), which re-evaluate `Entry->Section == ActiveHost`
	// on every paint. SListView's user-click selection (the blue row
	// background) is left under Slate's natural control — clicking a row
	// selects it visually, no programmatic write fights the click commit.
	//
	// Earlier attempts to mirror "selected" to "current" via
	// `SetItemSelection` from this refresh produced an occasional one-
	// frame flicker on click: between the click's selection commit and
	// our subsequent programmatic Direct set, Slate could schedule an
	// intermediate paint that briefly showed the wrong combination.
	// Decoupling the two means selection (user intent indicator) and
	// `▶`/gold (current state indicator) are drawn from independent
	// sources — slight cosmetic drift is possible if the user clicks a
	// row but the swap fails (selection on B, `▶` on A), but the more
	// common cases (click succeeds, external swap) read cleanly.
}

TSharedRef<ITableRow> SShotEditorRoot::MakeShotListRow(
	TSharedPtr<FShotEditorListEntry> Entry,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	// Title + color are TAttribute lambdas that re-evaluate every paint
	// — `bIsCurrent` flips when the editor's `ActiveHost` swaps to/from
	// this row's Section, and we want the `▶` prefix + gold tint to track
	// that without forcing a full SListView rebuild. Reading from the
	// captured weak entry pointer (rather than the row index) keeps the
	// lambda valid across diff-skipped refreshes that preserve the same
	// `TSharedPtr<FShotEditorListEntry>` instances.
	TWeakPtr<FShotEditorListEntry> WeakEntry = Entry;
	auto IsCurrentLambda = [this, WeakEntry]() -> bool
	{
		TSharedPtr<FShotEditorListEntry> Pin = WeakEntry.Pin();
		return Pin.IsValid() && Pin->Section.Get() == ActiveHost.Get();
	};

	TAttribute<FText> TitleAttr = TAttribute<FText>::CreateLambda([WeakEntry, IsCurrentLambda]()
	{
		TSharedPtr<FShotEditorListEntry> Pin = WeakEntry.Pin();
		if (!Pin.IsValid()) { return FText::GetEmpty(); }
		return IsCurrentLambda()
			? FText::FromString(FString::Printf(TEXT("▶ %s"), *Pin->TitleLabel))
			: FText::FromString(Pin->TitleLabel);
	});

	TAttribute<FSlateColor> TitleColorAttr =
		TAttribute<FSlateColor>::CreateLambda([IsCurrentLambda]() -> FSlateColor
	{
		return IsCurrentLambda()
			? FSlateColor(FLinearColor(1.f, 0.95f, 0.55f, 1.f))   // gold for current
			: FSlateColor(FLinearColor(0.92f, 0.92f, 0.95f, 1.f));
	});

	TAttribute<FText> MetaAttr = TAttribute<FText>::CreateLambda([WeakEntry]()
	{
		TSharedPtr<FShotEditorListEntry> Pin = WeakEntry.Pin();
		if (!Pin.IsValid()) { return FText::GetEmpty(); }
		return FText::FromString(FString::Printf(
			TEXT("%s   %s"), *Pin->TrackLabel, *Pin->TimeRowSuffix));
	});

	return SNew(STableRow<TSharedPtr<FShotEditorListEntry>>, OwnerTable)
		.Padding(FMargin(4.f, 3.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(TitleAttr)
				.ColorAndOpacity(TitleColorAttr)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 1.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Text(MetaAttr)
				.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.6f, 1.f)))
			]
		];
}

void SShotEditorRoot::OnShotListMouseButtonClick(
	TSharedPtr<FShotEditorListEntry> ClickedEntry)
{
	// Direct mouse-click handler — `SListView::OnMouseButtonClick` fires
	// on every item click independently of `OnSelectionChanged`, so the
	// swap routing never fights Slate's selection state machine. Earlier
	// approaches went through `OnSelectionChanged` and raced with
	// `Private_SetItemSelection` from the user click + our programmatic
	// selection sync, which produced two-rows-highlighted flicker and
	// snap-back-to-current visuals. With this handler, the swap is the
	// only authoritative path; selection state is purely cosmetic and
	// updated post-swap via `RefreshShotListItems`.
	if (!ClickedEntry.IsValid())
	{
		return;
	}
	UMovieSceneComposableCameraShotSection* Target = ClickedEntry->Section.Get();
	if (!Target || Target == ActiveHost.Get())
	{
		return;
	}
	FComposableCameraShotEditor::OpenForShotSection(Target);
}

#undef LOCTEXT_NAMESPACE
