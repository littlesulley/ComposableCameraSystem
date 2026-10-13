// Copyright 2026 Sulley. All Rights Reserved.
#include "CineCameraComponent.h"
#include "DataAssets/ComposableCameraShotAsset.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Editors/ComposableCameraShotAuthoringSession.h"
#include "Editors/ComposableCameraShotEditorViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/ComposableCameraShotEditorLayoutState.h"
#include "Widgets/ComposableCameraShotEditorStatusBarUtils.h"
#include "Widgets/SShotEditorAuthoringPanel.h"
#include "Widgets/SShotEditorParameterPanel.h"
#include "Widgets/SShotEditorRoot.h"
#include "Widgets/SShotEditorPreviewLayout.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Layout/SExpandableArea.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
TSharedPtr<SWidget> FindWorkspaceWidget(const TSharedRef<SWidget>& Widget, const FName Tag)
{
	if (Widget->GetTag() == Tag) return Widget;
	FChildren* Children = Widget->GetChildren();
	for (int32 Index = 0; Index < Children->Num(); ++Index)
		if (const auto Found = FindWorkspaceWidget(Children->GetChildAt(Index), Tag)) return Found;
	return nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotEditorActionableDiagnosticsTest,
	"ComposableCameraSystem.ShotEditor.ActionableDiagnostics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotEditorActionableDiagnosticsTest::RunTest(const FString&)
{
	if (!GEditor || GEditor->IsTransactionActive() || !FSlateApplication::IsInitialized()) return false;
	TStrongObjectPtr<UComposableCameraShotAsset> Asset(NewObject<UComposableCameraShotAsset>());
	auto& Shot = Asset->Shot;
	Shot.Targets.SetNum(1);
	Shot.Placement.BasisFrame = EShotPlacementBasisFrame::World;
	Shot.Placement.PlacementAnchor.TargetIndex = 0;
	Shot.Aim.Mode = EShotAimMode::NoOp;
	Shot.Focus.Mode = EShotFocusMode::Manual;
	Shot.Lens.FOVMode = EShotFOVMode::Manual;
	Shot.Lens.ManualFOV = 43.f;
	const auto Session = MakeShared<FComposableCameraShotAuthoringSession>();
	Session->Bind(&Shot, Asset.Get());
	Session->bUseLevelWorld = false;
	const auto Viewport = SNew(SShotEditorViewport);
	Viewport->SetAuthoringSession(Session);
	const auto Client = Viewport->GetClientForTesting();
	const auto Solve = [&]()
	{
		Viewport->SetActiveShot(&Shot, Asset.Get());
		Client->RunSolverAndDriveCamera(0.f);
		return Viewport->GetPreviewDiagnostic();
	};
	using EIssue = EShotEditorDiagnosticIssue;
	const FVector Previous = Client->GetViewLocation();
	auto Diagnostic = Solve();
	TestEqual(TEXT("Missing used Subject identified"), Diagnostic.Issue, EIssue::FollowAnchor);
	TestEqual(TEXT("Exact Subject A index reported"), Diagnostic.TargetIndex, 0);
	TestTrue(TEXT("Standalone Compose reports held pose"), Diagnostic.bPoseHeld);
	TestTrue(TEXT("Failed solve keeps view position"), Client->GetViewLocation().Equals(Previous));
	TestEqual(TEXT("Failure still updates manual lens"), Client->ViewFOV, 43.f);
	TestFalse(TEXT("Failure has actionable text"), Diagnostic.ToText().IsEmpty());
	Client->SetMode(EShotEditorMode::Free);
	TestFalse(TEXT("Inspect does not claim solver controls inspected pose"), Solve().bPoseHeld);
	Client->SetMode(EShotEditorMode::Drag);
	Shot.Placement.Mode = EShotPlacementMode::FixedWorldPosition;
	Shot.Placement.FixedWorldPosition = FVector(-100, 0, 0);
	TestEqual(TEXT("Unused invalid Subject is not a failure"), Solve().Issue, EIssue::None);
	Shot.Aim.Mode = EShotAimMode::LookAtAnchor;
	Shot.Aim.AimAnchor.TargetIndex = 6;
	TestEqual(TEXT("Out-of-range Aim slot distinguished"), Solve().TargetIndex, 6);
	TestEqual(TEXT("Aim routes to its own page"), Solve().Page, EShotEditorDiagnosticPage::Aim);
	Shot.Aim.AimAnchor.Mode = EShotAnchorMode::FixedWorldPosition;
	Shot.Placement.Mode = EShotPlacementMode::AnchorAtScreen;
	Shot.Placement.PlacementAnchor = Shot.Aim.AimAnchor;
	TestEqual(TEXT("First-frame coincident anchor failure explained"), Solve().Issue, EIssue::CoincidentAnchors);
	Shot.Placement.Mode = EShotPlacementMode::AnchorOrbit;
	Shot.Placement.BasisFrame = EShotPlacementBasisFrame::InheritFromActor;
	Shot.Placement.BasisActorIndex = 7;
	TestEqual(TEXT("Basis fallback is visible even when pose solves"), Solve().Issue, EIssue::BasisFallback);
	Shot.Placement.Mode = EShotPlacementMode::FixedWorldPosition;
	Shot.Focus.Mode = EShotFocusMode::FollowCustomAnchor;
	TestEqual(TEXT("Focus fallback names its anchor"), Solve().Issue, EIssue::FocusAnchor);
	Shot.Focus.Mode = EShotFocusMode::Manual;
	Shot.Lens.FOVMode = EShotFOVMode::SolvedFromBoundsFit;
	TestEqual(TEXT("No contributing bounds explained"), Solve().Issue, EIssue::NoFramingBounds);
	AActor* Target = Client->GetWorld()->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Preview test subject spawned"), Target)) return false;
	ON_SCOPE_EXIT { Target->Destroy(); };
	Shot.Targets[0].Target.Actor = Target;
	Shot.Targets[0].BoundsShape = EShotTargetBoundsShape::ManualExtent;
	Shot.Targets[0].ManualBoundsExtent = FVector(50);
	Shot.Lens.FOVClamp = FFloatInterval(12.f, 40.f);
	Shot.Lens.DesiredViewportFillRatio = .2f;
	Diagnostic = Solve();
	TestEqual(TEXT("Actual lens solve reaches upper clamp"), Diagnostic.Issue, EIssue::FOVMaximum);
	TestEqual(TEXT("Lens clamp uses native parameter path"), FString(Diagnostic.PropertyPath()), FString(TEXT("Lens.FOVClamp")));
	TestEqual(TEXT("Lower clamp classified"), DiagnoseShotPreview(Shot, true, false, 12.f).Issue, EIssue::FOVMinimum);
	Shot.Placement.Mode = EShotPlacementMode::AnchorOrbit;
	Shot.Placement.BasisFrame = EShotPlacementBasisFrame::World;
	Shot.Placement.PlacementAnchor.Mode = EShotAnchorMode::WeightedWorldCentroid;
	auto& Missing = Shot.Placement.PlacementAnchor.WeightedTargets.AddDefaulted_GetRef();
	Missing.TargetIndex = 9;
	auto& Resolved = Shot.Placement.PlacementAnchor.WeightedTargets.AddDefaulted_GetRef();
	Resolved.TargetIndex = 0;
	TestEqual(TEXT("Partially resolvable weighted anchor is not a failure"), DiagnoseShotPreview(Shot, true, false, 30.f).Issue, EIssue::None);
	Shot.Placement.Mode = EShotPlacementMode::FixedWorldPosition;
	TestEqual(TEXT("Native final-view FOV is not treated as a source clamp"), DiagnoseShotPreview(Shot, true, false, 40.f, true).Issue, EIssue::None);
	TStrongObjectPtr<UCineCameraComponent> Output(NewObject<UCineCameraComponent>());
	Client->OutputCamera = Output.Get();
	Client->bUseLevelWorld = true;
	TestEqual(TEXT("Production native output path clears standalone clamp"), Solve().Issue, EIssue::None);
	Client->OutputCamera.Reset();
	Client->bUseLevelWorld = false;

	const auto Panel = SNew(SShotEditorAuthoringPanel).Session(Session);
	TArray<TSharedPtr<SShotEditorParameterPanel>> ParameterPanels;
	Panel->GetEditParameterPanelsForTesting(ParameterPanels);
	const auto LensPanel = ParameterPanels[2];
	const uint32 Rebuilds = LensPanel->GetRebuildCountForTesting();
	int32 Changes = 0;
	Session->OnChanged = [&](bool) { ++Changes; };
	ON_SCOPE_EXIT { Session->OnChanged = nullptr; };
	const float Distance = Shot.Placement.Distance;
	TestTrue(TEXT("Diagnostic navigation accepted"), Panel->RevealDiagnostic(Diagnostic));
	TestEqual(TEXT("Navigation retains parameter controls"), LensPanel->GetRebuildCountForTesting(), Rebuilds);
	const auto ClampRow = FindWorkspaceWidget(LensPanel.ToSharedRef(), FName(TEXT("Lens.FOVClamp")));
	TestTrue(TEXT("Related native lens row remains reachable"), ClampRow.IsValid());
	TestEqual(TEXT("Navigation does not commit Shot data"), Changes, 0);
	TestEqual(TEXT("Navigation leaves follow distance intact"), Shot.Placement.Distance, Distance);
	Session->BeginEdit(NSLOCTEXT("ShotEditorTests", "DiagnosticGesture", "Test gesture"));
	TestFalse(TEXT("Navigation cannot interrupt numeric gesture"), Panel->RevealDiagnostic(Diagnostic));
	Session->EndEdit();

	using namespace ComposableCameraSystem::ShotEditorStatusBar;
	TestEqual(TEXT("Diagnostic activates status action"), ResolveStatusBarState(true, true, false, EShotEditorMode::Drag, true).Actions, EShotEditorStatusBarActions::Diagnostic);
	TestEqual(TEXT("Free-exit actions keep priority"), ResolveStatusBarState(true, true, true, EShotEditorMode::Free, true).Actions, EShotEditorStatusBarActions::FreeExit);
	TestEqual(TEXT("Destroyed host suppresses diagnostic action"), ResolveStatusBarState(true, false, false, EShotEditorMode::Drag, true).Actions, EShotEditorStatusBarActions::None);
	Viewport->SetActiveShot(nullptr, nullptr);
	TestFalse(TEXT("Source teardown clears stale diagnostic"), Viewport->GetPreviewDiagnostic().HasIssue());
	const auto Root = SNew(SShotEditorRoot).PersistViewState(false);
	Shot.Lens.FOVMode = EShotFOVMode::Manual;
	Shot.Aim.Mode = EShotAimMode::NoOp;
	Root->SetActiveShot(&Shot, Asset.Get());
	const auto RootClient = Root->Viewport->GetClientForTesting();
	RootClient->RunSolverAndDriveCamera(0.f);
	Root->Tick(FGeometry(), 0., 0.f);
	TestEqual(TEXT("Clean preview hides diagnostic bar"), Root->GetStatusBarVisibility(), EVisibility::Collapsed);
	Root->AuthoringSession->BeginEdit(NSLOCTEXT("ShotEditorTests", "DiagnosticGeometry", "Test status geometry"));
	Shot.Placement.Mode = EShotPlacementMode::AnchorOrbit;
	Shot.Placement.PlacementAnchor.Mode = EShotAnchorMode::SingleTarget;
	Shot.Placement.PlacementAnchor.TargetIndex = 9;
	RootClient->RunSolverAndDriveCamera(0.f);
	Root->Tick(FGeometry(), 0., 0.f);
	TestEqual(TEXT("New warning cannot resize preview mid-gesture"), Root->GetStatusBarVisibility(), EVisibility::Collapsed);
	Root->AuthoringSession->EndEdit();
	Root->Tick(FGeometry(), 0., 0.f);
	TestEqual(TEXT("Warning publishes after gesture ends"), Root->GetStatusBarVisibility(), EVisibility::Visible);
	Root->AuthoringSession->BeginEdit(NSLOCTEXT("ShotEditorTests", "DiagnosticRecovery", "Test status recovery"));
	Shot.Placement.Mode = EShotPlacementMode::FixedWorldPosition;
	RootClient->RunSolverAndDriveCamera(0.f);
	Root->Tick(FGeometry(), 0., 0.f);
	TestEqual(TEXT("Clearing warning also preserves gesture geometry"), Root->GetStatusBarVisibility(), EVisibility::Visible);
	TestFalse(TEXT("Stale diagnostic action disabled while preview recovers"), Root->CanRevealDiagnostic());
	Root->AuthoringSession->EndEdit();
	Root->Tick(FGeometry(), 0., 0.f);
	TestEqual(TEXT("Recovered status clears between gestures"), Root->GetStatusBarVisibility(), EVisibility::Collapsed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotEditorWorkspacePersistenceTest,
	"ComposableCameraSystem.ShotEditor.WorkspacePersistence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotEditorWorkspacePersistenceTest::RunTest(const FString&)
{
	if (!GConfig || !GEditor || GEditor->IsTransactionActive() || !FSlateApplication::IsInitialized()) return false;
	using namespace ComposableCameraSystem::ShotEditorLayout;
	const FString Ini = FString::Printf(TEXT("CCSShotWorkspaceTest_%s.ini"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	// This cache entry is removed without Flush. User preferences and disk remain untouched.
	GConfig->Add(Ini, FConfigFile{});
	ON_SCOPE_EXIT { GConfig->Remove(Ini); };
	auto State = ReadLayoutState(*GConfig, Ini);
	TestEqual(TEXT("Missing settings keep .4 split"), State.AuthoringFraction, .4f);
	State.AuthoringFraction = .57f;
	State.bShowHud = true;
	State.bShowGuides = false;
	State.Page = 1;
	State.EditSection = 4;
	WriteLayoutState(*GConfig, Ini, State);
	const auto Reloaded = ReadLayoutState(*GConfig, Ini);
	TestEqual(TEXT("Split coefficient survives config reload"), Reloaded.AuthoringFraction, .57f);
	TestTrue(TEXT("HUD preference restored"), Reloaded.bShowHud);
	TestFalse(TEXT("Guides preference restored"), Reloaded.bShowGuides);
	const auto Splitter = SNew(SShotEditorPreviewLayout).AuthoringFraction(Reloaded.AuthoringFraction)
		.OnAuthoringFractionChanged_Lambda([&](float Value) { State.AuthoringFraction = Value; WriteLayoutState(*GConfig, Ini, State); })
		.Authoring()[SNullWidget::NullWidget].Preview()[SNullWidget::NullWidget];
	TestEqual(TEXT("Restored split reaches native left slot"), Splitter->SlotAt(0).GetSizeValue(), .57f);
	Splitter->SlotAt(0).OnSlotResized().Execute(.49f);
	TestEqual(TEXT("Native resize callback persists coefficient"), ReadLayoutState(*GConfig, Ini).AuthoringFraction, .49f);
	TestTrue(TEXT("Native right slot keeps complementary coefficient"), FMath::IsNearlyEqual(Splitter->SlotAt(1).GetSizeValue(), .51f));
	GConfig->SetFloat(ConfigSection, TEXT("AuthoringFraction"), -1.f, Ini);
	GConfig->SetInt(ConfigSection, TEXT("Page"), 999, Ini);
	GConfig->SetInt(ConfigSection, TEXT("EditSection"), -1, Ini);
	const auto Invalid = ReadLayoutState(*GConfig, Ini);
	TestEqual(TEXT("Invalid splitter preference uses safe default"), Invalid.AuthoringFraction, .4f);
	TestEqual(TEXT("Invalid task restores Edit"), Invalid.Page, 1);
	TestEqual(TEXT("Invalid section restores Follow"), Invalid.EditSection, 0);
	WriteLayoutState(*GConfig, Ini, State);
	TStrongObjectPtr<UComposableCameraShotAsset> Asset(NewObject<UComposableCameraShotAsset>());
	Asset->Shot.Targets.SetNum(1);
	const auto Session = MakeShared<FComposableCameraShotAuthoringSession>();
	Session->Bind(&Asset->Shot, Asset.Get());
	{
		const auto Panel = SNew(SShotEditorAuthoringPanel).Session(Session).PersistViewState(true).ViewStateIni(Ini);
		TestTrue(TEXT("Recreated Subjects page restores its guide selection"), Session->bShowSubjectGuide);
		const auto Subject = FindWorkspaceWidget(Panel, FName(TEXT("ShotEditor.Edit.Targets[0].SubjectGroup")));
		if (!TestTrue(TEXT("Production Subject expander exists"), Subject.IsValid())) return false;
		StaticCastSharedPtr<SExpandableArea>(Subject)->SetExpanded(false);
		const auto Component = FindWorkspaceWidget(Panel, FName(TEXT("ShotEditor.Edit.Targets[0].ComponentGroup")));
		if (!TestTrue(TEXT("Production component expander exists"), Component.IsValid())) return false;
		StaticCastSharedPtr<SExpandableArea>(Component)->SetExpanded(true);
		const auto Preview = FindWorkspaceWidget(Panel->GetSubjectPanelForTesting(0).ToSharedRef(), FName(TEXT("Targets[0].PreviewGroup")));
		if (!TestTrue(TEXT("Native parameter subgroup exists"), Preview.IsValid())) return false;
		StaticCastSharedPtr<SExpandableArea>(Preview)->SetExpanded(true);
	}
	{
		const auto Panel = SNew(SShotEditorAuthoringPanel).Session(Session).PersistViewState(true).ViewStateIni(Ini);
		const auto Subject = FindWorkspaceWidget(Panel, FName(TEXT("ShotEditor.Edit.Targets[0].SubjectGroup")));
		const auto Component = FindWorkspaceWidget(Panel, FName(TEXT("ShotEditor.Edit.Targets[0].ComponentGroup")));
		if (!Subject || !Component) return false;
		TestFalse(TEXT("Whole-Subject fold restored on recreation"), StaticCastSharedPtr<SExpandableArea>(Subject)->IsExpanded());
		TestTrue(TEXT("Component fold restored independently"), StaticCastSharedPtr<SExpandableArea>(Component)->IsExpanded());
		const auto SubjectPanel = Panel->GetSubjectPanelForTesting(0);
		const auto Preview = FindWorkspaceWidget(SubjectPanel.ToSharedRef(), FName(TEXT("Targets[0].PreviewGroup")));
		if (!Preview) return false;
		TestTrue(TEXT("Native parameter fold restored on recreation"), StaticCastSharedPtr<SExpandableArea>(Preview)->IsExpanded());
		Panel->SelectLookAtForTesting(true);
	}
	{
		const auto Panel = SNew(SShotEditorAuthoringPanel).Session(Session).PersistViewState(true).ViewStateIni(Ini);
		TestTrue(TEXT("Last Edit section restored after recreation"), Session->bShowLookAtGuide);
		TestFalse(TEXT("Restored Aim does not leave stale Subject guides"), Session->bShowSubjectGuide);
	}
	return true;
}
#endif
