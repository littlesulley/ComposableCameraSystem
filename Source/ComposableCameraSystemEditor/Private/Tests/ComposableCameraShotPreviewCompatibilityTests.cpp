// Copyright 2026 Sulley. All Rights Reserved.
#include "Editor.h"
#include "Editors/ComposableCameraShotAuthoringSession.h"
#include "Editors/ComposableCameraShotEditorViewportClient.h"
#include "InputKeyEventArgs.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "MovieScene.h"
#include "MovieScene/MovieSceneComposableCameraShotSection.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "Widgets/SShotEditorAuthoringPanel.h"
#include "Widgets/SShotEditorParameterPanel.h"
#include "Widgets/SShotEditorViewport.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotEditorPreviewCompatibilityTest,
	"ComposableCameraSystem.ShotEditor.PreviewCompatibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotEditorPreviewCompatibilityTest::RunTest(const FString&)
{
	if (!GEditor || GEditor->IsTransactionActive()) return false;
	TStrongObjectPtr<UMovieScene> Scene(NewObject<UMovieScene>());
	TStrongObjectPtr<UMovieSceneComposableCameraShotSection> Section(
		NewObject<UMovieSceneComposableCameraShotSection>(Scene.Get(), NAME_None, RF_Transactional));
	Section->Source = EComposableCameraShotSource::Inline;
	FComposableCameraShot& Shot = Section->InlineShot;
	Shot.Placement.Mode = EShotPlacementMode::AnchorOrbit;
	Shot.Placement.BasisFrame = EShotPlacementBasisFrame::World;
	Shot.Placement.PlacementAnchor.Mode = EShotAnchorMode::FixedWorldPosition;
	Shot.Placement.PlacementAnchor.WorldPosition = FVector(100, 20, 80);
	Shot.Placement.Distance = 500.f;
	Shot.Placement.LocalCameraDirection = FVector2D(180, 10);
	Shot.Aim.AimAnchor = Shot.Placement.PlacementAnchor;
	Shot.Lens.FOVMode = EShotFOVMode::Manual;
	Shot.Lens.ManualFOV = 50.f;
	Shot.Focus.Mode = EShotFocusMode::Manual;
	Shot.Focus.ManualDistance = 345.f;
	Shot.Lens.Aperture = 3.f;
	Shot.Roll = 17.f;
	auto Session = MakeShared<FComposableCameraShotAuthoringSession>();
	Session->Bind(&Shot, Section.Get()); Session->bUseLevelWorld = false;
	const auto Panel = SNew(SShotEditorAuthoringPanel).Session(Session);
	const auto Follow = Panel->GetFollowPanelForTesting();
	const auto Preview = SNew(SShotEditorViewport);
	Preview->SetAuthoringSession(Session); Preview->SetActiveShot(&Shot, Section.Get());
	const auto Client = Preview->GetClientForTesting();
	if (!TestTrue(TEXT("Production preview has native viewport"), Client.IsValid() && Client->Viewport)) return false;
	int32 Commits = 0, Modified = 0, Structural = 0;
	Session->OnChanged = [&](bool bStructural)
	{
		if (bStructural) { ++Structural; Panel->Refresh(); Follow->RefreshSource(); }
	};
	const auto ChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda(
		[&](UObject* Object, FPropertyChangedEvent&) { if (Object == Section.Get()) ++Commits; });
	const auto ModifiedHandle = FCoreUObjectDelegates::OnObjectModified.AddLambda(
		[&](UObject* Object) { if (Object == Section.Get()) ++Modified; });
	ON_SCOPE_EXIT
	{
		Client->EndDrag(); Client->EndRollDrag();
		Session->OnChanged = nullptr;
		FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ChangedHandle);
		FCoreUObjectDelegates::OnObjectModified.Remove(ModifiedHandle);
	};
	const auto Mouse = [&](FKey Key, EInputEvent Event)
	{
		return Client->InputKey(FInputKeyEventArgs(Client->Viewport, FInputDeviceId::CreateFromInternalId(0),
			Key, Event, 1.f, false, 0u));
	};
	for (const EShotEditorMode Mode : { EShotEditorMode::Drag, EShotEditorMode::Free })
	{
		// Undo/Redo legitimately regenerates handles; measure persistence only across this gesture.
		Panel->Tick(FGeometry(), 0., 0.f); Follow->Tick(FGeometry(), 0., 0.f);
		const uint32 BeforeRows = Follow->GetRebuildCountForTesting();
		Client->SetMode(Mode); Commits = 0; Modified = 0; Structural = 0;
		const float BeforeRoll = Shot.Roll;
		Client->StartRollDrag();
		TestTrue(TEXT("Compose and Inspect retain authored Roll gesture"), Client->IsEditingGesture());
		const int32 StartX = Client->RollDragLastMouseX;
		Client->ApplyRollDrag(StartX + 10); Client->ApplyRollDrag(StartX + 20);
		TestEqual(TEXT("Roll retains native half-degree per pixel behavior"), Shot.Roll, BeforeRoll + 10.f);
		TestEqual(TEXT("Live Roll sends no host commit"), Commits, 0);
		TestFalse(TEXT("Captured Roll cannot nest wheel transaction"), Client->TryAdjustDistanceFromMouseWheel(true));
		TestTrue(TEXT("Native mouse release closes Roll"), Mouse(EKeys::RightMouseButton, IE_Released));
		TestFalse(TEXT("Release clears gesture"), Client->IsEditingGesture());
		TestEqual(TEXT("Roll emits one scalar commit"), Commits, 1);
		TestEqual(TEXT("Roll sends no OnObjectModified"), Modified, 0);
		TestEqual(TEXT("Roll does not refresh panel structure"), Structural, 0);
		Follow->Tick(FGeometry(), 0., 0.f);
		TestEqual(TEXT("Native parameter rows survive Roll"), Follow->GetRebuildCountForTesting(), BeforeRows);
		TestTrue(TEXT("One Undo restores entire Roll gesture"), GEditor->UndoTransaction());
		TestEqual(TEXT("Roll undo value restored"), Shot.Roll, BeforeRoll);
		TestTrue(TEXT("One Redo restores entire Roll gesture"), GEditor->RedoTransaction());
		TestEqual(TEXT("Roll redo value restored"), Shot.Roll, BeforeRoll + 10.f);
	}
	Client->SetMode(EShotEditorMode::Drag);
	const float BeforeDistance = Shot.Placement.Distance;
	TestTrue(TEXT("Compose native wheel authors distance"), Mouse(EKeys::MouseScrollUp, IE_Pressed));
	TestTrue(TEXT("Wheel moves camera toward anchor"), Shot.Placement.Distance < BeforeDistance);
	TestTrue(TEXT("Wheel is one Undo step"), GEditor->UndoTransaction());
	TestEqual(TEXT("Wheel Undo restores distance"), Shot.Placement.Distance, BeforeDistance);

	// Both world selections retain detached-shot solving when no output CineCamera is bound.
	// Real scene rendering, native orbit/pan/dolly and keyboard routing need an attached editor window.
	for (const bool bLevelWorld : { false, true })
	{
		Client->bUseLevelWorld = bLevelWorld;
		Client->SetMode(EShotEditorMode::Drag); Client->bHasCachedPriorPose = false;
		Client->RunSolverAndDriveCamera(0.f);
		const FVector SolvedPosition = Client->GetViewLocation();
		TestTrue(TEXT("Compose solves fixed world anchor"), !SolvedPosition.IsZero());
		Client->SetMode(EShotEditorMode::Free);
		const FVector InspectedPosition(400, 500, 600);
		Client->SetViewLocation(InspectedPosition); Client->SetViewRotation(FRotator(5, 25, 0));
		Client->RunSolverAndDriveCamera(0.f);
		TestTrue(TEXT("Inspect preserves user camera position"), Client->GetViewLocation().Equals(InspectedPosition));
		TestTrue(TEXT("Inspect preserves yaw and pitch"), FMath::IsNearlyEqual(Client->GetViewRotation().Yaw, 25.)
			&& FMath::IsNearlyEqual(Client->GetViewRotation().Pitch, 5.));
		TestEqual(TEXT("Inspect keeps authored Roll live"), float(Client->GetViewRotation().Roll), Shot.Roll);
		TestEqual(TEXT("Inspect keeps FOV live"), Client->ViewFOV, Shot.Lens.ManualFOV);
		TestEqual(TEXT("Inspect keeps focus live"), Client->CachedFocusDistance, Shot.Focus.ManualDistance);
		TestEqual(TEXT("Inspect keeps aperture live"), Client->CachedAperture, Shot.Lens.Aperture);
		Client->ResetViewToShot();
		TestTrue(TEXT("Reset restores solved position"), Client->GetViewLocation().Equals(SolvedPosition, .01));
		TestTrue(TEXT("Reset retains Inspect mode"), Client->GetMode() == EShotEditorMode::Free);
	}
	const FVector SavedPosition(200, 350, 450);
	Client->SetViewLocation(SavedPosition);
	FRotator SavedRotation = (Shot.Aim.AimAnchor.WorldPosition - SavedPosition).Rotation();
	SavedRotation.Roll = Shot.Roll;
	Client->SetViewRotation(SavedRotation); Client->ViewFOV = 66.f;
	TestTrue(TEXT("Inspect can save camera pose back to Shot"), Client->ReverseSolveCurrentCameraToShot());
	Client->SetMode(EShotEditorMode::Drag); Client->bHasCachedPriorPose = false;
	Client->RunSolverAndDriveCamera(0.f);
	TestTrue(TEXT("Reverse-solved orbit reproduces inspected position"), Client->GetViewLocation().Equals(SavedPosition, .01));
	TestEqual(TEXT("Reverse-solve saves manual FOV"), Shot.Lens.ManualFOV, 66.f);
	Client->SetShowDiagnosticHud(false); Client->SetShowCompositionGuides(false);
	TestFalse(TEXT("HUD retains display toggle"), Client->GetShowDiagnosticHud());
	TestFalse(TEXT("Guides retain display toggle"), Client->GetShowCompositionGuides());
	Client->SetShowDiagnosticHud(true); Client->SetShowCompositionGuides(true);

	const float LockedRoll = Shot.Roll;
	const float LockedDistance = Shot.Placement.Distance;
	for (const bool bSequenceReadOnly : { false, true })
	{
		Section->SetIsLocked(!bSequenceReadOnly); Scene->SetReadOnly(bSequenceReadOnly);
		for (const EShotEditorMode Mode : { EShotEditorMode::Drag, EShotEditorMode::Free })
		{
			Client->SetMode(Mode); Client->StartRollDrag(); Client->ApplyRollDrag(999);
			TestFalse(TEXT("Every mode rejects Roll on locked source"), Client->IsEditingGesture());
			TestEqual(TEXT("Locked Roll stays unchanged"), Shot.Roll, LockedRoll);
			TestFalse(TEXT("Locked source rejects distance authoring"), Client->TryAdjustDistanceFromMouseWheel(true));
			TestFalse(TEXT("Locked source rejects reverse-solve"), Client->ReverseSolveCurrentCameraToShot());
			if (Mode != EShotEditorMode::Free)
				TestTrue(TEXT("Read-only Compose consumes wheel without writing"), Mouse(EKeys::MouseScrollUp, IE_Pressed));
			TestEqual(TEXT("Locked distance stays unchanged"), Shot.Placement.Distance, LockedDistance);
		}
		Section->SetIsLocked(false); Scene->SetReadOnly(false);
	}
	Client->SetMode(EShotEditorMode::Free); Client->StartRollDrag();
	Client->ApplyRollDrag(Client->RollDragLastMouseX + 20);
	const float BeforeLock = Shot.Roll;
	Section->SetIsLocked(true); Client->ApplyRollDrag(Client->RollDragLastMouseX + 20);
	TestFalse(TEXT("Lock during capture closes writer"), Client->IsEditingGesture());
	TestEqual(TEXT("Lock during capture prevents next write"), Shot.Roll, BeforeLock);
	Section->SetIsLocked(false);
	TestTrue(TEXT("Partial gesture remains one Undo"), GEditor->UndoTransaction());
	TestEqual(TEXT("Partial gesture Undo restores previous Roll"), Shot.Roll, LockedRoll);
	Client->SetMode(EShotEditorMode::Drag); Client->StartRollDrag();
	Client->ApplyRollDrag(Client->RollDragLastMouseX + 10);
	Section->SetIsLocked(true);
	TestTrue(TEXT("Release immediately after lock is handled"), Mouse(EKeys::RightMouseButton, IE_Released));
	TestFalse(TEXT("Read-only guard does not trap capture"), Client->IsEditingGesture());
	Section->SetIsLocked(false);
	Client->StartRollDrag(); Client->ActiveHost.Reset();
	Client->ApplyRollDrag(Client->RollDragLastMouseX + 20);
	TestFalse(TEXT("Lost host cancels captured Roll"), Client->IsEditingGesture());
	return true;
}
#endif
