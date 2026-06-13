// Copyright 2026 Sulley. All Rights Reserved.

// Polish T.1 - Editor viewport client automation tests.
//
// Scope: pure-logic helpers and constants that the recent polish
// items (E.5 wheel-Distance modifier-key acceleration, F.1 / F.2
// Roll / Distance clamps, reverse-solve status text, and viewport toolbar
// action gating)
// depend on. The full Slate-viewport interaction surface (drag
// transactions, pose drift across resizes, mode-switch UX) needs
// real `SApp` + tab manager mocking and is intentionally excluded
// - manual-PIE remains the validation path for that.
//
// What's covered here:
// 1. `ComputeWheelZoomFactor` (E.5) - 4 modifier-key states
// verified to produce the expected zoom factor (1.1 default,
// 1.5 Shift, 1.02 Ctrl, 1.1 Shift+Ctrl).
// 2. `FShotPlacement::MinDistance` / `MaxDistance` (F.2) - invariants
// against accidental drift (1cm floor, 100m soft cap, floor < ceil).
// 3. `ShotEditorReverseSolveStatusToText` - exhaustive enum coverage.
// 4. Shot Details mode visibility - pure mapping used by the Details
// customizations so inactive Shot rows collapse instead of lingering as
// disabled clutter.
// 5. Shot dropdown search filtering - pure string matching used by the
// custom menu widget.
// 6. Viewport floating toolbar action gates - pure action-state mapping
// used by the overlay controls.
// 7. Shot Editor layout collapse-state defaults - pure state resolution used
// by persisted viewport toolbar / Quick strip layout.
// 8. Shot Editor mode-switch prompts - pure mode-request classification used
// by the Free-mode exit status bar.
// 9. Shot Editor status bar - pure status priority / action mapping used by
// the top-bar-adjacent unified status strip.

#include "DataAssets/ComposableCameraShot.h"
#include "Customizations/ComposableCameraShotModeVisibility.h"
#include "Editors/ComposableCameraShotEditorViewportClient.h"
#include "Misc/AutomationTest.h"
#include "Widgets/ComposableCameraShotEditorLayoutState.h"
#include "Widgets/ComposableCameraShotEditorModeSwitchUtils.h"
#include "Widgets/ComposableCameraShotMenuUtils.h"
#include "Widgets/ComposableCameraShotEditorStatusBarUtils.h"
#include "Widgets/ComposableCameraShotViewportToolbarUtils.h"
#include "Widgets/SShotEditorViewport.h"

#define LOCTEXT_NAMESPACE "ComposableCameraShotEditorTests"

#if WITH_DEV_AUTOMATION_TESTS

// 1. ComputeWheelZoomFactor (E.5) 

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotEditorComputeWheelZoomFactorTest,
	"ComposableCameraSystem.ShotEditor.ComputeWheelZoomFactor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShotEditorComputeWheelZoomFactorTest::RunTest(const FString& /*Parameters*/)
{
	using ComposableCameraSystem::ShotEditorWheelMath::ComputeWheelZoomFactor;

	// Default (no modifiers) - 10% step.
	TestEqual(TEXT("Default factor is 1.10"),
		ComputeWheelZoomFactor(/*Shift=*/false, /*Ctrl=*/false), 1.1f);

	// Shift only - 50% step.
	TestEqual(TEXT("Shift factor is 1.50"),
		ComputeWheelZoomFactor(/*Shift=*/true, /*Ctrl=*/false), 1.5f);

	// Ctrl only - 2% step.
	TestEqual(TEXT("Ctrl factor is 1.02"),
		ComputeWheelZoomFactor(/*Shift=*/false, /*Ctrl=*/true), 1.02f);

	// Both held - falls back to default (refuses to compose 5x and 0.2x).
	TestEqual(TEXT("Shift+Ctrl falls back to default 1.10"),
		ComputeWheelZoomFactor(/*Shift=*/true, /*Ctrl=*/true), 1.1f);

	// Custom DefaultStep parameter - verifies the function honors caller
	// override rather than baking 0.1 in.
	const float Custom = ComputeWheelZoomFactor(false, false, /*DefaultStep=*/0.05f);
	TestEqual(TEXT("Custom DefaultStep=0.05 -> 1.05"), Custom, 1.05f);

	const float CustomShift = ComputeWheelZoomFactor(true, false, /*DefaultStep=*/0.05f);
	TestEqual(TEXT("Custom DefaultStep=0.05 + Shift -> 1.25 (5x step)"),
		CustomShift, 1.25f);

	return true;
}

// 2. FShotPlacement Distance clamp invariants (F.2) 

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotPlacementDistanceClampInvariantsTest,
	"ComposableCameraSystem.ShotEditor.DistanceClampInvariants",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShotPlacementDistanceClampInvariantsTest::RunTest(const FString& /*Parameters*/)
{
	// Floor: 1cm - matches the solver's pre-flight `Distance < 1cm` guard.
	TestEqual(TEXT("MinDistance is 1.0 cm"),
		FShotPlacement::MinDistance, 1.0f);

	// Ceiling: 100m - the F.2 sanity cap against typo / scroll-spam (1km
	// was the original cap, lowered after real-world usage feedback).
	TestEqual(TEXT("MaxDistance is 10000.0 cm (100m)"),
		FShotPlacement::MaxDistance, 10000.0f);

	// Floor < ceil - guards against an accidental swap during a future
	// edit. Without this, every clamped-write would either silently
	// snap to the floor or to the ceiling depending on which side won.
	TestTrue(TEXT("MinDistance < MaxDistance"),
		FShotPlacement::MinDistance < FShotPlacement::MaxDistance);

	// Default `Distance = 200.f` lands inside the range - defensive
	// check against changing the default in a way that triggers the
	// clamp on every new Shot.
	const FShotPlacement Default;
	TestTrue(TEXT("Default Distance is in [Min, Max]"),
		Default.Distance >= FShotPlacement::MinDistance
		&& Default.Distance <= FShotPlacement::MaxDistance);

	return true;
}

// 3. ShotEditorReverseSolveStatusToText exhaustiveness

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotEditorReverseSolveStatusToTextTest,
	"ComposableCameraSystem.ShotEditor.ReverseSolveStatusToText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShotEditorReverseSolveStatusToTextTest::RunTest(const FString& /*Parameters*/)
{
	TestTrue(TEXT("Ok returns empty FText"),
		ShotEditorReverseSolveStatusToText(EShotEditorReverseSolveStatus::Ok).IsEmpty());

	const EShotEditorReverseSolveStatus FailureCases[] = {
		EShotEditorReverseSolveStatus::NoActiveShot,
		EShotEditorReverseSolveStatus::EffectiveShotInvalid,
		EShotEditorReverseSolveStatus::PlacementAnchorUnresolvable,
		EShotEditorReverseSolveStatus::AimAnchorUnresolvable,
		EShotEditorReverseSolveStatus::PlacementAnchorBehindCamera,
	};
	TArray<FString> SeenTexts;
	SeenTexts.Reserve(UE_ARRAY_COUNT(FailureCases));
	for (EShotEditorReverseSolveStatus Status: FailureCases)
	{
		const FText AsText = ShotEditorReverseSolveStatusToText(Status);
		TestFalse(FString::Printf(TEXT("Status %d returns non-empty FText"),
				static_cast<int32>(Status)),
			AsText.IsEmpty());

		const FString AsString = AsText.ToString();
		TestFalse(FString::Printf(TEXT("Status %d text is unique among failure cases"),
				static_cast<int32>(Status)),
			SeenTexts.Contains(AsString));
		SeenTexts.Add(AsString);
	}

	return true;
}

// 4. Shot Details mode-sensitive visibility mapping

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotDetailsModeVisibilityTest,
	"ComposableCameraSystem.ShotEditor.DetailsModeVisibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShotDetailsModeVisibilityTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ComposableCameraSystem::ShotDetailsVisibility;

	TestTrue(TEXT("Placement Mode is always visible"),
		IsPlacementFieldVisible(EShotPlacementMode::FixedWorldPosition,
			EShotPlacementBasisFrame::World,
			GET_MEMBER_NAME_CHECKED(FShotPlacement, Mode)));
	TestTrue(TEXT("Placement anchor visible in AnchorOrbit"),
		IsPlacementFieldVisible(EShotPlacementMode::AnchorOrbit,
			EShotPlacementBasisFrame::World,
			GET_MEMBER_NAME_CHECKED(FShotPlacement, PlacementAnchor)));
	TestTrue(TEXT("Placement anchor remains visible in FixedWorldPosition for focus follow modes"),
		IsPlacementFieldVisible(EShotPlacementMode::FixedWorldPosition,
			EShotPlacementBasisFrame::World,
			GET_MEMBER_NAME_CHECKED(FShotPlacement, PlacementAnchor)));
	TestTrue(TEXT("Basis actor visible only when basis inherits from actor"),
		IsPlacementFieldVisible(EShotPlacementMode::AnchorOrbit,
			EShotPlacementBasisFrame::InheritFromActor,
			GET_MEMBER_NAME_CHECKED(FShotPlacement, BasisActorIndex)));
	TestFalse(TEXT("Basis actor hidden for world basis"),
		IsPlacementFieldVisible(EShotPlacementMode::AnchorOrbit,
			EShotPlacementBasisFrame::World,
			GET_MEMBER_NAME_CHECKED(FShotPlacement, BasisActorIndex)));
	TestTrue(TEXT("Placement screen position visible in AnchorAtScreen"),
		IsPlacementFieldVisible(EShotPlacementMode::AnchorAtScreen,
			EShotPlacementBasisFrame::World,
			GET_MEMBER_NAME_CHECKED(FShotPlacement, ScreenPosition)));
	TestFalse(TEXT("Placement screen position hidden in AnchorOrbit"),
		IsPlacementFieldVisible(EShotPlacementMode::AnchorOrbit,
			EShotPlacementBasisFrame::World,
			GET_MEMBER_NAME_CHECKED(FShotPlacement, ScreenPosition)));
	TestTrue(TEXT("Distance remains visible in AnchorAtScreen"),
		IsPlacementFieldVisible(EShotPlacementMode::AnchorAtScreen,
			EShotPlacementBasisFrame::World,
			GET_MEMBER_NAME_CHECKED(FShotPlacement, Distance)));
	TestTrue(TEXT("Distance speed remains visible in AnchorAtScreen"),
		IsPlacementFieldVisible(EShotPlacementMode::AnchorAtScreen,
			EShotPlacementBasisFrame::World,
			GET_MEMBER_NAME_CHECKED(FShotPlacement, DistanceSpeed)));
	TestTrue(TEXT("Placement zones visible in AnchorAtScreen"),
		IsPlacementFieldVisible(EShotPlacementMode::AnchorAtScreen,
			EShotPlacementBasisFrame::World,
			GET_MEMBER_NAME_CHECKED(FShotPlacement, PlacementZones)));
	TestTrue(TEXT("Fixed world position visible in FixedWorldPosition"),
		IsPlacementFieldVisible(EShotPlacementMode::FixedWorldPosition,
			EShotPlacementBasisFrame::World,
			GET_MEMBER_NAME_CHECKED(FShotPlacement, FixedWorldPosition)));
	TestFalse(TEXT("Distance hidden in FixedWorldPosition"),
		IsPlacementFieldVisible(EShotPlacementMode::FixedWorldPosition,
			EShotPlacementBasisFrame::World,
			GET_MEMBER_NAME_CHECKED(FShotPlacement, Distance)));

	TestTrue(TEXT("Aim mode is always visible"),
		IsAimFieldVisible(EShotAimMode::NoOp,
			GET_MEMBER_NAME_CHECKED(FShotAim, Mode)));
	TestTrue(TEXT("Aim anchor remains visible in NoOp for focus follow modes"),
		IsAimFieldVisible(EShotAimMode::NoOp,
			GET_MEMBER_NAME_CHECKED(FShotAim, AimAnchor)));
	TestFalse(TEXT("Aim screen position hidden in NoOp"),
		IsAimFieldVisible(EShotAimMode::NoOp,
			GET_MEMBER_NAME_CHECKED(FShotAim, ScreenPosition)));
	TestTrue(TEXT("Aim screen position visible in LookAtAnchor"),
		IsAimFieldVisible(EShotAimMode::LookAtAnchor,
			GET_MEMBER_NAME_CHECKED(FShotAim, ScreenPosition)));
	TestTrue(TEXT("Aim zones visible in LookAtAnchor"),
		IsAimFieldVisible(EShotAimMode::LookAtAnchor,
			GET_MEMBER_NAME_CHECKED(FShotAim, AimZones)));
	TestFalse(TEXT("Aim zones hidden in NoOp"),
		IsAimFieldVisible(EShotAimMode::NoOp,
			GET_MEMBER_NAME_CHECKED(FShotAim, AimZones)));

	TestTrue(TEXT("Manual FOV visible in manual mode"),
		IsLensFieldVisible(EShotFOVMode::Manual,
			GET_MEMBER_NAME_CHECKED(FShotLens, ManualFOV)));
	TestFalse(TEXT("Desired viewport fill hidden in manual mode"),
		IsLensFieldVisible(EShotFOVMode::Manual,
			GET_MEMBER_NAME_CHECKED(FShotLens, DesiredViewportFillRatio)));
	TestFalse(TEXT("Manual FOV hidden in solved mode"),
		IsLensFieldVisible(EShotFOVMode::SolvedFromBoundsFit,
			GET_MEMBER_NAME_CHECKED(FShotLens, ManualFOV)));
	TestTrue(TEXT("Desired viewport fill visible in solved mode"),
		IsLensFieldVisible(EShotFOVMode::SolvedFromBoundsFit,
			GET_MEMBER_NAME_CHECKED(FShotLens, DesiredViewportFillRatio)));
	TestTrue(TEXT("FOV clamp visible in solved mode"),
		IsLensFieldVisible(EShotFOVMode::SolvedFromBoundsFit,
			GET_MEMBER_NAME_CHECKED(FShotLens, FOVClamp)));
	TestTrue(TEXT("Aperture always visible"),
		IsLensFieldVisible(EShotFOVMode::SolvedFromBoundsFit,
			GET_MEMBER_NAME_CHECKED(FShotLens, Aperture)));
	TestTrue(TEXT("FOV speed always visible"),
		IsLensFieldVisible(EShotFOVMode::Manual,
			GET_MEMBER_NAME_CHECKED(FShotLens, FOVSpeed)));

	TestTrue(TEXT("Manual focus distance visible in manual mode"),
		IsFocusFieldVisible(EShotFocusMode::Manual,
			GET_MEMBER_NAME_CHECKED(FShotFocus, ManualDistance)));
	TestFalse(TEXT("Focus anchor hidden in manual mode"),
		IsFocusFieldVisible(EShotFocusMode::Manual,
			GET_MEMBER_NAME_CHECKED(FShotFocus, FocusAnchor)));
	TestFalse(TEXT("Manual focus distance hidden when following aim anchor"),
		IsFocusFieldVisible(EShotFocusMode::FollowAimAnchor,
			GET_MEMBER_NAME_CHECKED(FShotFocus, ManualDistance)));
	TestTrue(TEXT("Custom focus anchor visible in custom-anchor mode"),
		IsFocusFieldVisible(EShotFocusMode::FollowCustomAnchor,
			GET_MEMBER_NAME_CHECKED(FShotFocus, FocusAnchor)));

	TestTrue(TEXT("Anchor target index visible in SingleTarget"),
		IsAnchorFieldVisible(EShotAnchorMode::SingleTarget,
			GET_MEMBER_NAME_CHECKED(FComposableCameraAnchorSpec, TargetIndex)));
	TestFalse(TEXT("Anchor target index hidden in WeightedWorldCentroid"),
		IsAnchorFieldVisible(EShotAnchorMode::WeightedWorldCentroid,
			GET_MEMBER_NAME_CHECKED(FComposableCameraAnchorSpec, TargetIndex)));
	TestTrue(TEXT("Weighted targets visible in WeightedWorldCentroid"),
		IsAnchorFieldVisible(EShotAnchorMode::WeightedWorldCentroid,
			GET_MEMBER_NAME_CHECKED(FComposableCameraAnchorSpec, WeightedTargets)));
	TestTrue(TEXT("World position visible in FixedWorldPosition"),
		IsAnchorFieldVisible(EShotAnchorMode::FixedWorldPosition,
			GET_MEMBER_NAME_CHECKED(FComposableCameraAnchorSpec, WorldPosition)));

	return true;
}

// 5. Shot dropdown search filtering

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotEditorMenuSearchFilterTest,
	"ComposableCameraSystem.ShotEditor.MenuSearchFilter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShotEditorMenuSearchFilterTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ComposableCameraSystem::ShotEditorMenu;

	TestTrue(TEXT("Empty filter matches"),
		MatchesSearchFilter(TEXT("Camera Track"), TEXT("CloseUp"), TEXT("(1.00s - 2.00s, Row 0)"), TEXT("")));
	TestTrue(TEXT("Title match is case-insensitive"),
		MatchesSearchFilter(TEXT("Camera Track"), TEXT("CloseUp"), TEXT("(1.00s - 2.00s, Row 0)"), TEXT("close")));
	TestTrue(TEXT("Track match works"),
		MatchesSearchFilter(TEXT("Boss Track"), TEXT("Inline (2)"), TEXT("(1.00s - 2.00s, Row 0)"), TEXT("boss")));
	TestTrue(TEXT("Time suffix match works"),
		MatchesSearchFilter(TEXT("Camera Track"), TEXT("Inline (2)"), TEXT("(12.50s - 14.00s, Row 3)"), TEXT("row 3")));
	TestTrue(TEXT("Multiple tokens can match different fields"),
		MatchesSearchFilter(TEXT("Boss Track"), TEXT("CloseUp"), TEXT("(12.50s - 14.00s, Row 3)"), TEXT("boss close")));
	TestFalse(TEXT("Missing token rejects entry"),
		MatchesSearchFilter(TEXT("Boss Track"), TEXT("CloseUp"), TEXT("(12.50s - 14.00s, Row 3)"), TEXT("boss wide")));

	return true;
}

// 6. Viewport floating toolbar action-state mapping

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotEditorViewportToolbarActionStateTest,
	"ComposableCameraSystem.ShotEditor.ViewportToolbarActionState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShotEditorViewportToolbarActionStateTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ComposableCameraSystem::ShotEditorViewportToolbar;

	TestTrue(TEXT("HUD toggle enabled with active Shot"),
		IsToolbarActionEnabled(EViewportToolbarAction::ToggleDiagnosticHud,
			/*ViewportAvailable=*/true,
			/*HasActiveShot=*/true,
			EShotEditorMode::Drag));
	TestFalse(TEXT("Guides toggle disabled without viewport"),
		IsToolbarActionEnabled(EViewportToolbarAction::ToggleCompositionGuides,
			/*ViewportAvailable=*/false,
			/*HasActiveShot=*/true,
			EShotEditorMode::Free));
	TestTrue(TEXT("Reset enabled only in Free mode"),
		IsToolbarActionEnabled(EViewportToolbarAction::ResetView,
			/*ViewportAvailable=*/true,
			/*HasActiveShot=*/true,
			EShotEditorMode::Free));
	TestFalse(TEXT("Reset disabled in Drag mode"),
		IsToolbarActionEnabled(EViewportToolbarAction::ResetView,
			/*ViewportAvailable=*/true,
			/*HasActiveShot=*/true,
			EShotEditorMode::Drag));
	TestTrue(TEXT("Expanded toolbar controls are visible when not collapsed"),
		ShouldShowToolbarExpandedControls(/*ToolbarCollapsed=*/false));
	TestFalse(TEXT("Expanded toolbar controls are hidden when collapsed"),
		ShouldShowToolbarExpandedControls(/*ToolbarCollapsed=*/true));

	return true;
}

// 7. Shot Editor layout collapse-state defaults

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotEditorLayoutStateTest,
	"ComposableCameraSystem.ShotEditor.LayoutState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShotEditorLayoutStateTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ComposableCameraSystem::ShotEditorLayout;

	const FShotEditorLayoutState DefaultState =
		ResolveLayoutState(TOptional<bool>(), TOptional<bool>());
	TestFalse(TEXT("Toolbar defaults to expanded"),
		DefaultState.bViewportToolbarCollapsed);
	TestTrue(TEXT("Quick controls default to collapsed"),
		DefaultState.bQuickControlsCollapsed);

	const FShotEditorLayoutState PersistedState =
		ResolveLayoutState(TOptional<bool>(true), TOptional<bool>(false));
	TestTrue(TEXT("Persisted toolbar collapse state overrides default"),
		PersistedState.bViewportToolbarCollapsed);
	TestFalse(TEXT("Persisted Quick collapse state overrides default"),
		PersistedState.bQuickControlsCollapsed);

	return true;
}

// 8. Shot Editor mode-switch prompts

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotEditorModeSwitchPromptTest,
	"ComposableCameraSystem.ShotEditor.ModeSwitchPrompt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShotEditorModeSwitchPromptTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ComposableCameraSystem::ShotEditorModeSwitch;

	TestEqual(TEXT("Free to Drag opens Free-exit status"),
		ClassifyModeRequest(EShotEditorMode::Free, EShotEditorMode::Drag),
		EModeRequestHandling::ShowFreeExitStatus);
	TestEqual(TEXT("Free to Lock opens Free-exit status"),
		ClassifyModeRequest(EShotEditorMode::Free, EShotEditorMode::Lock),
		EModeRequestHandling::ShowFreeExitStatus);
	TestEqual(TEXT("Drag to Lock applies immediately"),
		ClassifyModeRequest(EShotEditorMode::Drag, EShotEditorMode::Lock),
		EModeRequestHandling::ApplyImmediately);
	TestEqual(TEXT("Same mode ignored"),
		ClassifyModeRequest(EShotEditorMode::Free, EShotEditorMode::Free),
		EModeRequestHandling::Ignore);
	TestTrue(TEXT("Pending Free-exit status is visible only while viewport remains in Free"),
		ShouldShowFreeExitStatus(/*HasPendingFreeExitMode=*/true, EShotEditorMode::Free));
	TestFalse(TEXT("Pending status hides after viewport leaves Free"),
		ShouldShowFreeExitStatus(/*HasPendingFreeExitMode=*/true, EShotEditorMode::Drag));
	TestFalse(TEXT("Status hides when no pending request exists"),
		ShouldShowFreeExitStatus(/*HasPendingFreeExitMode=*/false, EShotEditorMode::Free));

	return true;
}

// 9. Shot Editor status bar

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotEditorStatusBarStateTest,
	"ComposableCameraSystem.ShotEditor.StatusBarState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShotEditorStatusBarStateTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ComposableCameraSystem::ShotEditorStatusBar;

	const FShotEditorStatusBarState CleanActiveShot =
		ResolveStatusBarState(/*bHasActiveShot=*/true,
			/*bHostValid=*/true,
			/*bHasPendingFreeExitMode=*/false,
			EShotEditorMode::Drag);
	TestEqual(TEXT("Clean active Shot hides status bar"),
		CleanActiveShot.Kind, EShotEditorStatusBarKind::Hidden);
	TestEqual(TEXT("Clean active Shot exposes no status actions"),
		CleanActiveShot.Actions, EShotEditorStatusBarActions::None);

	const FShotEditorStatusBarState NoActiveShot =
		ResolveStatusBarState(/*bHasActiveShot=*/false,
			/*bHostValid=*/false,
			/*bHasPendingFreeExitMode=*/false,
			EShotEditorMode::Drag);
	TestEqual(TEXT("No active Shot shows info status"),
		NoActiveShot.Kind, EShotEditorStatusBarKind::Info);
	TestEqual(TEXT("No active Shot exposes no status actions"),
		NoActiveShot.Actions, EShotEditorStatusBarActions::None);

	const FShotEditorStatusBarState StaleHost =
		ResolveStatusBarState(/*bHasActiveShot=*/true,
			/*bHostValid=*/false,
			/*bHasPendingFreeExitMode=*/false,
			EShotEditorMode::Drag);
	TestEqual(TEXT("Stale host shows warning status"),
		StaleHost.Kind, EShotEditorStatusBarKind::Warning);
	TestEqual(TEXT("Stale host exposes no status actions"),
		StaleHost.Actions, EShotEditorStatusBarActions::None);

	const FShotEditorStatusBarState PendingFreeExit =
		ResolveStatusBarState(/*bHasActiveShot=*/true,
			/*bHostValid=*/true,
			/*bHasPendingFreeExitMode=*/true,
			EShotEditorMode::Free);
	TestEqual(TEXT("Pending Free exit shows warning status"),
		PendingFreeExit.Kind, EShotEditorStatusBarKind::Warning);
	TestEqual(TEXT("Pending Free exit exposes Free-exit actions"),
		PendingFreeExit.Actions, EShotEditorStatusBarActions::FreeExit);

	const FShotEditorStatusBarState PendingFreeExitWithStaleHost =
		ResolveStatusBarState(/*bHasActiveShot=*/true,
			/*bHostValid=*/false,
			/*bHasPendingFreeExitMode=*/true,
			EShotEditorMode::Free);
	TestEqual(TEXT("Stale host has priority over pending Free exit"),
		PendingFreeExitWithStaleHost.Kind, EShotEditorStatusBarKind::Warning);
	TestEqual(TEXT("Stale host suppresses Free-exit actions"),
		PendingFreeExitWithStaleHost.Actions, EShotEditorStatusBarActions::None);

	const FShotEditorStatusBarState PendingFreeExitWithoutShot =
		ResolveStatusBarState(/*bHasActiveShot=*/false,
			/*bHostValid=*/false,
			/*bHasPendingFreeExitMode=*/true,
			EShotEditorMode::Free);
	TestEqual(TEXT("No active Shot has priority over pending Free exit"),
		PendingFreeExitWithoutShot.Kind, EShotEditorStatusBarKind::Info);
	TestEqual(TEXT("No active Shot suppresses Free-exit actions"),
		PendingFreeExitWithoutShot.Actions, EShotEditorStatusBarActions::None);

	const FShotEditorStatusBarState PendingAfterLeavingFree =
		ResolveStatusBarState(/*bHasActiveShot=*/true,
			/*bHostValid=*/true,
			/*bHasPendingFreeExitMode=*/true,
			EShotEditorMode::Drag);
	TestEqual(TEXT("Pending Free exit hides after viewport leaves Free"),
		PendingAfterLeavingFree.Kind, EShotEditorStatusBarKind::Hidden);
	TestEqual(TEXT("Hidden pending state exposes no status actions"),
		PendingAfterLeavingFree.Actions, EShotEditorStatusBarActions::None);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

#undef LOCTEXT_NAMESPACE
