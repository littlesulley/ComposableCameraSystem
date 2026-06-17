// Copyright 2026 Sulley. All Rights Reserved.

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
#include "Misc/ConfigCacheIni.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "Nodes/ComposableCameraCompositionFramingNode.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/StructOnScope.h"
#include "UObject/UObjectGlobals.h"
#include "Styling/AppStyle.h"
#include "Widgets/ComposableCameraShotEditorLayoutState.h"
#include "Widgets/ComposableCameraShotEditorModeSwitchUtils.h"
#include "Widgets/ComposableCameraShotMenuUtils.h"
#include "Widgets/ComposableCameraShotEditorStatusBarUtils.h"
#include "Widgets/ComposableCameraShotViewportToolbarUtils.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SShotEditorViewport.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SShotEditorRoot"

namespace
{
	constexpr const TCHAR* kShotEditorLayoutConfigSection =
		TEXT("ComposableCameraSystem.ShotEditorLayout");
	constexpr const TCHAR* kViewportToolbarCollapsedKey =
		TEXT("ViewportToolbarCollapsed");
	constexpr const TCHAR* kQuickControlsCollapsedKey =
		TEXT("QuickControlsCollapsed");
}

void SShotEditorRoot::Construct(const FArguments& /*InArgs*/)
{
	// Build the structure details view BEFORE the Slate tree - its widget
	// becomes the right-pane content. NotifyHook = this widget so each
	// property edit on the Shot routes through our NotifyPreChange /
	// NotifyPostChange to the host UObject (Modify + PostEditChangeProperty).
	{
		FPropertyEditorModule& PropertyModule =
			FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

		FDetailsViewArgs DetailsArgs;
		DetailsArgs.bAllowSearch = true;
		DetailsArgs.bShowOptions = false; // hide "Filter" / "Diff" UI clutter - single-Shot context
		DetailsArgs.bShowScrollBar = true;
		DetailsArgs.bUpdatesFromSelection = false;
		DetailsArgs.NotifyHook = this;
		DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
		DetailsArgs.bAllowFavoriteSystem = false;

		FStructureDetailsViewArgs StructArgs;
		StructArgs.bShowAssets = true;
		StructArgs.bShowClasses = true;
		StructArgs.bShowInterfaces = true;
		StructArgs.bShowObjects = true;

		StructureDetailsView = PropertyModule.CreateStructureDetailView(DetailsArgs, StructArgs, /*StructData=*/nullptr);
	}

	LoadPersistedLayoutState();

	ChildSlot
	[SNew(SVerticalBox)

		// Top bar: asset commands, host breadcrumb, Shot navigation dropdown,
		// recents, and the viewport mode selector in one row.
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.f, 2.f)
		[BuildHeaderArea()]

		// Body: 2-region horizontal splitter (Viewport / Details). Shot
		// navigation lives in the top bar's "Shots" dropdown so the viewport
		// keeps the space formerly used by the left outliner.
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		.Padding(2.f)
		[SNew(SSplitter)
			.Orientation(Orient_Horizontal)

			+ SSplitter::Slot()
			.Value(0.72f) // Viewport - primary authoring surface
			[BuildViewportPane()]

			+ SSplitter::Slot()
			.Value(0.28f) // Details - moderate width for property editor
			[BuildDetailsPane()]]];
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
}

void SShotEditorRoot::OnActiveShotChanged()
{
	ClearPendingFreeExitMode();

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
}

void SShotEditorRoot::RefreshDetailsView()
{
	if (!StructureDetailsView.IsValid())
	{
		return;
	}

	if (ActiveShot && ActiveHost.IsValid())
	{
		// FStructOnScope wrapping a raw struct pointer inside a UObject - 
		// the constructor `(UScriptStruct*, uint8*)` sets `OwnsMemory=false`
		// so the wrapper does NOT free the memory in its destructor (the
		// host UObject owns it). Liveness is guarded by ActiveHost weak
		// ref + Tick() - if the host is GC'd, Tick() clears ActiveShot
		// and re-triggers RefreshDetailsView with nullptr.
		TSharedPtr<FStructOnScope> StructData = MakeShared<FStructOnScope>(FComposableCameraShot::StaticStruct(),
			reinterpret_cast<uint8*>(ActiveShot));
		StructureDetailsView->SetStructureData(StructData);
	}
	else
	{
		StructureDetailsView->SetStructureData(nullptr);
	}
}

TSharedRef<SWidget> SShotEditorRoot::BuildDetailsPane()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 0.f, 0.f, 2.f)
		[BuildQuickControls()]

		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		[StructureDetailsView->GetWidget().ToSharedRef()];
}

TSharedRef<SWidget> SShotEditorRoot::BuildViewportPane()
{
	return SNew(SOverlay)
		+ SOverlay::Slot()
		[SAssignNew(Viewport, SShotEditorViewport)]

		+ SOverlay::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Top)
		.Padding(8.f)
		[BuildViewportFloatingToolbar()];
}

TSharedRef<SWidget> SShotEditorRoot::BuildViewportFloatingToolbar()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(4.f, 3.f)
		[SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.f, 0.f, 4.f, 0.f)
			[SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.OnClicked(this, &SShotEditorRoot::OnViewportToolbarToggleCollapsedClicked)
				.ToolTipText(LOCTEXT("ViewportToolbarToggleTooltip",
					"Show or collapse viewport tools."))
				[SNew(STextBlock)
					.Text_Lambda([this]()
					{
						return bViewportToolbarCollapsed
							? LOCTEXT("ViewportToolbarExpand", "Tools +")
							: LOCTEXT("ViewportToolbarCollapse", "Tools -");
					})]]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[SNew(SHorizontalBox)
				.Visibility(this, &SShotEditorRoot::GetViewportToolbarControlsVisibility)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.f, 0.f, 4.f, 0.f)
				[SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.IsEnabled(this, &SShotEditorRoot::CanResetViewportCamera)
					.OnClicked(this, &SShotEditorRoot::OnResetViewportCameraClicked)
					.ToolTipText(LOCTEXT("ViewportToolbarResetTooltip",
						"Reset the Free camera back to the current solved Shot pose."))
					[SNew(STextBlock)
						.Text(LOCTEXT("ViewportToolbarReset", "Reset"))]]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.f, 0.f, 4.f, 0.f)
				[SNew(SCheckBox)
					.IsChecked(this, &SShotEditorRoot::GetViewportDiagnosticHudCheckState)
					.OnCheckStateChanged(this, &SShotEditorRoot::OnViewportDiagnosticHudToggled)
					.IsEnabled(this, &SShotEditorRoot::CanToggleViewportDiagnosticHud)
					.ToolTipText(LOCTEXT("ViewportToolbarHudTooltip",
						"Show or hide diagnostic HUD text."))
					[SNew(STextBlock)
						.Text(LOCTEXT("ViewportToolbarHud", "HUD"))]]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[SNew(SCheckBox)
					.IsChecked(this, &SShotEditorRoot::GetViewportCompositionGuidesCheckState)
					.OnCheckStateChanged(this, &SShotEditorRoot::OnViewportCompositionGuidesToggled)
					.IsEnabled(this, &SShotEditorRoot::CanToggleViewportCompositionGuides)
					.ToolTipText(LOCTEXT("ViewportToolbarGuidesTooltip",
						"Show or hide handles, framing zones, and bounds guides."))
					[SNew(STextBlock)
						.Text(LOCTEXT("ViewportToolbarGuides", "Guides"))]]]];
}

EVisibility SShotEditorRoot::GetViewportToolbarControlsVisibility() const
{
	using namespace ComposableCameraSystem::ShotEditorViewportToolbar;
	return ShouldShowToolbarExpandedControls(bViewportToolbarCollapsed)
		? EVisibility::Visible
		: EVisibility::Collapsed;
}

FReply SShotEditorRoot::OnViewportToolbarToggleCollapsedClicked()
{
	bViewportToolbarCollapsed = !bViewportToolbarCollapsed;
	SavePersistedLayoutState();
	return FReply::Handled();
}

void SShotEditorRoot::OnQuickControlsExpansionChanged(bool bExpanded)
{
	bQuickControlsCollapsed = !bExpanded;
	SavePersistedLayoutState();
}

void SShotEditorRoot::LoadPersistedLayoutState()
{
	using namespace ComposableCameraSystem::ShotEditorLayout;

	TOptional<bool> ViewportToolbarCollapsed;
	TOptional<bool> QuickControlsCollapsed;

	if (GConfig)
	{
		bool bPersistedViewportToolbarCollapsed = false;
		if (GConfig->GetBool(kShotEditorLayoutConfigSection,
			kViewportToolbarCollapsedKey,
			bPersistedViewportToolbarCollapsed,
			GEditorPerProjectIni))
		{
			ViewportToolbarCollapsed = bPersistedViewportToolbarCollapsed;
		}

		bool bPersistedQuickControlsCollapsed = true;
		if (GConfig->GetBool(kShotEditorLayoutConfigSection,
			kQuickControlsCollapsedKey,
			bPersistedQuickControlsCollapsed,
			GEditorPerProjectIni))
		{
			QuickControlsCollapsed = bPersistedQuickControlsCollapsed;
		}
	}

	const FShotEditorLayoutState LayoutState =
		ResolveLayoutState(ViewportToolbarCollapsed, QuickControlsCollapsed);
	bViewportToolbarCollapsed = LayoutState.bViewportToolbarCollapsed;
	bQuickControlsCollapsed = LayoutState.bQuickControlsCollapsed;
}

void SShotEditorRoot::SavePersistedLayoutState() const
{
	if (!GConfig)
	{
		return;
	}

	GConfig->SetBool(kShotEditorLayoutConfigSection,
		kViewportToolbarCollapsedKey,
		bViewportToolbarCollapsed,
		GEditorPerProjectIni);
	GConfig->SetBool(kShotEditorLayoutConfigSection,
		kQuickControlsCollapsedKey,
		bQuickControlsCollapsed,
		GEditorPerProjectIni);
	GConfig->Flush(/*bRead=*/false, GEditorPerProjectIni);
}

TSharedRef<SWidget> SShotEditorRoot::BuildQuickControls()
{
	return SNew(SExpandableArea)
		.InitiallyCollapsed(bQuickControlsCollapsed)
		.OnAreaExpansionChanged(this, &SShotEditorRoot::OnQuickControlsExpansionChanged)
		.BorderImage(FAppStyle::GetBrush("DetailsView.CategoryTop"))
		.HeaderContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("QuickControlsHeader", "Quick"))
			.ColorAndOpacity(FLinearColor(0.85f, 0.85f, 0.9f, 1.f))
		]
		.BodyContent()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(6.f, 4.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.f, 0.f, 4.f, 0.f)
					[BuildQuickFloatControl(EQuickControlField::Distance,
						LOCTEXT("QuickDistance", "Distance"),
						LOCTEXT("QuickDistanceTip", "Placement.Distance"),
						FShotPlacement::MinDistance,
						FShotPlacement::MaxDistance)]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.f, 0.f, 4.f, 0.f)
					[BuildQuickFloatControl(EQuickControlField::FOV,
						LOCTEXT("QuickFOV", "Manual FOV"),
						LOCTEXT("QuickFOVTip", "Lens.ManualFOV"),
						1.f,
						170.f)]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					[BuildQuickFloatControl(EQuickControlField::Roll,
						LOCTEXT("QuickRoll", "Roll"),
						LOCTEXT("QuickRollTip", "Shot.Roll"),
						-180.f,
						180.f)]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 4.f, 0.f, 0.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.f, 0.f, 4.f, 0.f)
					[BuildQuickFloatControl(EQuickControlField::PlacementX,
						LOCTEXT("QuickPlaceX", "Placement X"),
						LOCTEXT("QuickPlaceXTip", "Placement.ScreenPosition.X"),
						-0.5f,
						0.5f)]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.f, 0.f, 4.f, 0.f)
					[BuildQuickFloatControl(EQuickControlField::PlacementY,
						LOCTEXT("QuickPlaceY", "Placement Y"),
						LOCTEXT("QuickPlaceYTip", "Placement.ScreenPosition.Y"),
						-0.5f,
						0.5f)]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.f, 0.f, 4.f, 0.f)
					[BuildQuickFloatControl(EQuickControlField::AimX,
						LOCTEXT("QuickAimX", "Aim X"),
						LOCTEXT("QuickAimXTip", "Aim.ScreenPosition.X"),
						-0.5f,
						0.5f)]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					[BuildQuickFloatControl(EQuickControlField::AimY,
						LOCTEXT("QuickAimY", "Aim Y"),
						LOCTEXT("QuickAimYTip", "Aim.ScreenPosition.Y"),
						-0.5f,
						0.5f)]
				]
			]
		];
}

TSharedRef<SWidget> SShotEditorRoot::BuildQuickFloatControl(EQuickControlField Field,
	const FText& Label,
	const FText& ToolTip,
	float MinValue,
	float MaxValue)
{
	TSharedRef<TOptional<float>> DragCache = MakeShared<TOptional<float>>();
	return SNew(SBox)
		.WidthOverride(112.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(Label)
				.ToolTipText(ToolTip)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 1.f, 0.f, 0.f)
			[
				SNew(SNumericEntryBox<float>)
				.AllowSpin(true)
				.MinValue(TOptional<float>(MinValue))
				.MaxValue(TOptional<float>(MaxValue))
				.MinSliderValue(TOptional<float>(MinValue))
				.MaxSliderValue(TOptional<float>(MaxValue))
				.MinDesiredValueWidth(56.f)
				.IsEnabled_Lambda([this, Field]()
				{
					return IsQuickControlEnabled(Field);
				})
				.Value_Lambda([this, Field, DragCache]() -> TOptional<float>
				{
					if (DragCache->IsSet())
					{
						return DragCache->GetValue();
					}
					return GetQuickControlValue(Field);
				})
				.OnValueChanged_Lambda([DragCache](float NewValue)
				{
					*DragCache = NewValue;
				})
				.OnValueCommitted_Lambda([this, Field, DragCache](float NewValue, ETextCommit::Type)
				{
					DragCache->Reset();
					CommitQuickControlValue(Field, NewValue);
				})
				.OnEndSliderMovement_Lambda([this, Field, DragCache](float NewValue)
				{
					DragCache->Reset();
					CommitQuickControlValue(Field, NewValue);
				})
			]
		];
}

TOptional<float> SShotEditorRoot::GetQuickControlValue(EQuickControlField Field) const
{
	if (!ActiveShot)
	{
		return TOptional<float>();
	}

	switch (Field)
	{
	case EQuickControlField::Distance:
		return ActiveShot->Placement.Distance;
	case EQuickControlField::FOV:
		return ActiveShot->Lens.ManualFOV;
	case EQuickControlField::Roll:
		return ActiveShot->Roll;
	case EQuickControlField::PlacementX:
		return ActiveShot->Placement.ScreenPosition.X;
	case EQuickControlField::PlacementY:
		return ActiveShot->Placement.ScreenPosition.Y;
	case EQuickControlField::AimX:
		return ActiveShot->Aim.ScreenPosition.X;
	case EQuickControlField::AimY:
		return ActiveShot->Aim.ScreenPosition.Y;
	default:
		return TOptional<float>();
	}
}

bool SShotEditorRoot::IsQuickControlEnabled(EQuickControlField Field) const
{
	if (!ActiveShot || !ActiveHost.IsValid())
	{
		return false;
	}

	switch (Field)
	{
	case EQuickControlField::Distance:
		return ActiveShot->Placement.Mode == EShotPlacementMode::AnchorOrbit
			|| ActiveShot->Placement.Mode == EShotPlacementMode::AnchorAtScreen;
	case EQuickControlField::FOV:
		return ActiveShot->Lens.FOVMode == EShotFOVMode::Manual;
	case EQuickControlField::Roll:
		return true;
	case EQuickControlField::PlacementX:
	case EQuickControlField::PlacementY:
		return ActiveShot->Placement.Mode == EShotPlacementMode::AnchorAtScreen;
	case EQuickControlField::AimX:
	case EQuickControlField::AimY:
		return ActiveShot->Aim.Mode == EShotAimMode::LookAtAnchor;
	default:
		return false;
	}
}

void SShotEditorRoot::CommitQuickControlValue(EQuickControlField Field, float NewValue)
{
	if (!ActiveShot || !ActiveHost.IsValid())
	{
		return;
	}

	const TOptional<float> OldValue = GetQuickControlValue(Field);
	if (!OldValue.IsSet())
	{
		return;
	}

	switch (Field)
	{
	case EQuickControlField::Distance:
		NewValue = FMath::Clamp(NewValue, FShotPlacement::MinDistance, FShotPlacement::MaxDistance);
		break;
	case EQuickControlField::FOV:
		NewValue = FMath::Clamp(NewValue, 1.f, 170.f);
		break;
	case EQuickControlField::Roll:
		NewValue = FMath::Clamp(NewValue, -180.f, 180.f);
		break;
	case EQuickControlField::PlacementX:
	case EQuickControlField::PlacementY:
	case EQuickControlField::AimX:
	case EQuickControlField::AimY:
		NewValue = FMath::Clamp(NewValue, -0.5f, 0.5f);
		break;
	default:
		return;
	}

	if (FMath::IsNearlyEqual(OldValue.GetValue(), NewValue))
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("EditShotQuickControl", "Edit Shot Quick Control"));
	if (UObject* Host = ActiveHost.Get())
	{
		Host->Modify();
	}

	switch (Field)
	{
	case EQuickControlField::Distance:
		ActiveShot->Placement.Distance = NewValue;
		break;
	case EQuickControlField::FOV:
		ActiveShot->Lens.ManualFOV = NewValue;
		break;
	case EQuickControlField::Roll:
		ActiveShot->Roll = NewValue;
		break;
	case EQuickControlField::PlacementX:
		ActiveShot->Placement.ScreenPosition.X = NewValue;
		break;
	case EQuickControlField::PlacementY:
		ActiveShot->Placement.ScreenPosition.Y = NewValue;
		break;
	case EQuickControlField::AimX:
		ActiveShot->Aim.ScreenPosition.X = NewValue;
		break;
	case EQuickControlField::AimY:
		ActiveShot->Aim.ScreenPosition.Y = NewValue;
		break;
	default:
		break;
	}

	PostActiveShotValueSet();
	RefreshDetailsView();
}

FProperty* SShotEditorRoot::ResolveActiveShotProperty() const
{
	UObject* Host = ActiveHost.Get();
	if (!Host)
	{
		return nullptr;
	}

	if (UMovieSceneComposableCameraShotSection* Section =
			Cast<UMovieSceneComposableCameraShotSection>(Host))
	{
		if (ActiveShot == &Section->InlineShot)
		{
			return Section->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMovieSceneComposableCameraShotSection, InlineShot));
		}
		if (ActiveShot == &Section->ShotOverrides)
		{
			return Section->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMovieSceneComposableCameraShotSection, ShotOverrides));
		}
	}

	return Host->GetClass()->FindPropertyByName(TEXT("Shot"));
}

void SShotEditorRoot::PostActiveShotValueSet()
{
	UObject* Host = ActiveHost.Get();
	FProperty* ShotProp = ResolveActiveShotProperty();
	if (!Host || !ShotProp)
	{
		return;
	}

	FPropertyChangedEvent Event(ShotProp, EPropertyChangeType::ValueSet);
	Host->PostEditChangeProperty(Event);
}

void SShotEditorRoot::NotifyPreChange(FProperty* /*PropertyAboutToChange*/)
{
	// Snapshot pre-edit state for undo via the bare-bones path - 
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

void SShotEditorRoot::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* /*PropertyThatChanged*/)
{
	// Skip Interactive (per-frame slider drag) so Sequencer's eval cache
	// isn't invalidated mid-drag. The host's downstream listeners fire on
	// commit (ValueSet) - sufficient for graph-node refresh / Build /
	// runtime debug. Solver in the viewport reads ActiveShot directly each
	// tick so live drag visual feedback is unaffected.
	if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive)
	{
		return;
	}

	UObject* Host = ActiveHost.Get();
	FProperty* ShotProp = ResolveActiveShotProperty();
	if (Host && ShotProp)
	{
		FPropertyChangedEvent OuterEvent(ShotProp, PropertyChangedEvent.ChangeType);
		Host->PostEditChangeProperty(OuterEvent);
	}
}

namespace
{
	/** Section title resolver - mirrors `FComposableCameraShotSectionInterface::GetSectionTitle`'s
	 * format ("Inline (N)" / asset name / "(no asset)") so the header label
	 * reads identically to what Sequencer renders on the section. Replicated
	 * here rather than going through the SectionInterface because (a) we
	 * don't have a Painter context to construct one and (b) the label needs
	 * to be cheap (called every context swap + every menu rebuild). */
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
	 * null when the chain is broken (Section orphaned mid-edit). */
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
	 * both the header context label and the LS-shots dropdown entries. The
	 * suffix lets multiple same-titled sections (e.g. several Inline shots
	 * on different rows / time windows in one LevelSequence) read
	 * unambiguously in either surface. Falls back to "(Unbounded, Row N)"
	 * for sections without closed bounds, or an empty string when the
	 * parent MovieScene is unresolvable. */
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
			const double StartSec = TickResolution.AsSeconds(FFrameTime(Range.GetLowerBoundValue()));
			const double EndSec = TickResolution.AsSeconds(FFrameTime(Range.GetUpperBoundValue()));
			return FString::Printf(TEXT("(%.2fs - %.2fs, Row %d)"),
				StartSec, EndSec, Section.GetRowIndex());
		}
		return FString::Printf(TEXT("(Unbounded, Row %d)"), Section.GetRowIndex());
	}

	struct FLSShotMenuEntry
	{
		TWeakObjectPtr<UMovieSceneComposableCameraShotSection> Section;
		FString TrackLabel;
		FString TitleLabel;
		FString TimeRowSuffix;
		bool bIsCurrent = false;
	};
}

FText SShotEditorRoot::BuildHostContextChain() const
{
	if (!ActiveShot)
	{
		return LOCTEXT("NoShotLoadedLabel", "No Shot loaded");
	}

	UObject* Host = ActiveHost.Get();
	if (!Host)
	{
		return LOCTEXT("ActiveHostStaleLabel", "Host destroyed");
	}

	// Section host -> "{LS} -> {Track} -> {Section} (start - end, Row N)".
	// The trailing time/row suffix matches the LS-shots dropdown so the
	// header reads unambiguously when the LevelSequence holds multiple
	// same-titled sections, AND so the snapshot pushed to FShotEditorHistory
	// (which captures whatever this function returns) carries the
	// disambiguation through to the Recent menu as well.
	if (const UMovieSceneComposableCameraShotSection* Section =
			Cast<UMovieSceneComposableCameraShotSection>(Host))
	{
		const UMovieSceneTrack* Track = Section->GetTypedOuter<UMovieSceneTrack>();
		const ULevelSequence* LS = ResolveLevelSequenceForSection(Section);
		const FString LSName = LS ? LS->GetName() : FString(TEXT("?"));
		const FString TrackName = Track ? Track->GetDisplayName().ToString() : FString(TEXT("?"));
		const FString SectionTitle = ResolveShotSectionTitle(*Section);
		const FString Suffix = BuildSectionTimeRowSuffix(*Section);
		return FText::FromString(FString::Printf(TEXT("%s -> %s -> %s %s"),
			*LSName, *TrackName, *SectionTitle, *Suffix));
	}

	// Standalone ShotAsset host -> just the asset name (no parent chain
	// makes sense - the asset is the endpoint).
	if (const UComposableCameraShotAsset* ShotAsset = Cast<UComposableCameraShotAsset>(Host))
	{
		return FText::FromString(ShotAsset->GetName());
	}

	// Camera-graph node host (CompositionFramingNode, possibly other
	// solver-aware nodes in the future) -> "{TypeAsset} -> {Node}".
	if (const UComposableCameraCameraNodeBase* Node =
			Cast<UComposableCameraCameraNodeBase>(Host))
	{
		const UComposableCameraTypeAsset* TypeAsset =
			Node->GetTypedOuter<UComposableCameraTypeAsset>();
		const FString AssetName = TypeAsset
			? TypeAsset->GetName()
			: Node->GetOutermost()->GetName();
		return FText::FromString(FString::Printf(TEXT("%s -> %s"), *AssetName, *Node->GetName()));
	}

	// Unknown host shape - show enough identity for the designer to see what's
	// bound rather than a blank label.
	return FText::Format(LOCTEXT("ActiveHostFmt", "{0} ({1})"),
		FText::FromString(Host->GetName()),
		FText::FromString(Host->GetClass()->GetName()));
}

EVisibility SShotEditorRoot::GetLSShotsComboVisibility() const
{
	// Only meaningful when the host is a Sequencer Shot Section - for
	// CameraTypeAsset / standalone-ShotAsset hosts there's no sibling list
	// to enumerate. Collapsed (not Hidden) so the layout reclaims the space.
	return Cast<UMovieSceneComposableCameraShotSection>(ActiveHost.Get())
		? EVisibility::Visible: EVisibility::Collapsed;
}

TSharedRef<SWidget> SShotEditorRoot::BuildLSShotsMenu()
{
	UMovieSceneComposableCameraShotSection* CurrentSection =
		Cast<UMovieSceneComposableCameraShotSection>(ActiveHost.Get());
	const ULevelSequence* LS = ResolveLevelSequenceForSection(CurrentSection);
	const UMovieScene* MovieScene = LS ? LS->GetMovieScene() : nullptr;
	if (!MovieScene)
	{
		return SNew(SBox)
			.WidthOverride(420.f)
			.Padding(10.f)
			[SNew(STextBlock)
				.Text(LOCTEXT("LSShotsNoLS", "No LevelSequence resolved."))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())];
	}

	// Walk every Shot section in the MovieScene. Shot tracks live under
	// object bindings (FMovieSceneBinding) since `FComposableCameraShotTrackEditor`
	// extends `BuildObjectBindingTrackMenu`, but we also scan root tracks
	// defensively so future-routing changes don't silently empty the menu.
	TSharedRef<TArray<FLSShotMenuEntry>> Entries = MakeShared<TArray<FLSShotMenuEntry>>();
	TSet<const UMovieSceneComposableCameraShotSection*> SeenSections;
	auto AddShotEntry =
		[Entries, &SeenSections, CurrentSection](UMovieSceneComposableCameraShotSection* Section,
			const FString& TrackLabel)
	{
		if (!Section || SeenSections.Contains(Section))
		{
			return;
		}

		SeenSections.Add(Section);
		FLSShotMenuEntry Entry;
		Entry.Section = Section;
		Entry.TrackLabel = TrackLabel;
		Entry.TitleLabel = ResolveShotSectionTitle(*Section);
		Entry.TimeRowSuffix = BuildSectionTimeRowSuffix(*Section);
		Entry.bIsCurrent = (Section == CurrentSection);
		Entries->Add(MoveTemp(Entry));
	};

	for (const FMovieSceneBinding& Binding: MovieScene->GetBindings())
	{
		for (UMovieSceneTrack* Track: Binding.GetTracks())
		{
			if (!Track)
			{
				continue;
			}
			const FString TrackLabel = Track->GetDisplayName().ToString();
			for (UMovieSceneSection* Sec: Track->GetAllSections())
			{
				AddShotEntry(Cast<UMovieSceneComposableCameraShotSection>(Sec),
					TrackLabel);
			}
		}
	}

	// Defensive scan of root-level tracks. Currently no Shot tracks live
	// here, but covering this branch keeps the menu honest if track routing
	// ever changes.
	for (UMovieSceneTrack* Track: MovieScene->GetTracks())
	{
		if (!Track)
		{
			continue;
		}
		const FString TrackLabel = Track->GetDisplayName().ToString();
		for (UMovieSceneSection* Sec: Track->GetAllSections())
		{
			AddShotEntry(Cast<UMovieSceneComposableCameraShotSection>(Sec),
				TrackLabel);
		}
	}

	TSharedRef<FString> SearchText = MakeShared<FString>();
	TSharedRef<TSharedPtr<SScrollBox>> ResultsBoxRef = MakeShared<TSharedPtr<SScrollBox>>();

	auto AddEmptyRow = [ResultsBoxRef](const FText& Message)
	{
		if (!ResultsBoxRef->IsValid())
		{
			return;
		}
		(*ResultsBoxRef)->AddSlot()
			.Padding(8.f, 10.f)
			[SNew(STextBlock)
				.Text(Message)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())];
	};

	auto RebuildResults = [this, Entries, SearchText, ResultsBoxRef, AddEmptyRow]()
	{
		using ComposableCameraSystem::ShotEditorMenu::MatchesSearchFilter;

		if (!ResultsBoxRef->IsValid())
		{
			return;
		}

		(*ResultsBoxRef)->ClearChildren();
		if (Entries->Num() == 0)
		{
			AddEmptyRow(LOCTEXT("LSShotsEmpty", "No Shot sections in this LevelSequence."));
			return;
		}

		FString LastTrackLabel;
		bool bAnyVisible = false;
		for (const FLSShotMenuEntry& Entry: *Entries)
		{
			if (!MatchesSearchFilter(Entry.TrackLabel, Entry.TitleLabel, Entry.TimeRowSuffix, *SearchText))
			{
				continue;
			}

			if (LastTrackLabel != Entry.TrackLabel)
			{
				LastTrackLabel = Entry.TrackLabel;
				(*ResultsBoxRef)->AddSlot()
					.Padding(8.f, bAnyVisible ? 8.f: 2.f, 8.f, 2.f)
					[SNew(STextBlock)
						.Text(FText::FromString(Entry.TrackLabel))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())];
			}

			const TWeakObjectPtr<UMovieSceneComposableCameraShotSection> WeakSection = Entry.Section;
			const bool bIsCurrent = Entry.bIsCurrent;
			const FSlateColor TitleColor = bIsCurrent
				? FSlateColor(FLinearColor(1.f, 0.95f, 0.55f, 1.f))
				: FSlateColor::UseForeground();

			(*ResultsBoxRef)->AddSlot()
				.Padding(4.f, 1.f)
				[SNew(SButton)
					.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
					.ContentPadding(FMargin(6.f, 4.f))
					.OnClicked_Lambda([this, WeakSection, bIsCurrent]() -> FReply
					{
						if (bIsCurrent)
						{
							return FReply::Handled();
						}
						if (UMovieSceneComposableCameraShotSection* Live = WeakSection.Get())
						{
							if (LSShotsCombo.IsValid())
							{
								LSShotsCombo->SetIsOpen(false);
							}
							FComposableCameraShotEditor::OpenForShotSection(Live);
						}
						return FReply::Handled();
					})
					[SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.f, 0.f, 6.f, 0.f)
						[SNew(SImage)
							.Image(FAppStyle::GetBrush("Icons.Check"))
							.Visibility(bIsCurrent ? EVisibility::Visible: EVisibility::Hidden)]

						+ SHorizontalBox::Slot()
						.FillWidth(1.f)
						.VAlign(VAlign_Center)
						[SNew(STextBlock)
							.Text(FText::FromString(Entry.TitleLabel))
							.ColorAndOpacity(TitleColor)]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(12.f, 0.f, 0.f, 0.f)
						[SNew(STextBlock)
							.Text(FText::FromString(Entry.TimeRowSuffix))
							.ColorAndOpacity(FSlateColor::UseSubduedForeground())]]];

			bAnyVisible = true;
		}

		if (!bAnyVisible)
		{
			AddEmptyRow(LOCTEXT("LSShotsNoMatches", "No matching shots."));
		}
	};

	TSharedPtr<SScrollBox> ResultsBox;
	TSharedRef<SWidget> MenuWidget = SNew(SBox)
		.WidthOverride(460.f)
		.MaxDesiredHeight(520.f)
		[SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(6.f)
			[SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 6.f)
				[SNew(SSearchBox)
					.HintText(LOCTEXT("LSShotsSearchHint", "Search shots, tracks, or time"))
					.OnTextChanged_Lambda([SearchText, RebuildResults](const FText& NewText)
					{
						*SearchText = NewText.ToString();
						RebuildResults();
					})]

				+ SVerticalBox::Slot()
				.FillHeight(1.f)
				[SAssignNew(ResultsBox, SScrollBox)]]];

	*ResultsBoxRef = ResultsBox;
	RebuildResults();
	return MenuWidget;
}

TSharedRef<SWidget> SShotEditorRoot::BuildHistoryMenu()
{
	FMenuBuilder MenuBuilder(/*bShouldCloseAfterSelect=*/true, /*CommandList=*/nullptr);

	const TArray<FShotEditorHistoryEntry>& Entries =
		FShotEditorHistory::Get().GetEntries();
	if (Entries.IsEmpty())
	{
		MenuBuilder.AddMenuEntry(LOCTEXT("HistoryEmpty", "History is empty."),
			FText::GetEmpty(), FSlateIcon(), FUIAction(),
			NAME_None, EUserInterfaceActionType::None);
		return MenuBuilder.MakeWidget();
	}

	for (const FShotEditorHistoryEntry& Entry: Entries)
	{
		const TWeakObjectPtr<UObject> WeakHost = Entry.Host;
		const FSoftObjectPath HostPath = Entry.HostPath;
		const bool bAlive = WeakHost.IsValid();
		const bool bResolvable = bAlive || HostPath.IsValid();

		// Tooltip distinguishes three states: live (in-session), persisted
		// (live ref cold but soft path on disk - clicking will load the
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

		MenuBuilder.AddMenuEntry(Entry.DisplayLabel,
			Tooltip,
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([WeakHost, HostPath]()
				{
					// Re-resolve through the singleton so the live-then-
					// path fallback stays single-source.
					FShotEditorHistoryEntry Snapshot;
					Snapshot.Host = WeakHost;
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

	using namespace ComposableCameraSystem::ShotEditorModeSwitch;
	switch (ClassifyModeRequest(OldMode, NewMode))
	{
	case EModeRequestHandling::Ignore:
		return;
	case EModeRequestHandling::ShowFreeExitStatus:
		QueueFreeExitStatus(NewMode);
		return;
	case EModeRequestHandling::ApplyImmediately:
		ClearPendingFreeExitMode();
		Viewport->SetMode(NewMode);
		return;
	}

	return;
}

void SShotEditorRoot::ClearPendingFreeExitMode()
{
	bHasPendingFreeExitMode = false;
	PendingFreeExitMode = EShotEditorMode::Drag;
	PendingFreeExitStatus = EShotEditorReverseSolveStatus::Ok;
}

void SShotEditorRoot::QueueFreeExitStatus(EShotEditorMode NewMode)
{
	if (!Viewport.IsValid())
	{
		ClearPendingFreeExitMode();
		return;
	}

	PendingFreeExitMode = NewMode;
	bHasPendingFreeExitMode = true;
	PendingFreeExitStatus = Viewport->DiagnoseReverseSolveCurrentCamera();
}

void SShotEditorRoot::ApplyPendingFreeExitMode(bool bSaveCurrentCamera)
{
	if (!bHasPendingFreeExitMode)
	{
		return;
	}
	if (!Viewport.IsValid())
	{
		ClearPendingFreeExitMode();
		return;
	}

	if (bSaveCurrentCamera && !Viewport->ReverseSolveCurrentCameraToShot())
	{
		PendingFreeExitStatus = Viewport->DiagnoseReverseSolveCurrentCamera();
		return;
	}

	const EShotEditorMode ModeToApply = PendingFreeExitMode;
	ClearPendingFreeExitMode();
	Viewport->SetMode(ModeToApply);
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

	// Only fire on plain key presses (no modifiers) - leaves Ctrl+1 / Alt+2
	// etc. free for stock editor shortcuts.
	if (InKeyEvent.IsControlDown() || InKeyEvent.IsShiftDown()
		|| InKeyEvent.IsAltDown() || InKeyEvent.IsCommandDown())
	{
		return FReply::Unhandled();
	}

	const FKey Key = InKeyEvent.GetKey();
	const bool bHandled =
		(Key == EKeys::One && (TrySetMode(EShotEditorMode::Drag), true)) ||
		(Key == EKeys::Two && (TrySetMode(EShotEditorMode::Free), true)) ||
		(Key == EKeys::Three && (TrySetMode(EShotEditorMode::Lock), true));

	if (bHandled)
	{
		// Restore keyboard focus to this root widget after mode shortcuts.
		// Free-exit confirmation lives in the status bar, so focus does not
		// leave Slate.
		FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::SetDirectly);
		return FReply::Handled();
	}

	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

// Asset toolbar 

namespace
{
	/** Walk the host's outer chain to find the asset (the UObject whose
	 * outer is the package). Returns null when the host is null or
	 * somehow lacks a package - neither path should happen in practice
	 * but the null-guard keeps the toolbar safe under teardown races. */
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
		return (Asset && Asset->GetOutermost() != Asset) ? Asset: nullptr;
	}
}

TSharedRef<SWidget> SShotEditorRoot::BuildHeaderArea()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[BuildTopBar()]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 2.f, 0.f, 0.f)
		[BuildStatusBar()];
}

TSharedRef<SWidget> SShotEditorRoot::BuildTopBar()
{
	return SNew(SBorder)
		.BorderBackgroundColor(FLinearColor(0.10f, 0.10f, 0.12f, 1.f))
		.Padding(4.f, 2.f)
		[SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[BuildAssetToolbar()]

			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			.Padding(8.f, 0.f, 8.f, 0.f)
			[SAssignNew(HostNameLabel, STextBlock)
				.Text(BuildHostContextChain())
				.ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.95f, 1.f))]

			// "Shots" - dropdown of all Shot sections in the active host's
			// parent LevelSequence. Hidden in non-LS contexts.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.f, 0.f, 4.f, 0.f)
			[SAssignNew(LSShotsCombo, SComboButton)
				.Visibility(this, &SShotEditorRoot::GetLSShotsComboVisibility)
				.ToolTipText(LOCTEXT("LSShotsTooltip",
					"Jump to another Shot section in this LevelSequence. "
					"Only shown when the active Shot's host is a "
					"Sequencer Section - for CameraTypeAsset / "
					"standalone ShotAsset hosts there's no sibling list."))
				.OnGetMenuContent(this, &SShotEditorRoot::BuildLSShotsMenu)
				.ButtonContent()
				[SNew(STextBlock).Text(LOCTEXT("LSShotsLabel", "Shots"))]]

			// "Recent" - last 20 hosts the editor was bound to (most recent
			// first). Backed by FShotEditorHistory.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.f, 0.f, 8.f, 0.f)
			[SAssignNew(HistoryCombo, SComboButton)
				.ToolTipText(LOCTEXT("HistoryTooltip",
					"Reopen a recently edited Shot. History is in-memory "
					"for the current editor session (last 20 entries)."))
				.OnGetMenuContent(this, &SShotEditorRoot::BuildHistoryMenu)
				.ButtonContent()
				[SNew(STextBlock).Text(LOCTEXT("HistoryLabel", "Recent"))]]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[SNew(SSegmentedControl<EShotEditorMode>)
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
						"to Drag or Lock shows Save / Discard / Stay for "
						"the current camera framing."))
				+ SSegmentedControl<EShotEditorMode>::Slot(EShotEditorMode::Lock)
					.Text(LOCTEXT("ModeLock", "Lock"))
					.ToolTip(LOCTEXT("ModeLockTip",
						"Lock mode: solver drives the camera (same as "
						"Drag) but ALL viewport input is consumed - no "
						"handle drag, no camera control, no scroll-zoom. "
						"Read-only preview state, useful for "
						"screenshots / demos / preventing accidental "
						"edits."))]];
}

TSharedRef<SWidget> SShotEditorRoot::BuildStatusBar()
{
	return SNew(SBorder)
		.Visibility(this, &SShotEditorRoot::GetStatusBarVisibility)
		.BorderBackgroundColor(this, &SShotEditorRoot::GetStatusBarBackgroundColor)
		.Padding(6.f, 4.f)
		[SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			[SNew(STextBlock)
				.Text(this, &SShotEditorRoot::GetStatusBarText)
				.AutoWrapText(true)
				.ColorAndOpacity(this, &SShotEditorRoot::GetStatusBarTextColor)]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(6.f, 0.f, 0.f, 0.f)
			[SNew(SHorizontalBox)
				.Visibility(this, &SShotEditorRoot::GetStatusBarFreeExitActionsVisibility)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.IsEnabled(this, &SShotEditorRoot::CanSaveFreeExitStatus)
					.OnClicked(this, &SShotEditorRoot::OnFreeExitStatusSaveClicked)
					.ToolTipText(this, &SShotEditorRoot::GetFreeExitStatusSaveTooltip)
					[SNew(STextBlock)
						.Text(LOCTEXT("FreeExitPromptSave", "Save"))]]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.f, 0.f, 0.f, 0.f)
				[SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.OnClicked(this, &SShotEditorRoot::OnFreeExitStatusDiscardClicked)
					.ToolTipText(LOCTEXT("FreeExitPromptDiscardTooltip",
						"Discard the Free camera pose and switch modes."))
					[SNew(STextBlock)
						.Text(LOCTEXT("FreeExitPromptDiscard", "Discard"))]]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.f, 0.f, 0.f, 0.f)
				[SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.OnClicked(this, &SShotEditorRoot::OnFreeExitStatusStayClicked)
					.ToolTipText(LOCTEXT("FreeExitPromptStayTooltip",
						"Stay in Free mode."))
					[SNew(STextBlock)
						.Text(LOCTEXT("FreeExitPromptStay", "Stay"))]]]];
}

EVisibility SShotEditorRoot::GetStatusBarVisibility() const
{
	const EShotEditorMode CurrentMode = Viewport.IsValid()
		? Viewport->GetMode()
		: EShotEditorMode::Drag;
	const ComposableCameraSystem::ShotEditorStatusBar::FShotEditorStatusBarState State =
		ComposableCameraSystem::ShotEditorStatusBar::ResolveStatusBarState(
			ActiveShot != nullptr,
			ActiveHost.IsValid(),
			bHasPendingFreeExitMode,
			CurrentMode);
	return ComposableCameraSystem::ShotEditorStatusBar::ShouldShowStatusBar(State)
		? EVisibility::Visible
		: EVisibility::Collapsed;
}

EVisibility SShotEditorRoot::GetStatusBarFreeExitActionsVisibility() const
{
	const EShotEditorMode CurrentMode = Viewport.IsValid()
		? Viewport->GetMode()
		: EShotEditorMode::Drag;
	const ComposableCameraSystem::ShotEditorStatusBar::FShotEditorStatusBarState State =
		ComposableCameraSystem::ShotEditorStatusBar::ResolveStatusBarState(
			ActiveShot != nullptr,
			ActiveHost.IsValid(),
			bHasPendingFreeExitMode,
			CurrentMode);
	return State.Actions == ComposableCameraSystem::ShotEditorStatusBar::EShotEditorStatusBarActions::FreeExit
		? EVisibility::Visible
		: EVisibility::Collapsed;
}

FSlateColor SShotEditorRoot::GetStatusBarBackgroundColor() const
{
	const EShotEditorMode CurrentMode = Viewport.IsValid()
		? Viewport->GetMode()
		: EShotEditorMode::Drag;
	const ComposableCameraSystem::ShotEditorStatusBar::FShotEditorStatusBarState State =
		ComposableCameraSystem::ShotEditorStatusBar::ResolveStatusBarState(
			ActiveShot != nullptr,
			ActiveHost.IsValid(),
			bHasPendingFreeExitMode,
			CurrentMode);

	switch (State.Kind)
	{
	case ComposableCameraSystem::ShotEditorStatusBar::EShotEditorStatusBarKind::Info:
		return FSlateColor(FLinearColor(0.06f, 0.09f, 0.12f, 1.f));
	case ComposableCameraSystem::ShotEditorStatusBar::EShotEditorStatusBarKind::Warning:
		return FSlateColor(FLinearColor(0.16f, 0.12f, 0.05f, 1.f));
	case ComposableCameraSystem::ShotEditorStatusBar::EShotEditorStatusBarKind::Hidden:
	default:
		return FSlateColor(FLinearColor::Transparent);
	}
}

FSlateColor SShotEditorRoot::GetStatusBarTextColor() const
{
	const EShotEditorMode CurrentMode = Viewport.IsValid()
		? Viewport->GetMode()
		: EShotEditorMode::Drag;
	const ComposableCameraSystem::ShotEditorStatusBar::FShotEditorStatusBarState State =
		ComposableCameraSystem::ShotEditorStatusBar::ResolveStatusBarState(
			ActiveShot != nullptr,
			ActiveHost.IsValid(),
			bHasPendingFreeExitMode,
			CurrentMode);

	switch (State.Kind)
	{
	case ComposableCameraSystem::ShotEditorStatusBar::EShotEditorStatusBarKind::Info:
		return FSlateColor(FLinearColor(0.72f, 0.82f, 0.92f, 1.f));
	case ComposableCameraSystem::ShotEditorStatusBar::EShotEditorStatusBarKind::Warning:
		return FSlateColor(FLinearColor(0.95f, 0.88f, 0.66f, 1.f));
	case ComposableCameraSystem::ShotEditorStatusBar::EShotEditorStatusBarKind::Hidden:
	default:
		return FSlateColor::UseForeground();
	}
}

FText SShotEditorRoot::GetStatusBarText() const
{
	const EShotEditorMode CurrentMode = Viewport.IsValid()
		? Viewport->GetMode()
		: EShotEditorMode::Drag;
	const ComposableCameraSystem::ShotEditorStatusBar::FShotEditorStatusBarState State =
		ComposableCameraSystem::ShotEditorStatusBar::ResolveStatusBarState(
			ActiveShot != nullptr,
			ActiveHost.IsValid(),
			bHasPendingFreeExitMode,
			CurrentMode);

	if (State.Actions == ComposableCameraSystem::ShotEditorStatusBar::EShotEditorStatusBarActions::FreeExit)
	{
		if (PendingFreeExitStatus == EShotEditorReverseSolveStatus::Ok)
		{
			return LOCTEXT("FreeExitPromptReady",
				"Free camera moved. Save framing before leaving Free?");
		}

		return FText::Format(LOCTEXT("FreeExitPromptUnavailable",
			"Free camera moved. Save unavailable: {0}"),
			ShotEditorReverseSolveStatusToText(PendingFreeExitStatus));
	}

	if (ActiveShot && !ActiveHost.IsValid())
	{
		return LOCTEXT("StatusBarHostDestroyed",
			"Host destroyed. Shot context cleared; reopen from the source asset or Sequencer section.");
	}

	if (!ActiveShot)
	{
		return LOCTEXT("StatusBarNoShotLoaded",
			"No Shot loaded. Open a Shot from a CompositionFramingNode, Shot asset, or Sequencer section.");
	}

	return FText::GetEmpty();
}

FText SShotEditorRoot::GetFreeExitStatusSaveTooltip() const
{
	if (PendingFreeExitStatus == EShotEditorReverseSolveStatus::Ok)
	{
		return LOCTEXT("FreeExitPromptSaveTooltip",
			"Save the current Free camera pose into Shot params and switch modes.");
	}

	return FText::Format(LOCTEXT("FreeExitPromptSaveUnavailableTooltip",
		"Save is unavailable: {0}"),
		ShotEditorReverseSolveStatusToText(PendingFreeExitStatus));
}

bool SShotEditorRoot::CanSaveFreeExitStatus() const
{
	return bHasPendingFreeExitMode
		&& ActiveShot
		&& ActiveHost.IsValid()
		&& Viewport.IsValid()
		&& PendingFreeExitStatus == EShotEditorReverseSolveStatus::Ok;
}

FReply SShotEditorRoot::OnFreeExitStatusSaveClicked()
{
	ApplyPendingFreeExitMode(/*SaveCurrentCamera=*/true);
	return FReply::Handled();
}

FReply SShotEditorRoot::OnFreeExitStatusDiscardClicked()
{
	ApplyPendingFreeExitMode(/*SaveCurrentCamera=*/false);
	return FReply::Handled();
}

FReply SShotEditorRoot::OnFreeExitStatusStayClicked()
{
	ClearPendingFreeExitMode();
	return FReply::Handled();
}

TSharedRef<SWidget> SShotEditorRoot::BuildAssetToolbar()
{
	FToolBarBuilder ToolbarBuilder(/*InCommandList=*/nullptr,
		FMultiBoxCustomization::None);
	ToolbarBuilder.SetStyle(&FAppStyle::Get(), "AssetEditorToolbar");

	ToolbarBuilder.BeginSection("Asset");
	{
		// Use the modern `Icons.*` style names - they're registered as
		// 20x20 SVG brushes engine-wide, so Save / Browse / Refresh end up
		// the same size. The legacy `AssetEditor.SaveAsset` brush is a
		// 40x40 PNG meant for the big asset-editor toolbar, while
		// `SystemWideCommands.FindInContentBrowser` is a 20x20 - mixing
		// them gave inconsistent button sizes.
		ToolbarBuilder.AddToolBarButton(FUIAction(FExecuteAction::CreateSP(this, &SShotEditorRoot::OnSaveClicked),
				FCanExecuteAction::CreateSP(this, &SShotEditorRoot::CanSave)),
			NAME_None,
			LOCTEXT("ToolbarSave", "Save"),
			LOCTEXT("ToolbarSaveTooltip",
				"Save the asset that contains the active Shot's host. For a "
				"CompositionFramingNode host this is the parent CameraTypeAsset; "
				"for a Sequencer Section host it's the LevelSequence; for a "
				"ShotAsset host it's the asset itself."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Save"));

		ToolbarBuilder.AddToolBarButton(FUIAction(FExecuteAction::CreateSP(this, &SShotEditorRoot::OnBrowseClicked),
				FCanExecuteAction::CreateSP(this, &SShotEditorRoot::CanBrowse)),
			NAME_None,
			LOCTEXT("ToolbarBrowse", "Browse"),
			LOCTEXT("ToolbarBrowseTooltip",
				"Show the active host's containing asset in the Content Browser."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.BrowseContent"));
	}
	ToolbarBuilder.EndSection();

	ToolbarBuilder.BeginSection("Details");
	{
		ToolbarBuilder.AddToolBarButton(FUIAction(FExecuteAction::CreateSP(this, &SShotEditorRoot::OnRefreshClicked)),
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
	// Standard editor save path - runs source-control checkout / dirty
	// dialog as needed. `bCheckDirty=false` so we save unconditionally
	// (designer hit Save explicitly).
	TArray<UPackage*> Packages = { Pkg };
	FEditorFileUtils::PromptForCheckoutAndSave(Packages, /*bCheckDirty=*/false, /*bPromptToSave=*/false);
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

FReply SShotEditorRoot::OnResetViewportCameraClicked()
{
	if (Viewport.IsValid())
	{
		Viewport->ResetViewToShot();
	}
	return FReply::Handled();
}

bool SShotEditorRoot::CanResetViewportCamera() const
{
	using namespace ComposableCameraSystem::ShotEditorViewportToolbar;
	const EShotEditorMode ViewportMode = Viewport.IsValid()
		? Viewport->GetMode()
		: EShotEditorMode::Drag;
	return IsToolbarActionEnabled(EViewportToolbarAction::ResetView,
		Viewport.IsValid(),
		ActiveShot != nullptr && ActiveHost.IsValid(),
		ViewportMode);
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

ECheckBoxState SShotEditorRoot::GetViewportDiagnosticHudCheckState() const
{
	return Viewport.IsValid() && Viewport->GetShowDiagnosticHud()
		? ECheckBoxState::Checked
		: ECheckBoxState::Unchecked;
}

void SShotEditorRoot::OnViewportDiagnosticHudToggled(ECheckBoxState NewState)
{
	if (Viewport.IsValid())
	{
		Viewport->SetShowDiagnosticHud(NewState == ECheckBoxState::Checked);
	}
}

bool SShotEditorRoot::CanToggleViewportDiagnosticHud() const
{
	using namespace ComposableCameraSystem::ShotEditorViewportToolbar;
	const EShotEditorMode ViewportMode = Viewport.IsValid()
		? Viewport->GetMode()
		: EShotEditorMode::Drag;
	return IsToolbarActionEnabled(EViewportToolbarAction::ToggleDiagnosticHud,
		Viewport.IsValid(),
		ActiveShot != nullptr && ActiveHost.IsValid(),
		ViewportMode);
}

ECheckBoxState SShotEditorRoot::GetViewportCompositionGuidesCheckState() const
{
	return Viewport.IsValid() && Viewport->GetShowCompositionGuides()
		? ECheckBoxState::Checked
		: ECheckBoxState::Unchecked;
}

void SShotEditorRoot::OnViewportCompositionGuidesToggled(ECheckBoxState NewState)
{
	if (Viewport.IsValid())
	{
		Viewport->SetShowCompositionGuides(NewState == ECheckBoxState::Checked);
	}
}

bool SShotEditorRoot::CanToggleViewportCompositionGuides() const
{
	using namespace ComposableCameraSystem::ShotEditorViewportToolbar;
	const EShotEditorMode ViewportMode = Viewport.IsValid()
		? Viewport->GetMode()
		: EShotEditorMode::Drag;
	return IsToolbarActionEnabled(EViewportToolbarAction::ToggleCompositionGuides,
		Viewport.IsValid(),
		ActiveShot != nullptr && ActiveHost.IsValid(),
		ViewportMode);
}

#undef LOCTEXT_NAMESPACE
