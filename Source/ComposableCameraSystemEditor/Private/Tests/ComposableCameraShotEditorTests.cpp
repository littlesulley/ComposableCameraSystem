// Copyright 2026 Sulley. All Rights Reserved.

// Polish T.1 - Editor viewport client automation tests.
//
// Scope: pure-logic helpers and constants that the recent polish
// items (E.5 wheel-Distance modifier-key acceleration, F.1 / F.2
// Roll / Distance clamps, E.3 reverse-solve diagnose-and-report)
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
// 3. `ShotEditorReverseSolveStatusToText` (E.3) - exhaustive enum
// coverage. `Ok` returns empty; every other enum value returns
// non-empty designer-actionable text; no two failure cases share
// the same text (catches "I added a status but forgot the
// switch arm" regressions).

#include "DataAssets/ComposableCameraShot.h"
#include "Editors/ComposableCameraShotEditorViewportClient.h"
#include "Misc/AutomationTest.h"
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

// 3. ShotEditorReverseSolveStatusToText exhaustiveness (E.3) 

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotEditorReverseSolveStatusToTextTest,
	"ComposableCameraSystem.ShotEditor.ReverseSolveStatusToText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShotEditorReverseSolveStatusToTextTest::RunTest(const FString& /*Parameters*/)
{
	// Ok is the success state - designer never sees the text rendered;
	// the function returns empty so the caller can pick a separate
	// success-path body.
	TestTrue(TEXT("Ok returns empty FText"),
		ShotEditorReverseSolveStatusToText(EShotEditorReverseSolveStatus::Ok).IsEmpty());

	// Every failure case must produce non-empty designer-actionable
	// text - empty here would render the dialog body's "Reason: " line
	// blank. The exhaustive list catches "I added a status but forgot
	// to extend the switch in `ShotEditorReverseSolveStatusToText`".
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

		// Uniqueness check - same text across two different statuses
		// would defeat the whole point of having separate enum values.
		const FString AsString = AsText.ToString();
		TestFalse(FString::Printf(TEXT("Status %d text is unique among failure cases"),
				static_cast<int32>(Status)),
			SeenTexts.Contains(AsString));
		SeenTexts.Add(AsString);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

#undef LOCTEXT_NAMESPACE
