// Copyright 2026 Sulley. All Rights Reserved.
#include "Customizations/ComposableCameraShotModeVisibility.h"
#include "DataAssets/ComposableCameraShotAsset.h"
#include "Editors/ComposableCameraShotAuthoringSession.h"
#include "Misc/AutomationTest.h"
#include "Misc/NotifyHook.h"
#include "Misc/ScopeExit.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"
#include "Widgets/Input/SButton.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "TickableEditorObject.h"
#include "Widgets/SShotEditorAuthoringPanel.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/SShotEditorParameterPanel.h"
#include "Widgets/SShotEditorViewport.h"
#include "Widgets/SShotEditorRoot.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/ComposableCameraShotViewportDisplayUtils.h"
#include "Widgets/ComposableCameraShotViewportOverlayUtils.h"
#include "Math/ComposableCameraShotSolver.h"
#include "MovieScene.h"
#include "MovieScene/MovieSceneComposableCameraShotSection.h"
#include "Editors/ComposableCameraShotEditorViewportClient.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/SShotEditorPreviewFrame.h"
#include "Widgets/SShotEditorPreviewLayout.h"
#include "Widgets/ComposableCameraShotEditorStyle.h"
#include "Widgets/SNullWidget.h"
#include "Layout/ArrangedChildren.h"

#include <type_traits>

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotEditorCameraModesTest,
	"ComposableCameraSystem.ShotEditor.CameraModes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotEditorCameraModesTest::RunTest(const FString&)
{
	if (!GEditor || GEditor->IsTransactionActive() || !FSlateApplication::IsInitialized()) return false;
	const auto Root = SNew(SShotEditorRoot);
	TSharedPtr<SSegmentedControl<EShotEditorMode>> Modes;
	TFunction<void(const TSharedRef<SWidget>&)> FindModes;
	FindModes = [&](const TSharedRef<SWidget>& Widget)
	{
		if (Widget->GetTag() == FName(TEXT("ShotEditor.ModeSelector")))
		{
			Modes = StaticCastSharedRef<SSegmentedControl<EShotEditorMode>>(Widget);
			return;
		}
		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Index < Children->Num() && !Modes; ++Index) FindModes(Children->GetChildAt(Index));
	};
	FindModes(Root);
	if (!TestTrue(TEXT("Production camera mode control exists"), Modes.IsValid())) return false;
	int32 Buttons = 0;
	TArray<FText> Labels;
	TFunction<void(const TSharedRef<SWidget>&)> ReadModes;
	ReadModes = [&](const TSharedRef<SWidget>& Widget)
	{
		if (Widget->GetType() == FName(TEXT("SCheckBox"))) ++Buttons;
		if (Widget->GetType() == FName(TEXT("STextBlock"))) Labels.Add(StaticCastSharedRef<STextBlock>(Widget)->GetText());
		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Index < Children->Num(); ++Index) ReadModes(Children->GetChildAt(Index));
	};
	ReadModes(Modes.ToSharedRef());
	TestEqual(TEXT("Camera mode control has only two buttons"), Buttons, 2);
	if (!TestEqual(TEXT("Camera mode control has only two labels"), Labels.Num(), 2)) return false;
	TestTrue(TEXT("Compose remains first mode"), Labels[0].EqualTo(NSLOCTEXT("SShotEditorRoot", "ModeDrag", "Compose")));
	TestTrue(TEXT("Inspect remains second mode"), Labels[1].EqualTo(NSLOCTEXT("SShotEditorRoot", "ModeFree", "Inspect")));
	const auto Press = [&](FKey Key) { return Root->OnKeyDown(FGeometry(), FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0)).IsEventHandled(); };
	TestTrue(TEXT("Default mode remains Compose"), Modes->GetValue() == EShotEditorMode::Drag);
	TestFalse(TEXT("Removed Preview shortcut is unhandled in Compose"), Press(EKeys::Three));
	TestTrue(TEXT("Removed shortcut keeps Compose"), Modes->GetValue() == EShotEditorMode::Drag);
	TestTrue(TEXT("Inspect shortcut remains handled"), Press(EKeys::Two));
	TestTrue(TEXT("Inspect shortcut enters Inspect"), Modes->GetValue() == EShotEditorMode::Free);
	TestFalse(TEXT("Removed Preview shortcut is unhandled in Inspect"), Press(EKeys::Three));
	TestTrue(TEXT("Removed shortcut keeps Inspect"), Modes->GetValue() == EShotEditorMode::Free);
	TestTrue(TEXT("Compose shortcut still queues the existing exit flow"), Press(EKeys::One));
	TestTrue(TEXT("Inspect exit waits for Save / Discard / Stay"), Modes->GetValue() == EShotEditorMode::Free);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotEditorFollowLookAtParametersTest,
	"ComposableCameraSystem.ShotEditor.FollowLookAtParameters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotEditorFollowLookAtParametersTest::RunTest(const FString&)
{
	using namespace ComposableCameraSystem::ShotDetailsVisibility;
	TStrongObjectPtr<UComposableCameraShotAsset> Asset(NewObject<UComposableCameraShotAsset>());
	auto Session = MakeShared<FComposableCameraShotAuthoringSession>();
	Asset->Shot.Targets.SetNum(2);
	Asset->Shot.Placement.ScreenPosition = FVector2D(.12, -.08);
	Asset->Shot.Placement.FixedWorldPosition = FVector(120, 340, 560);
	Session->Bind(&Asset->Shot, Asset.Get());
	const TSharedRef<SShotEditorParameterPanel> Follow = SNew(SShotEditorParameterPanel)
		.Session(Session).Section(EShotEditorParameterSection::Follow);
	const TSharedRef<SShotEditorParameterPanel> LookAt = SNew(SShotEditorParameterPanel)
		.Session(Session).Section(EShotEditorParameterSection::LookAt);

	// Assert actual generated rows, including custom target-index rows. A hand-picked
	// Orbit-only widget list cannot satisfy all reflected fields in all three modes.
	for (EShotPlacementMode Mode : { EShotPlacementMode::AnchorOrbit, EShotPlacementMode::AnchorAtScreen, EShotPlacementMode::FixedWorldPosition })
	{
		for (EShotPlacementBasisFrame Basis : { EShotPlacementBasisFrame::World, EShotPlacementBasisFrame::InheritFromActor, EShotPlacementBasisFrame::TwoTargetAxis })
		{
			Asset->Shot.Placement.Mode = Mode;
			Asset->Shot.Placement.BasisFrame = Basis;
			Follow->RefreshForTesting();
			TSet<FName> Names;
			Follow->GetVisiblePropertyNamesForTesting(Names);
			for (TFieldIterator<FProperty> Property(FShotPlacement::StaticStruct()); Property; ++Property)
			{
				const FName Name = Property->GetFName();
				const bool bExpected = IsPlacementFieldVisible(Mode, Basis, Name);
				TestEqual(FString::Printf(TEXT("Follow mode %d basis %d exposes %s exactly when used"), int32(Mode), int32(Basis), *Name.ToString()), Names.Contains(Name), bExpected);
			}
		}
	}
	for (EShotAimMode Mode : { EShotAimMode::LookAtAnchor, EShotAimMode::NoOp })
	{
		Asset->Shot.Aim.Mode = Mode;
		LookAt->RefreshForTesting();
		TSet<FName> Names;
		LookAt->GetVisiblePropertyNamesForTesting(Names);
		for (TFieldIterator<FProperty> Property(FShotAim::StaticStruct()); Property; ++Property)
		{
			const FName Name = Property->GetFName();
			TestEqual(FString::Printf(TEXT("Aim mode %d exposes %s exactly when used"), int32(Mode), *Name.ToString()), Names.Contains(Name), IsAimFieldVisible(Mode, Name));
		}
	}
	TestTrue(TEXT("Hidden screen coordinates survive mode switches"), Asset->Shot.Placement.ScreenPosition.Equals(FVector2D(.12, -.08)));
	TestTrue(TEXT("Hidden world coordinates survive mode switches"), Asset->Shot.Placement.FixedWorldPosition.Equals(FVector(120, 340, 560)));

	TStrongObjectPtr<UComposableCameraShotAsset> Other(NewObject<UComposableCameraShotAsset>());
	Other->Shot.Placement.Mode = EShotPlacementMode::FixedWorldPosition;
	Session->Bind(&Other->Shot, Other.Get());
	Follow->RefreshForTesting();
	TSet<FName> Names;
	Follow->GetVisiblePropertyNamesForTesting(Names);
	TestTrue(TEXT("Source rebind exposes new source world position"), Names.Contains(GET_MEMBER_NAME_CHECKED(FShotPlacement, FixedWorldPosition)));
	TestFalse(TEXT("Source rebind removes previous source orbit direction"), Names.Contains(GET_MEMBER_NAME_CHECKED(FShotPlacement, LocalCameraDirection)));
	Session->Bind(nullptr, nullptr);
	Follow->RefreshForTesting();
	Names.Reset();
	Follow->GetVisiblePropertyNamesForTesting(Names);
	TestEqual(TEXT("Closing source clears generated parameter rows"), Names.Num(), 0);
	const uint32 ClearCount = Follow->GetRebuildCountForTesting();
	Follow->Tick(FGeometry(), 0.0, 0.f);
	Follow->Tick(FGeometry(), 0.0, 0.f);
	TestEqual(TEXT("Closed source does not rebuild the empty layout every frame"), Follow->GetRebuildCountForTesting(), ClearCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotEditorLensFocusSubjectParametersTest,
	"ComposableCameraSystem.ShotEditor.LensFocusSubjectParameters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotEditorLensFocusSubjectParametersTest::RunTest(const FString&)
{
	using namespace ComposableCameraSystem::ShotDetailsVisibility;
	TStrongObjectPtr<UComposableCameraShotAsset> Asset(NewObject<UComposableCameraShotAsset>());
	Asset->Shot.Targets.SetNum(2);
	Asset->Shot.Lens.FOVClamp = FFloatInterval(23.f, 91.f);
	Asset->Shot.Focus.ManualDistance = 345.f;
	Asset->Shot.Targets[1].Target.Offset = FVector(12, 34, 56);
	Asset->Shot.Targets[1].BoundsContributionWeight = .35f;
	auto Session = MakeShared<FComposableCameraShotAuthoringSession>();
	Session->Bind(&Asset->Shot, Asset.Get());
	const auto LensFocus = SNew(SShotEditorParameterPanel).Session(Session).Section(EShotEditorParameterSection::LensFocus);
	const auto Subject = SNew(SShotEditorParameterPanel).Session(Session).Section(EShotEditorParameterSection::Subject).SubjectIndex(1);
	const auto Motion = SNew(SShotEditorParameterPanel).Session(Session).Section(EShotEditorParameterSection::Motion);

	// Inspect paths emitted while constructing actual widgets, not just fields
	// available somewhere in the generator. Omitting a UI row fails this test.
	for (EShotFOVMode LensMode : { EShotFOVMode::Manual, EShotFOVMode::SolvedFromBoundsFit })
		for (EShotFocusMode FocusMode : { EShotFocusMode::Manual, EShotFocusMode::FollowPlacementAnchor, EShotFocusMode::FollowAimAnchor, EShotFocusMode::FollowCustomAnchor })
		{
			Asset->Shot.Lens.FOVMode = LensMode;
			Asset->Shot.Focus.Mode = FocusMode;
			LensFocus->RefreshForTesting();
			TSet<FString> Paths;
			LensFocus->GetRenderedPropertyPathsForTesting(Paths);
			for (TFieldIterator<FProperty> Property(FShotLens::StaticStruct()); Property; ++Property)
			{
				const FString Path = TEXT("Lens.") + Property->GetName();
				TestEqual(Path + TEXT(" visibility in lens mode ") + FString::FromInt(int32(LensMode)), Paths.Contains(Path), IsLensFieldVisible(LensMode, Property->GetFName()));
			}
			for (TFieldIterator<FProperty> Property(FShotFocus::StaticStruct()); Property; ++Property)
			{
				const FString Path = TEXT("Focus.") + Property->GetName();
				TestEqual(Path + TEXT(" visibility in focus mode ") + FString::FromInt(int32(FocusMode)), Paths.Contains(Path), IsFocusFieldVisible(FocusMode, Property->GetFName()));
			}
		}
	Asset->Shot.Focus.Mode = EShotFocusMode::FollowCustomAnchor;
	for (EShotAnchorMode AnchorMode : { EShotAnchorMode::SingleTarget, EShotAnchorMode::WeightedWorldCentroid, EShotAnchorMode::FixedWorldPosition })
	{
		Asset->Shot.Focus.FocusAnchor.Mode = AnchorMode;
		LensFocus->RefreshForTesting();
		TSet<FString> Paths;
		LensFocus->GetRenderedPropertyPathsForTesting(Paths);
		for (TFieldIterator<FProperty> Property(FComposableCameraAnchorSpec::StaticStruct()); Property; ++Property)
		{
			const FString Path = TEXT("Focus.FocusAnchor.") + Property->GetName();
			TestEqual(Path + TEXT(" visibility in custom anchor mode ") + FString::FromInt(int32(AnchorMode)), Paths.Contains(Path), IsAnchorFieldVisible(AnchorMode, Property->GetFName()));
		}
	}
	for (EShotTargetBoundsShape Shape : { EShotTargetBoundsShape::None, EShotTargetBoundsShape::ManualExtent, EShotTargetBoundsShape::AutoFromComponentBounds })
		for (EBoundsCachePolicy Policy : { EBoundsCachePolicy::StaticSnapshot, EBoundsCachePolicy::Periodic, EBoundsCachePolicy::Live })
		{
			Asset->Shot.Targets[1].BoundsShape = Shape;
			Asset->Shot.Targets[1].BoundsCachePolicy = Policy;
			Subject->RefreshForTesting();
			TSet<FString> Paths;
			Subject->GetRenderedPropertyPathsForTesting(Paths);
			for (TFieldIterator<FProperty> Property(FComposableCameraShotTarget::StaticStruct()); Property; ++Property)
			{
				if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(FComposableCameraShotTarget, Target)) continue;
				const FString Path = TEXT("Targets[1].") + Property->GetName();
				TestEqual(Path + TEXT(" excludes caches and exposes editable bounds"), Paths.Contains(Path), Property->HasAnyPropertyFlags(CPF_Edit));
			}
			for (TFieldIterator<FProperty> Property(FComposableCameraTargetInfo::StaticStruct()); Property; ++Property)
			{
				const FName Name = Property->GetFName();
				const bool bSemanticIdentity = Name == GET_MEMBER_NAME_CHECKED(FComposableCameraTargetInfo, Actor)
					|| Name == GET_MEMBER_NAME_CHECKED(FComposableCameraTargetInfo, ComponentName)
					|| Name == GET_MEMBER_NAME_CHECKED(FComposableCameraTargetInfo, BoneName);
				const FString Path = TEXT("Targets[1].Target.") + Property->GetName();
				TestEqual(Path + TEXT(" covered without duplicating identity controls"), Paths.Contains(Path), !bSemanticIdentity && Property->HasAnyPropertyFlags(CPF_Edit));
			}
		}
	for (EShotPlacementMode PlacementMode : { EShotPlacementMode::AnchorOrbit, EShotPlacementMode::AnchorAtScreen, EShotPlacementMode::FixedWorldPosition })
		for (EShotAimMode AimMode : { EShotAimMode::LookAtAnchor, EShotAimMode::NoOp })
		{
			Asset->Shot.Placement.Mode = PlacementMode;
			Asset->Shot.Aim.Mode = AimMode;
			Motion->RefreshForTesting();
			TSet<FString> Paths;
			Motion->GetRenderedPropertyPathsForTesting(Paths);
			TestEqual(TEXT("Motion exposes DistanceSpeed only when used"), Paths.Contains(TEXT("Placement.DistanceSpeed")), PlacementMode != EShotPlacementMode::FixedWorldPosition);
			TestTrue(TEXT("Motion exposes FOVSpeed"), Paths.Contains(TEXT("Lens.FOVSpeed")));
			TestTrue(TEXT("Motion exposes RollSpeed"), Paths.Contains(TEXT("RollSpeed")));
			for (const TCHAR* Name : { TEXT("bEnabled"), TEXT("HorizontalSpeed"), TEXT("VerticalSpeed") })
			{
				TestEqual(FString(TEXT("Follow screen response ")) + Name, Paths.Contains(FString(TEXT("Placement.PlacementZones.")) + Name), PlacementMode == EShotPlacementMode::AnchorAtScreen);
				TestEqual(FString(TEXT("Aim screen response ")) + Name, Paths.Contains(FString(TEXT("Aim.AimZones.")) + Name), AimMode == EShotAimMode::LookAtAnchor);
			}
		}
	TestEqual(TEXT("Lens mode switches preserve hidden FOV lower bound"), Asset->Shot.Lens.FOVClamp.Min, 23.f);
	TestEqual(TEXT("Lens mode switches preserve hidden FOV upper bound"), Asset->Shot.Lens.FOVClamp.Max, 91.f);
	TestEqual(TEXT("Focus mode switches preserve manual distance"), Asset->Shot.Focus.ManualDistance, 345.f);
	TestEqual(TEXT("Subject refresh preserves fractional bounds weight"), Asset->Shot.Targets[1].BoundsContributionWeight, .35f);
	TestTrue(TEXT("Subject refresh preserves pivot offset"), Asset->Shot.Targets[1].Target.Offset.Equals(FVector(12, 34, 56)));
	TestTrue(TEXT("Native subject weight handle accepts a fractional value"), Subject->SetSubjectFloatForTesting(GET_MEMBER_NAME_CHECKED(FComposableCameraShotTarget, BoundsContributionWeight), .65f));
	TestEqual(TEXT("Subject index 1 handle writes the real second subject"), Asset->Shot.Targets[1].BoundsContributionWeight, .65f);
	TestEqual(TEXT("Subject index 1 handle leaves the first subject unchanged"), Asset->Shot.Targets[0].BoundsContributionWeight, 1.f);

	Asset->Shot.Targets.SetNum(1);
	Subject->RefreshForTesting();
	TSet<FString> Paths;
	Subject->GetRenderedPropertyPathsForTesting(Paths);
	TestEqual(TEXT("Removed subject index clears stale controls"), Paths.Num(), 0);
	Session->Bind(nullptr, nullptr);
	LensFocus->RefreshForTesting();
	LensFocus->GetRenderedPropertyPathsForTesting(Paths);
	TestEqual(TEXT("Closing source clears lens/focus controls"), Paths.Num(), 0);
	const uint32 ClearCount = LensFocus->GetRebuildCountForTesting();
	LensFocus->Tick(FGeometry(), 0.0, 0.f);
	TestEqual(TEXT("Closed lens/focus source stays idle"), LensFocus->GetRebuildCountForTesting(), ClearCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotEditorPersistentParameterPagesTest,
	"ComposableCameraSystem.ShotEditor.PersistentParameterPages",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotEditorPersistentParameterPagesTest::RunTest(const FString&)
{
	if (!GEditor || GEditor->IsTransactionActive()) return false;
	TStrongObjectPtr<UComposableCameraShotAsset> Asset(NewObject<UComposableCameraShotAsset>(GetTransientPackage(), NAME_None, RF_Transactional));
	Asset->Shot.Targets.SetNum(2);
	auto Session = MakeShared<FComposableCameraShotAuthoringSession>();
	Session->Bind(&Asset->Shot, Asset.Get());
	struct FNativeShotNotifyHook : FNotifyHook
	{
		TSharedPtr<FComposableCameraShotAuthoringSession> Session;
		virtual ~FNativeShotNotifyHook() = default;
		virtual void NotifyPreChange(FProperty*) override { SaveToTransactionBuffer(Session->GetHost(), false); }
		virtual void NotifyPostChange(const FPropertyChangedEvent& Event, FProperty*) override { Session->NotifyNativePropertyChange(Event); }
	} Hook;
	static_assert(std::has_virtual_destructor_v<FNativeShotNotifyHook>, "Polymorphic Shot test hook requires a virtual destructor.");
	Hook.Session = Session;
	const auto Panel = SNew(SShotEditorAuthoringPanel).Session(Session).NotifyHook(&Hook);
	const auto Follow = Panel->GetFollowPanelForTesting();
	const TSharedRef<SWidget> ParameterBody = Follow->GetChildren()->GetChildAt(0);
	const SWidget* EditPage = Panel->GetEditPageForTesting();
	const uint32 InitialRows = Follow->GetRebuildCountForTesting();
	int32 StructuralChanges = 0;
	Session->OnChanged = [&](bool bStructural)
	{
		if (bStructural) { ++StructuralChanges; Panel->Refresh(); }
	};
	int32 HostNotifications = 0;
	const FDelegateHandle HostChanged = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda(
		[&](UObject* Object, FPropertyChangedEvent&) { if (Object == Asset.Get()) ++HostNotifications; });
	FPropertyChangedEvent Interactive(FindFProperty<FProperty>(FShotPlacement::StaticStruct(), GET_MEMBER_NAME_CHECKED(FShotPlacement, Distance)),
		EPropertyChangeType::Interactive | EPropertyChangeType::ValueSet);
	Session->NotifyNativePropertyChange(Interactive);
	TestEqual(TEXT("Interactive bit never broadcasts host invalidation"), HostNotifications, 0);
	const float Before = Asset->Shot.Placement.Distance;
	{
		FScopedTransaction Transaction(FText::FromString(TEXT("Native Shot Parameter Commit")));
		TestTrue(TEXT("Native value widget writes through the actual NotifyHook"), Follow->SetFloatForTesting(GET_MEMBER_NAME_CHECKED(FShotPlacement, Distance), Before + 123.f));
	}
	Panel->Tick(FGeometry(), 0.0, 0.f);
	Follow->Tick(FGeometry(), 0.0, 0.f);
	TestEqual(TEXT("ValueSet, forwarded outer commit and Finalized event do not request structural refresh"), StructuralChanges, 0);
	TestEqual(TEXT("Scalar commit retains parameter widgets"), Follow->GetRebuildCountForTesting(), InitialRows);
	TestTrue(TEXT("Scalar commit retains the unified parameter body"), Follow->GetChildren()->GetChildAt(0) == ParameterBody);
	TestEqual(TEXT("Native commit broadcasts exactly one host event"), HostNotifications, 1);
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(HostChanged);
	TestEqual(TEXT("Pages built once"), Panel->GetPageBuildCountForTesting(), 1u);
	TestTrue(TEXT("Edit scroll/switcher survives scalar commit"), Panel->GetEditPageForTesting() == EditPage);
	TestEqual(TEXT("Actual authoring value changed"), Asset->Shot.Placement.Distance, Before + 123.f);
	TestTrue(TEXT("Native commit can undo"), GEditor->UndoTransaction());
	TestEqual(TEXT("Undo restores real source"), Asset->Shot.Placement.Distance, Before);
	TestTrue(TEXT("Undo requests handle refresh"), StructuralChanges > 0);
	Panel->Tick(FGeometry(), 0.0, 0.f);
	TestTrue(TEXT("History refresh retains page identity"), Panel->GetEditPageForTesting() == EditPage);
	TestTrue(TEXT("Native commit can redo"), GEditor->RedoTransaction());
	TestEqual(TEXT("Redo restores final value"), Asset->Shot.Placement.Distance, Before + 123.f);
	FString Reason;
	TestTrue(TEXT("Structural append still succeeds"), Session->AddTarget(nullptr, Reason));
	Panel->Tick(FGeometry(), 0.0, 0.f);
	TestEqual(TEXT("Subject list updated"), Asset->Shot.Targets.Num(), 3);
	TestEqual(TEXT("Append/history never recreate task pages"), Panel->GetPageBuildCountForTesting(), 1u);
	TestTrue(TEXT("Append retains page identity"), Panel->GetEditPageForTesting() == EditPage);
	// A mode discriminator changes only retained-row visibility, even without a root/source refresh.
	Asset->Shot.Placement.Mode = EShotPlacementMode::AnchorOrbit;
	Asset->Shot.Placement.BasisFrame = EShotPlacementBasisFrame::World;
	Follow->Tick(FGeometry(), 0.0, 0.f);
	TSet<FString> Paths;
	Follow->GetRenderedPropertyPathsForTesting(Paths);
	TestFalse(TEXT("World basis hides subject index"), Paths.Contains(TEXT("Placement.BasisActorIndex")));
	Asset->Shot.Placement.BasisFrame = EShotPlacementBasisFrame::TwoTargetAxis;
	Follow->Tick(FGeometry(), 0.0, 0.f);
	Follow->GetRenderedPropertyPathsForTesting(Paths);
	TestTrue(TEXT("Basis change reveals both subject indices locally"), Paths.Contains(TEXT("Placement.BasisActorIndex")) && Paths.Contains(TEXT("Placement.BasisSecondaryTargetIndex")));
	Asset->Shot.Placement.PlacementAnchor.Mode = EShotAnchorMode::FixedWorldPosition;
	Follow->Tick(FGeometry(), 0.0, 0.f);
	Follow->GetRenderedPropertyPathsForTesting(Paths);
	TestTrue(TEXT("Nested anchor mode reveals world position locally"), Paths.Contains(TEXT("Placement.PlacementAnchor.WorldPosition")));
	TestFalse(TEXT("Nested anchor mode hides target index locally"), Paths.Contains(TEXT("Placement.PlacementAnchor.TargetIndex")));
	TestEqual(TEXT("Local mode changes retain pages"), Panel->GetPageBuildCountForTesting(), 1u);
	// Scalar host edits made elsewhere remain a conservative external refresh.
	FPropertyChangedEvent External(FindFProperty<FProperty>(UComposableCameraShotAsset::StaticClass(), GET_MEMBER_NAME_CHECKED(UComposableCameraShotAsset, Shot)), EPropertyChangeType::ValueSet);
	const int32 BeforeExternal = StructuralChanges;
	Asset->PostEditChangeProperty(External);
	TestTrue(TEXT("External host edits still request refresh"), StructuralChanges > BeforeExternal);
	TStrongObjectPtr<UComposableCameraShotAsset> Other(NewObject<UComposableCameraShotAsset>(GetTransientPackage(), NAME_None, RF_Transactional));
	Other->Shot.Targets.SetNum(1);
	Session->Bind(&Other->Shot, Other.Get());
	Panel->Refresh(); Panel->Tick(FGeometry(), 0.0, 0.f); Follow->Tick(FGeometry(), 0.0, 0.f);
	TestTrue(TEXT("Source swap regenerates handles against new host"), Follow->SetFloatForTesting(GET_MEMBER_NAME_CHECKED(FShotPlacement, Distance), 456.f));
	TestEqual(TEXT("New source receives value"), Other->Shot.Placement.Distance, 456.f);
	TestEqual(TEXT("Old source remains intact"), Asset->Shot.Placement.Distance, Before + 123.f);
	Session->Bind(nullptr, nullptr);
	Panel->Refresh(); Panel->Tick(FGeometry(), 0.0, 0.f); Follow->Tick(FGeometry(), 0.0, 0.f);
	TestEqual(TEXT("Source swap/close retains task pages"), Panel->GetPageBuildCountForTesting(), 1u);
	TestTrue(TEXT("Source swap/close retains parameter container"), Follow->GetChildren()->GetChildAt(0) == ParameterBody);
	Session->OnChanged = nullptr;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotEditorRetainedBooleanEnumRowsTest,
	"ComposableCameraSystem.ShotEditor.RetainedBooleanEnumRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotEditorRetainedBooleanEnumRowsTest::RunTest(const FString&)
{
	if (!GEditor || GEditor->IsTransactionActive()) return false;
	TStrongObjectPtr<UComposableCameraShotAsset> Asset(NewObject<UComposableCameraShotAsset>(GetTransientPackage(), NAME_None, RF_Transactional));
	Asset->Shot.Targets.SetNum(2);
	Asset->Shot.Placement.PlacementAnchor.WeightedTargets.SetNum(2);
	auto Session = MakeShared<FComposableCameraShotAuthoringSession>();
	Session->Bind(&Asset->Shot, Asset.Get());
	struct FNativeHook : FNotifyHook
	{
		TSharedPtr<FComposableCameraShotAuthoringSession> Session;
		virtual ~FNativeHook() = default;
		virtual void NotifyPreChange(FProperty*) override { SaveToTransactionBuffer(Session->GetHost(), false); }
		virtual void NotifyPostChange(const FPropertyChangedEvent& Event, FProperty*) override { Session->NotifyNativePropertyChange(Event); }
	} Hook;
	Hook.Session = Session;
	const auto Authoring = SNew(SShotEditorAuthoringPanel).Session(Session).NotifyHook(&Hook);
	TArray<TSharedPtr<SShotEditorParameterPanel>> Panels;
	Authoring->GetEditParameterPanelsForTesting(Panels);
	if (!TestEqual(TEXT("Every Edit parameter page exists"), Panels.Num(), 4)) return false;
	for (const auto& Panel : Panels)
		if (!TestTrue(TEXT("Native parameter panel exists"), Panel.IsValid())) return false;
	const auto Follow = Panels[0].ToSharedRef();
	const auto Aim = Panels[1].ToSharedRef();
	const auto Lens = Panels[2].ToSharedRef();
	const auto Subject = SNew(SShotEditorParameterPanel).Session(Session).NotifyHook(&Hook)
		.Section(EShotEditorParameterSection::Subject).SubjectIndex(1);
	Panels.Add(Subject);
	TArray<uint32> Counts;
	for (const auto& Panel : Panels) Counts.Add(Panel->GetRebuildCountForTesting());
	int32 StructuralChanges = 0;
	Session->OnChanged = [&](bool bStructural)
	{
		if (bStructural) { ++StructuralChanges; Authoring->Refresh(); Subject->RefreshSource(); }
	};
	const auto FindRow = [](const TSharedRef<SWidget>& Root, FName Tag) -> TSharedPtr<SWidget>
	{
		TSharedPtr<SWidget> Found;
		TFunction<void(const TSharedRef<SWidget>&)> Walk;
		Walk = [&](const TSharedRef<SWidget>& Widget)
		{
			if (Widget->GetTag() == Tag) { Found = Widget; return; }
			FChildren* Children = Widget->GetChildren();
			for (int32 Index = 0; Index < Children->Num() && !Found; ++Index) Walk(Children->GetChildAt(Index));
		};
		Walk(Root);
		return Found;
	};
	const FName FollowModeTag(TEXT("Placement.Mode"));
	const FName ZoneTag(TEXT("Placement.PlacementZones.bEnabled"));
	const FName AimModeTag(TEXT("Aim.Mode"));
	const FName LensModeTag(TEXT("Lens.FOVMode"));
	const FName BoundsTag(TEXT("Targets[1].BoundsShape"));
	const auto FollowModeRow = FindRow(Follow, FollowModeTag);
	const auto ZoneRow = FindRow(Follow, ZoneTag);
	const auto AimModeRow = FindRow(Aim, AimModeTag);
	const auto LensModeRow = FindRow(Lens, LensModeTag);
	const auto BoundsRow = FindRow(Subject, BoundsTag);
	if (!TestTrue(TEXT("Every native mode/toggle row is constructed, including inactive modes"),
		FollowModeRow && ZoneRow && AimModeRow && LensModeRow && BoundsRow)) { Session->OnChanged = nullptr; return false; }
	// Hold the complete child widgets, not only the reused page/body wrapper.
	const auto FollowModeControl = FollowModeRow->GetChildren()->GetChildAt(0);
	const auto ZoneControl = ZoneRow->GetChildren()->GetChildAt(0);
	const auto Pump = [&]()
	{
		for (int32 Frame = 0; Frame < 3; ++Frame)
		{
			// Exercise the real generator's deferred editor tick; a panel-only tick misses native tree refreshes.
			FTickableEditorObject::TickObjects(0.f);
			Authoring->Tick(FGeometry(), 0., 0.f);
			for (const auto& Panel : Panels) Panel->Tick(FGeometry(), 0., 0.f);
		}
	};
	const auto CheckRetained = [&]()
	{
		for (int32 Index = 0; Index < Panels.Num(); ++Index)
			TestEqual(TEXT("Boolean/Enum edits never rebuild parameter pages"), Panels[Index]->GetRebuildCountForTesting(), Counts[Index]);
		TestTrue(TEXT("Follow mode control survives"), FindRow(Follow, FollowModeTag) == FollowModeRow && FollowModeRow->GetChildren()->GetChildAt(0) == FollowModeControl);
		TestTrue(TEXT("Inactive/active checkbox control survives"), FindRow(Follow, ZoneTag) == ZoneRow && ZoneRow->GetChildren()->GetChildAt(0) == ZoneControl);
		TestTrue(TEXT("Aim, Lens and Subject enum rows survive"), FindRow(Aim, AimModeTag) == AimModeRow
			&& FindRow(Lens, LensModeTag) == LensModeRow && FindRow(Subject, BoundsTag) == BoundsRow);
		TestEqual(TEXT("Native scalar commits never request host/source refresh"), StructuralChanges, 0);
	};
	const auto Change = [&](const TSharedRef<SShotEditorParameterPanel>& Panel, const TCHAR* Path, auto Value)
	{
		{
			FScopedTransaction Transaction(FText::FromString(TEXT("Native Shot Boolean/Enum")));
			TestTrue(FString(TEXT("Native property write: ")) + Path, Panel->SetValueForTesting(Path, Value));
		}
		Pump(); CheckRetained();
	};
	const auto CheckVisibility = [&](const TSharedRef<SShotEditorParameterPanel>& Panel, const TCHAR* Path, EVisibility Expected, const TCHAR* Label)
	{
		const auto Row = FindRow(Panel, FName(Path));
		if (TestTrue(FString(TEXT("Retained row exists: ")) + Path, Row.IsValid()))
			TestTrue(Label, Row->GetVisibility() == Expected);
	};
	Change(Follow, TEXT("Placement.Mode"), uint8(EShotPlacementMode::AnchorAtScreen));
	CheckVisibility(Follow, TEXT("Placement.ScreenPosition"), EVisibility::Visible, TEXT("Mode edit reveals existing screen row"));
	Change(Follow, TEXT("Placement.PlacementZones.bEnabled"), true);
	Change(Follow, TEXT("Placement.PlacementZones.bEnabled"), false);
	Change(Follow, TEXT("Placement.Mode"), uint8(EShotPlacementMode::AnchorOrbit));
	Change(Follow, TEXT("Placement.BasisFrame"), uint8(EShotPlacementBasisFrame::TwoTargetAxis));
	CheckVisibility(Follow, TEXT("Placement.BasisSecondaryTargetIndex"), EVisibility::Visible, TEXT("Basis edit reveals existing secondary subject row"));
	Change(Follow, TEXT("Placement.PlacementAnchor.Mode"), uint8(EShotAnchorMode::WeightedWorldCentroid));
	CheckVisibility(Follow, TEXT("Placement.PlacementAnchor.WeightedTargets"), EVisibility::Visible, TEXT("Weighted anchor exposes retained array"));
	// Array-element owners must not be mistaken for anchor direct members when evaluating ancestor visibility.
	TSet<FString> Paths;
	Follow->GetRenderedPropertyPathsForTesting(Paths);
	bool bWeightedChildVisible = false;
	for (const FString& Path : Paths) bWeightedChildVisible |= Path.StartsWith(TEXT("Placement.PlacementAnchor.WeightedTargets."));
	TestTrue(TEXT("Weighted centroid keeps entry controls visible"), bWeightedChildVisible);
	Change(Follow, TEXT("Placement.PlacementAnchor.Mode"), uint8(EShotAnchorMode::FixedWorldPosition));
	CheckVisibility(Follow, TEXT("Placement.PlacementAnchor.WorldPosition"), EVisibility::Visible, TEXT("Anchor edit reveals existing world position"));
	Change(Aim, TEXT("Aim.Mode"), uint8(EShotAimMode::NoOp));
	CheckVisibility(Aim, TEXT("Aim.ScreenPosition"), EVisibility::Collapsed, TEXT("NoOp hides existing screen row"));
	Change(Aim, TEXT("Aim.Mode"), uint8(EShotAimMode::LookAtAnchor));
	Change(Aim, TEXT("Aim.AimZones.bEnabled"), true);
	Change(Lens, TEXT("Lens.FOVMode"), uint8(EShotFOVMode::SolvedFromBoundsFit));
	Change(Lens, TEXT("Focus.Mode"), uint8(EShotFocusMode::FollowCustomAnchor));
	Change(Lens, TEXT("Focus.FocusAnchor.Mode"), uint8(EShotAnchorMode::FixedWorldPosition));
	CheckVisibility(Lens, TEXT("Focus.FocusAnchor.WorldPosition"), EVisibility::Visible, TEXT("Custom focus reveals existing world-anchor field"));
	Change(Subject, TEXT("Targets[1].BoundsShape"), uint8(EShotTargetBoundsShape::AutoFromComponentBounds));
	Change(Subject, TEXT("Targets[1].BoundsCachePolicy"), uint8(EBoundsCachePolicy::Periodic));
	Change(Subject, TEXT("Targets[1].Target.bOffsetInLocalSpace"), false);
	Change(Subject, TEXT("Targets[1].Target.bUseBoneAsPivot"), true);
	TestTrue(TEXT("Boolean changed actual second subject"), Asset->Shot.Targets[1].Target.bUseBoneAsPivot);
	TestFalse(TEXT("First subject remains unchanged"), Asset->Shot.Targets[0].Target.bUseBoneAsPivot);
	TestTrue(TEXT("Native boolean commit supports Undo"), GEditor->UndoTransaction());
	Pump();
	TestFalse(TEXT("Undo restores boolean"), Asset->Shot.Targets[1].Target.bUseBoneAsPivot);
	TestTrue(TEXT("Undo still refreshes potentially invalid handles"), StructuralChanges > 0 && Subject->GetRebuildCountForTesting() > Counts.Last());
	TestTrue(TEXT("Native boolean commit supports Redo"), GEditor->RedoTransaction());
	Pump();
	TestTrue(TEXT("Redo restores boolean"), Asset->Shot.Targets[1].Target.bUseBoneAsPivot);
	TestEqual(TEXT("History retains task pages"), Authoring->GetPageBuildCountForTesting(), 1u);
	Session->OnChanged = nullptr;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotEditorViewportValueCommitsTest,
	"ComposableCameraSystem.ShotEditor.ViewportValueCommits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotEditorViewportValueCommitsTest::RunTest(const FString&)
{
	if (!GEditor || GEditor->IsTransactionActive()) return false;
	TStrongObjectPtr<UComposableCameraShotAsset> Asset(NewObject<UComposableCameraShotAsset>(GetTransientPackage(), NAME_None, RF_Transactional));
	Asset->Shot.Targets.SetNum(1);
	Asset->Shot.Placement.Mode = EShotPlacementMode::AnchorOrbit;
	Asset->Shot.Aim.Mode = EShotAimMode::LookAtAnchor;
	auto Session = MakeShared<FComposableCameraShotAuthoringSession>();
	Session->Bind(&Asset->Shot, Asset.Get());
	Session->bUseLevelWorld = false;
	const auto Panel = SNew(SShotEditorAuthoringPanel).Session(Session);
	const auto Follow = Panel->GetFollowPanelForTesting();
	const uint32 BeforeRows = Follow->GetRebuildCountForTesting();
	const SWidget* Page = Panel->GetEditPageForTesting();
	const auto Preview = SNew(SShotEditorViewport);
	Preview->SetAuthoringSession(Session);
	Preview->SetActiveShot(&Asset->Shot, Asset.Get());
	const auto Client = Preview->GetClientForTesting();
	if (!TestTrue(TEXT("Real preview client created"), Client.IsValid())) return false;
	int32 StructuralChanges = 0;
	Session->OnChanged = [&](bool bStructural) { if (bStructural) { ++StructuralChanges; Panel->Refresh(); } };
	int32 HostCommits = 0, ModifiedEvents = 0;
	const FDelegateHandle HostChanged = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda(
		[&](UObject* Object, FPropertyChangedEvent&) { if (Object == Asset.Get()) ++HostCommits; });
	const FDelegateHandle HostModified = FCoreUObjectDelegates::OnObjectModified.AddLambda(
		[&](UObject* Object) { if (Object == Asset.Get()) ++ModifiedEvents; });
	const float Distance = Asset->Shot.Placement.Distance;
	TestTrue(TEXT("Actual wheel path changes distance"), Client->TryAdjustDistanceFromMouseWheel(true));
	Panel->Tick(FGeometry(), 0., 0.f); Follow->Tick(FGeometry(), 0., 0.f);
	TestEqual(TEXT("Wheel commit does not request structural refresh"), StructuralChanges, 0);
	TestEqual(TEXT("Wheel emits exactly one host commit"), HostCommits, 1);
	TestEqual(TEXT("Wheel snapshot emits no OnObjectModified"), ModifiedEvents, 0);
	TestEqual(TEXT("Wheel retains native parameter controls"), Follow->GetRebuildCountForTesting(), BeforeRows);
	TestTrue(TEXT("Wheel keeps page identity"), Panel->GetEditPageForTesting() == Page);
	TestTrue(TEXT("Wheel records undo"), GEditor->UndoTransaction());
	TestEqual(TEXT("Wheel undo restores distance"), Asset->Shot.Placement.Distance, Distance);
	Panel->Tick(FGeometry(), 0., 0.f); Follow->Tick(FGeometry(), 0., 0.f);
	StructuralChanges = 0;
	HostCommits = ModifiedEvents = 0;
	const uint32 AfterUndoRows = Follow->GetRebuildCountForTesting();
	const FVector2D ScreenBefore = Asset->Shot.Aim.ScreenPosition;
	// Seed the mouse-down state, then exercise the real drag-release callback.
	Client->DragTransaction = MakeUnique<FScopedTransaction>(FText::FromString(TEXT("Aim preview drag")));
	SaveToTransactionBuffer(Asset.Get(), false);
	Client->ActiveDragHandleType = FComposableCameraShotEditorViewportClient::EHandleType::AimAnchor;
	Asset->Shot.Aim.ScreenPosition = FVector2D(.12f, -.08f);
	Client->NotifyInteractiveEdit();
	Client->EndDrag();
	Panel->Tick(FGeometry(), 0., 0.f); Follow->Tick(FGeometry(), 0., 0.f);
	TestEqual(TEXT("Anchor drag release does not request structural refresh"), StructuralChanges, 0);
	TestEqual(TEXT("Anchor release emits exactly one host commit"), HostCommits, 1);
	TestEqual(TEXT("Anchor snapshot emits no OnObjectModified"), ModifiedEvents, 0);
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(HostChanged);
	FCoreUObjectDelegates::OnObjectModified.Remove(HostModified);
	TestEqual(TEXT("Anchor drag retains native parameter controls"), Follow->GetRebuildCountForTesting(), AfterUndoRows);
	TestTrue(TEXT("Anchor drag remains one undo step"), GEditor->UndoTransaction());
	TestTrue(TEXT("Anchor drag undo restores both coordinates"), Asset->Shot.Aim.ScreenPosition.Equals(ScreenBefore));
	Panel->SelectLookAtForTesting(true);
	TestTrue(TEXT("Edit / Aim enables guide"), Client->ShowLookAtHandle());
	Panel->SelectLookAtForTesting(false);
	TestFalse(TEXT("Follow hides guide without waiting for repaint"), Client->ShowLookAtHandle());
	Panel->SelectLookAtForTesting(true);
	Panel->SelectCreateForTesting();
	TestFalse(TEXT("Create hides guide despite retained Aim selection"), Client->ShowLookAtHandle());
	Asset->Shot.Aim.Mode = EShotAimMode::NoOp;
	Panel->SelectLookAtForTesting(true);
	TestFalse(TEXT("NoOp still hides guide on selected Aim tab"), Client->ShowLookAtHandle());
	TestEqual(TEXT("Gestures and navigation keep task pages"), Panel->GetPageBuildCountForTesting(), 1u);
	Session->OnChanged = nullptr;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotEditorOrbitControlTest,
	"ComposableCameraSystem.ShotEditor.OrbitControl",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotEditorOrbitControlTest::RunTest(const FString&)
{
	using namespace ComposableCameraSystem::ShotViewportOverlay;
	if (!GEditor || GEditor->IsTransactionActive()) return false;
	TStrongObjectPtr<UComposableCameraShotAsset> Asset(NewObject<UComposableCameraShotAsset>(GetTransientPackage(), NAME_None, RF_Transactional));
	Asset->Shot.Placement.Mode = EShotPlacementMode::AnchorOrbit;
	Asset->Shot.Placement.LocalCameraDirection = FVector2D(170., 20.);
	Asset->Shot.Placement.Distance = 500.f;
	auto Session = MakeShared<FComposableCameraShotAuthoringSession>();
	Session->Bind(&Asset->Shot, Asset.Get());
	Session->bUseLevelWorld = false;
	const auto Panel = SNew(SShotEditorAuthoringPanel).Session(Session);
	Panel->SelectLookAtForTesting(false);
	const auto Preview = SNew(SShotEditorViewport);
	Preview->SetAuthoringSession(Session); Preview->SetActiveShot(&Asset->Shot, Asset.Get());
	const auto Client = Preview->GetClientForTesting();
	if (!TestTrue(TEXT("Production orbit client created"), Client.IsValid())) return false;
	if (!TestTrue(TEXT("Scene viewport assigned before captured input"), Client->Viewport != nullptr)) return false;
	const auto Follow = Panel->GetFollowPanelForTesting();
	const auto RowsBefore = Follow->GetRebuildCountForTesting();
	const SWidget* PageBefore = Panel->GetEditPageForTesting();
	const FVector2D Before = Asset->Shot.Placement.LocalCameraDirection;
	const FVector2D FollowScreenBefore = Asset->Shot.Placement.ScreenPosition;
	const FVector2D AimScreenBefore = Asset->Shot.Aim.ScreenPosition;
	int32 Commits = 0, Modified = 0, Structural = 0;
	Session->OnChanged = [&](bool bStructural) { if (bStructural) ++Structural; };
	const auto ChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda(
		[&](UObject* Object, FPropertyChangedEvent&) { if (Object == Asset.Get()) ++Commits; });
	const auto ModifiedHandle = FCoreUObjectDelegates::OnObjectModified.AddLambda(
		[&](UObject* Object) { if (Object == Asset.Get()) ++Modified; });
	FComposableCameraShotEditorViewportClient::FHandleScreenPosCache Hit;
	Hit.Type = FComposableCameraShotEditorViewportClient::EHandleType::OrbitDirection;
	Client->OrbitControlDpiScale = 2.f;
	Client->StartHandleDrag(Hit, 100, 100);
	TestTrue(TEXT("Mouse-down begins one production gesture"), Client->IsEditingGesture());
	Client->ApplyDragToShot(120, 90);
	Client->ApplyDragToShot(300, 60);
	TestFalse(TEXT("Wheel cannot nest a transaction inside captured drag"), Client->TryAdjustDistanceFromMouseWheel(true));
	const FVector2D Expected = DragOrbitDirection(Before, FVector2D(200., -40.), 2.f);
	TestTrue(TEXT("Captured mouse writes DPI-independent yaw/pitch"), Asset->Shot.Placement.LocalCameraDirection.Equals(Expected, .001));
	TestEqual(TEXT("Live samples send no host commit"), Commits, 0);
	Client->EndDrag();
	Panel->Tick(FGeometry(), 0., 0.f); Follow->Tick(FGeometry(), 0., 0.f);
	TestEqual(TEXT("Gesture release sends exactly one commit"), Commits, 1);
	TestEqual(TEXT("Snapshots never broadcast OnObjectModified"), Modified, 0);
	TestEqual(TEXT("Orbit values do not refresh panel structure"), Structural, 0);
	TestEqual(TEXT("Native Follow controls retained"), Follow->GetRebuildCountForTesting(), RowsBefore);
	TestTrue(TEXT("Task page retained"), Panel->GetEditPageForTesting() == PageBefore);
	TestEqual(TEXT("Orbit preserves radius"), Asset->Shot.Placement.Distance, 500.f);
	TestTrue(TEXT("Screen constraints stay authored"), Asset->Shot.Placement.ScreenPosition == FollowScreenBefore && Asset->Shot.Aim.ScreenPosition == AimScreenBefore);
	const FVector Anchor(80., -20., 150.);
	for (const FQuat Basis : { FQuat::Identity, FRotator(12., 70., 5.).Quaternion() })
	{
		const FVector Camera = ComposableCameraSystem::ShotSolver::SolveAnchorOrbitPosition(Anchor, Basis, Asset->Shot.Placement.Distance,
			Asset->Shot.Placement.LocalCameraDirection, FVector2D::ZeroVector, 1.f, 16.f / 9.f);
		TestTrue(TEXT("Real solver moves camera on unchanged sphere"), FMath::IsNearlyEqual((Camera - Anchor).Length(), 500., .01));
		TestTrue(TEXT("Drag respects authored basis"), Basis.Inverse().RotateVector((Camera - Anchor).GetSafeNormal())
			.Equals(FRotator(Expected.Y, Expected.X, 0.).Vector(), .001));
	}
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ChangedHandle);
	FCoreUObjectDelegates::OnObjectModified.Remove(ModifiedHandle);
	TestTrue(TEXT("One Undo restores both angles"), GEditor->UndoTransaction());
	TestTrue(TEXT("Pre-gesture direction restored"), Asset->Shot.Placement.LocalCameraDirection.Equals(Before));
	Panel->Tick(FGeometry(), 0., 0.f); Follow->Tick(FGeometry(), 0., 0.f);
	Commits = 0;
	const auto NoopHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda(
		[&](UObject* Object, FPropertyChangedEvent&) { if (Object == Asset.Get()) ++Commits; });
	Client->StartHandleDrag(Hit, 100, 100); Client->ApplyDragToShot(100, 100); Client->EndDrag();
	TestEqual(TEXT("Click without movement has no commit"), Commits, 0);
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(NoopHandle);
	Panel->SelectLookAtForTesting(true);
	TestFalse(TEXT("Aim hides orbit immediately"), Client->ShowOrbitControl());
	Client->StartHandleDrag(Hit, 100, 100);
	TestFalse(TEXT("Stale orbit hit cannot begin on Aim"), Client->IsEditingGesture());
	Panel->SelectLookAtForTesting(false);
	TestTrue(TEXT("Follow shows orbit"), Client->ShowOrbitControl());
	Client->StartHandleDrag(Hit, 100, 100);
	Panel->SelectLookAtForTesting(true);
	Client->ApplyDragToShot(200, 200);
	TestTrue(TEXT("Leaving Follow prevents stale drag writes"), Asset->Shot.Placement.LocalCameraDirection.Equals(Before));
	Client->EndDrag();
	Panel->SelectLookAtForTesting(false);
	for (EShotPlacementMode Mode : { EShotPlacementMode::AnchorAtScreen, EShotPlacementMode::FixedWorldPosition })
	{
		Asset->Shot.Placement.Mode = Mode;
		Client->StartHandleDrag(Hit, 100, 100);
		TestFalse(TEXT("Other placement modes reject orbit"), Client->IsEditingGesture());
	}
	Asset->Shot.Placement.Mode = EShotPlacementMode::AnchorOrbit;
	Client->SetMode(EShotEditorMode::Free); Client->StartHandleDrag(Hit, 100, 100);
	TestFalse(TEXT("Inspect cannot edit orbit"), Client->IsEditingGesture());
	Client->SetMode(EShotEditorMode::Drag); Client->SetShowCompositionGuides(false);
	Client->StartHandleDrag(Hit, 100, 100);
	TestFalse(TEXT("Guides off rejects orbit"), Client->IsEditingGesture());
	Client->SetShowCompositionGuides(true);
	Panel->SelectCreateForTesting(); Client->StartHandleDrag(Hit, 100, 100);
	TestFalse(TEXT("Create rejects retained Follow control"), Client->IsEditingGesture());
	Panel->SelectLookAtForTesting(false);
	TStrongObjectPtr<UMovieScene> Scene(NewObject<UMovieScene>());
	TStrongObjectPtr<UMovieSceneComposableCameraShotSection> Section(NewObject<UMovieSceneComposableCameraShotSection>(Scene.Get()));
	Section->InlineShot.Placement.Mode = EShotPlacementMode::AnchorOrbit; Section->SetIsLocked(true);
	Session->Bind(&Section->InlineShot, Section.Get()); Preview->SetActiveShot(&Section->InlineShot, Section.Get());
	Client->StartHandleDrag(Hit, 100, 100);
	TestFalse(TEXT("Locked section rejects orbit writes"), Client->IsEditingGesture());
	Section->SetIsLocked(false); Scene->SetReadOnly(true);
	Client->StartHandleDrag(Hit, 100, 100);
	TestFalse(TEXT("Read-only sequence rejects orbit writes"), Client->IsEditingGesture());
	Session->OnChanged = nullptr;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotEditorPreviewOverlayLayoutTest,
	"ComposableCameraSystem.ShotEditor.PreviewOverlayLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotEditorPreviewOverlayLayoutTest::RunTest(const FString&)
{
	using namespace ComposableCameraSystem::ShotViewportOverlay;
	using namespace ComposableCameraSystem::ShotEditorCanvas;
	const FVector2D Start(175., 10.);
	TestTrue(TEXT("Yaw wraps across 180"), DragOrbitDirection(Start, FVector2D(40., 0.), 1.f).Equals(FVector2D(-167., 10.)));
	TestTrue(TEXT("Mouse up raises pitch"), DragOrbitDirection(Start, FVector2D(0., -20.), 1.f).Y > Start.Y);
	TestTrue(TEXT("Zero delta does not normalize or dirty authored data"), DragOrbitDirection(FVector2D(360., 95.), FVector2D::ZeroVector, 1.f) == FVector2D(360., 95.));
	TestTrue(TEXT("Equivalent physical drag under 2x DPI"), DragOrbitDirection(Start, FVector2D(40., -20.), 1.f).Equals(DragOrbitDirection(Start, FVector2D(80., -40.), 2.f)));
	TestEqual(TEXT("Pitch remains clear of north singularity"), DragOrbitDirection(Start, FVector2D(0., -1000.), 1.f).Y, 89.5);
	TestEqual(TEXT("Pitch remains clear of south singularity"), DragOrbitDirection(Start, FVector2D(0., 1000.), 1.f).Y, -89.5);
	TestTrue(TEXT("Fine drag is smaller"), FMath::Abs(DragOrbitDirection(FVector2D::ZeroVector, FVector2D(10., 0.), 1.f, .2f).X)
		< FMath::Abs(DragOrbitDirection(FVector2D::ZeroVector, FVector2D(10., 0.), 1.f).X));
	for (float Scale : { 1.f, 1.5f, 2.f })
	{
		for (FVector2D LogicalSize : { FVector2D(1200., 675.), FVector2D(700., 394.), FVector2D(500., 281.), FVector2D(400., 700.), FVector2D(1200., 260.), FVector2D(500., 196.) })
		{
			const FVector2D Origin(23., 41.);
			const FBox2D Physical(Origin * Scale, (Origin + LogicalSize) * Scale);
			const FBox2D Image(ViewportToCanvas(Physical.Min, Scale), ViewportToCanvas(Physical.Max, Scale));
			const auto Orbit = OrbitLayout(Image);
			TestTrue(TEXT("Globe fits real camera frame at mixed DPI"), Orbit.IsVisible() && Image.IsInside(Orbit.Panel.Min) && Image.IsInside(Orbit.Panel.Max));
			TestTrue(TEXT("Globe center receives input"), Orbit.Hit(ViewportToCanvas(CanvasToViewport(Orbit.Center, Scale), Scale)));
			TestFalse(TEXT("Bounding-square corners do not steal clicks"), Orbit.Hit(Orbit.Center + FVector2D(Orbit.Radius * .9f, Orbit.Radius * .9f)));
			const auto Hud = HudLayout(Image);
			TestTrue(TEXT("HUD always starts at the fixed upper-left inset"), Hud.Camera.Min.Equals(Image.Min + FVector2D(8., 8.), .001));
			TestTrue(TEXT("Camera and Composition share the same left edge"), FMath::IsNearlyEqual(Hud.Composition.Min.X, Hud.Camera.Min.X, .001));
			TestTrue(TEXT("Composition stacks below Camera with a scaled gap"), FMath::IsNearlyEqual(Hud.Composition.Min.Y - Hud.Camera.Max.Y, 8. * Hud.Scale, .001));
			TestEqual(TEXT("Uniform scaling retains all eight camera rows"), Hud.Rows(Hud.Camera), 8);
			TestEqual(TEXT("Uniform scaling retains all nine composition rows"), Hud.Rows(Hud.Composition), 9);
			TestFalse(TEXT("Cards never overlap"), Hud.Camera.Intersect(Hud.Composition));
			TestTrue(TEXT("Stacked HUD occupies at most a quarter of image width"), Hud.Camera.GetSize().X <= LogicalSize.X * .25);
			for (const FBox2D& Card : { Hud.Camera, Hud.Composition })
			{
				TestTrue(TEXT("Cards stay inside constrained image"), Image.IsInside(Card.Min) && Image.IsInside(Card.Max));
				TestFalse(TEXT("Fixed top-left HUD leaves bottom-left orbit input visible"), Card.Intersect(Orbit.Panel));
			}
		}
	}
	const FBox2D LargeImage(FVector2D(23., 41.), FVector2D(1303., 761.));
	const FBox2D SmallImage(LargeImage.Min, LargeImage.Min + LargeImage.GetSize() * .5);
	const auto LargeHud = HudLayout(LargeImage);
	const auto SmallHud = HudLayout(SmallImage);
	TestTrue(TEXT("Regular-sized HUD is larger than the previous 0.7 scale"), LargeHud.Scale > .7f);
	TestTrue(TEXT("Halving image halves HUD scale, including font and row spacing"), FMath::IsNearlyEqual(SmallHud.Scale, LargeHud.Scale * .5f));
	TestTrue(TEXT("Cards shrink proportionally instead of dropping rows or wrapping"), SmallHud.Camera.GetSize().Equals(LargeHud.Camera.GetSize() * .5, .001)
		&& SmallHud.Composition.GetSize().Equals(LargeHud.Composition.GetSize() * .5, .001));
	TestTrue(TEXT("Resize leaves upper-left HUD origin fixed"), SmallHud.Camera.Min.Equals(LargeHud.Camera.Min, .001));
	TestFalse(TEXT("Invalid camera rectangle hides HUD"), HudLayout(FBox2D(ForceInit)).Camera.bIsValid);
	for (FVector2D Size : { FVector2D::ZeroVector, FVector2D(10., 200.), FVector2D(200., 10.) })
		TestFalse(TEXT("Degenerate image hides HUD rather than escaping camera frame"), HudLayout(FBox2D(FVector2D::ZeroVector, Size)).Camera.bIsValid);
	for (FVector2D Size : { FVector2D::ZeroVector, FVector2D(170., 400.), FVector2D(300., 180.) })
		TestFalse(TEXT("Insufficient geometry hides orbit rather than drawing outside image"), OrbitLayout(FBox2D(FVector2D::ZeroVector, Size)).IsVisible());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotEditorCameraAspectTest,
	"ComposableCameraSystem.ShotEditor.CameraAspect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotEditorCameraAspectTest::RunTest(const FString&)
{
	using namespace ComposableCameraSystem::ShotViewportDisplay;
	TStrongObjectPtr<UCineCameraComponent> Camera(NewObject<UCineCameraComponent>());
	FCameraFilmbackSettings Filmback = Camera->Filmback;
	Filmback.SensorWidth = 36.f; Filmback.SensorHeight = 24.f;
	Camera->SetFilmback(Filmback);
	FCameraLensSettings Lens = Camera->LensSettings;
	Lens.SqueezeFactor = 1.5f;
	Camera->SetLensSettings(Lens);
	FPlateCropSettings Crop; Crop.AspectRatio = 0.f;
	Camera->SetCropSettings(Crop);
	TestEqual(TEXT("Filmback and squeeze determine preview aspect"), CameraAspectRatio(Camera.Get()), 2.25f);
	for (float Ratio : { 2.39f, 1.f, 9.f / 16.f })
	{
		Crop.AspectRatio = Ratio; Camera->SetCropSettings(Crop);
		const float Configured = CameraAspectRatio(Camera.Get());
		TestEqual(TEXT("Crop overrides filmback aspect"), Configured, Ratio);
		FMinimalViewInfo NativeView; Camera->GetCameraView(0.f, NativeView);
		TestEqual(TEXT("Preview aspect equals actual CineCamera view"), Configured, NativeView.AspectRatio);
		const auto Box = SNew(SShotEditorPreviewFrame).AspectRatio(Configured)[SNullWidget::NullWidget];
		for (FVector2D Size : { FVector2D(1200, 300), FVector2D(500, 800), FVector2D(1920, 1080) })
		{
			FArrangedChildren Children(EVisibility::All);
			Box->ArrangeChildren(FGeometry::MakeRoot(Size, FSlateLayoutTransform()), Children);
			if (!TestEqual(TEXT("One preview frame arranged"), Children.Num(), 1)) continue;
			const FVector2D Image = Children[0].Geometry.GetLocalSize();
			TestTrue(TEXT("Right-aligned zero-desired-size preview retains camera aspect on resize"), FMath::IsNearlyEqual(float(Image.X / Image.Y), Ratio, 1.e-4f));
			TestTrue(TEXT("Preview has positive image area"), Image.X > 0.0 && Image.Y > 0.0);
			TestTrue(TEXT("Preview leaves room for external screen bezel"), Image.X <= Size.X - 2.f * SShotEditorPreviewFrame::BezelThickness + .01
				&& Image.Y <= Size.Y - 2.f * SShotEditorPreviewFrame::BezelThickness + .01);
			const FVector2D Origin = Children[0].Geometry.LocalToAbsolute(FVector2D::ZeroVector);
			TestTrue(TEXT("Screen bezel meets right edge for portrait, square and wide cameras"), FMath::IsNearlyEqual(Origin.X + Image.X + SShotEditorPreviewFrame::BezelThickness, Size.X, .01));
			TestTrue(TEXT("Bezel surrounds actual image inside pane without clipping"), Origin.X >= SShotEditorPreviewFrame::BezelThickness - .01
				&& Origin.Y >= SShotEditorPreviewFrame::BezelThickness - .01
				&& Origin.Y + Image.Y + SShotEditorPreviewFrame::BezelThickness <= Size.Y + .01);
			TestTrue(TEXT("Preview image stays vertically centered"), FMath::IsNearlyEqual(Origin.Y + Image.Y * .5, Size.Y * .5, .01));
		}
	}
	Camera->Filmback.SensorHeight = 0.f; Camera->CropSettings.AspectRatio = 0.f;
	TestEqual(TEXT("Invalid filmback has finite default aspect"), CameraAspectRatio(Camera.Get()), 16.f / 9.f);
	TestTrue(TEXT("Detached Shot uses native CineCamera default"), FMath::IsNearlyEqual(CameraAspectRatio(nullptr), CameraAspectRatio(GetDefault<UCineCameraComponent>())));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotEditorAdaptivePreviewResizeTest,
	"ComposableCameraSystem.ShotEditor.AdaptivePreviewResize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotEditorAdaptivePreviewResizeTest::RunTest(const FString&)
{
	float Ratio = 16.f / 9.f;
	const auto Frame = SNew(SShotEditorPreviewFrame).AspectRatio_Lambda([&]() { return FOptionalSize(Ratio); })
		[SNullWidget::NullWidget];
	const auto Layout = SNew(SShotEditorPreviewLayout)
		.Authoring()[SNullWidget::NullWidget].Preview()[Frame];
	TestTrue(TEXT("Native divider remains hit-testable for mouse resizing"), Layout->GetVisibility().IsHitTestVisible());
	const auto ImageSize = [&](FVector2D Available, float Scale = 1.f) -> FVector2D
	{
		// Process native SBox aspect attributes before arranging the same widgets.
		Layout->Invalidate(EInvalidateWidgetReason::Layout);
		Layout->SlatePrepass(Scale);
		const FGeometry Root = FGeometry::MakeRoot(Available, FSlateLayoutTransform(Scale));
		FArrangedChildren Columns(EVisibility::All);
		Layout->ArrangeChildren(Root, Columns, true);
		if (!TestEqual(TEXT("Production layout has exactly two columns"), Columns.Num(), 2)) return FVector2D::ZeroVector;
		const FVector2D AuthoringSize = Columns[0].Geometry.GetLocalSize();
		const FVector2D AuthoringOrigin = Root.AbsoluteToLocal(Columns[0].Geometry.LocalToAbsolute(FVector2D::ZeroVector));
		const FVector2D PreviewSize = Columns[1].Geometry.GetLocalSize();
		const FVector2D PreviewOrigin = Root.AbsoluteToLocal(Columns[1].Geometry.LocalToAbsolute(FVector2D::ZeroVector));
		TestTrue(TEXT("Parameter column starts at the left and uses full height"), AuthoringOrigin.Equals(FVector2D::ZeroVector, .01)
			&& FMath::IsNearlyEqual(AuthoringSize.Y, Available.Y, .01));
		TestTrue(TEXT("Preview stays beside parameters with full height"), PreviewOrigin.X >= AuthoringSize.X
			&& FMath::IsNearlyZero(PreviewOrigin.Y, .01) && FMath::IsNearlyEqual(PreviewSize.Y, Available.Y, .01));
		TestTrue(TEXT("Both columns stay usable at tested widths"), AuthoringSize.X > 0.0 && PreviewSize.X > 0.0);
		// Native splitter rounds slot origins to logical pixels; allow its one-pixel remainder.
		TestTrue(TEXT("Columns fit resized window"), PreviewOrigin.X + PreviewSize.X <= Available.X + 1.0);
		FArrangedChildren Image(EVisibility::All);
		Frame->ArrangeChildren(Columns[1].Geometry, Image, true);
		if (!TestEqual(TEXT("Native camera image arranged"), Image.Num(), 1)) return FVector2D::ZeroVector;
		const FVector2D Size = Image[0].Geometry.GetLocalSize();
		const FVector2D Origin = Root.AbsoluteToLocal(Image[0].Geometry.LocalToAbsolute(FVector2D::ZeroVector));
		TestTrue(TEXT("Image keeps current camera aspect"), Size.Y > 0.0 && FMath::IsNearlyEqual(float(Size.X / Size.Y), Ratio, 1.e-4f));
		TestTrue(TEXT("Image bezel meets preview column's right edge"), FMath::IsNearlyEqual(Origin.X + Size.X + SShotEditorPreviewFrame::BezelThickness, PreviewOrigin.X + PreviewSize.X, .01));
		TestTrue(TEXT("External bezel fits preview without covering parameter column"), Origin.X - SShotEditorPreviewFrame::BezelThickness >= PreviewOrigin.X - .01
			&& Origin.Y >= SShotEditorPreviewFrame::BezelThickness - .01
			&& Origin.Y + Size.Y + SShotEditorPreviewFrame::BezelThickness <= Available.Y + .01);
		TestTrue(TEXT("Letterboxed image remains vertically centered"), FMath::IsNearlyEqual(Origin.Y + Size.Y * .5, Available.Y * .5, .01));
		return Size;
	};
	const FVector2D Small = ImageSize(FVector2D(640, 1000));
	const FVector2D Medium = ImageSize(FVector2D(1800, 1000));
	const FVector2D Large = ImageSize(FVector2D(2000, 1000));
	TestTrue(TEXT("Widening alone enlarges native preview width and height"), Small.X < Medium.X && Medium.X < Large.X && Small.Y < Medium.Y && Medium.Y < Large.Y);
	TestTrue(TEXT("Narrowing same widget restores smaller preview"), ImageSize(FVector2D(640, 1000)).Equals(Small, .01));
	const FVector2D Short = ImageSize(FVector2D(1440, 320));
	TestTrue(TEXT("Short window shrinks image while parameters keep full height"), Short.X < Large.X && Short.Y < Large.Y);
	TestTrue(TEXT("DPI scaling preserves logical framing size"), ImageSize(FVector2D(2000, 1000), 1.5f).Equals(Large, .01));
	Layout->SlotAt(0).SetSizeValue(.5f); Layout->SlotAt(1).SetSizeValue(.5f);
	TestTrue(TEXT("Dragging divider gives parameters more room and resizes preview"), ImageSize(FVector2D(2000, 1000)).X < Large.X);
	for (float CameraRatio : { 1.f, 2.39f, 9.f / 16.f })
	{
		Ratio = CameraRatio;
		ImageSize(FVector2D(1440, 1000));
	}
	for (FVector2D Available : { FVector2D::ZeroVector, FVector2D(4, 300), FVector2D(1440, 0) })
	{
		FArrangedChildren Empty(EVisibility::All);
		Layout->ArrangeChildren(FGeometry::MakeRoot(Available, FSlateLayoutTransform()), Empty);
		TestEqual(TEXT("Degenerate tab geometry avoids negative preview extents"), Empty.Num(), 0);
	}
	for (FVector2D Available : { FVector2D(12, 200), FVector2D(200, 12), FVector2D::ZeroVector })
	{
		FArrangedChildren Empty(EVisibility::All);
		Frame->ArrangeChildren(FGeometry::MakeRoot(Available, FSlateLayoutTransform()), Empty);
		TestEqual(TEXT("Pane smaller than bezel does not arrange invalid image"), Empty.Num(), 0);
	}
	TestTrue(TEXT("Screen frame decoration never intercepts viewport mouse input"), Frame->GetVisibility() == EVisibility::SelfHitTestInvisible);
	TestTrue(TEXT("Resize retains original preview frame"), Layout->SlotAt(1).GetWidget() == Frame);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotEditorSubjectCollectionTest,
	"ComposableCameraSystem.ShotEditor.SubjectCollection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotEditorSubjectCollectionTest::RunTest(const FString&)
{
	if (!GEditor || GEditor->IsTransactionActive() || !FSlateApplication::IsInitialized()) return false;
	TStrongObjectPtr<UComposableCameraShotAsset> Asset(NewObject<UComposableCameraShotAsset>(GetTransientPackage(), NAME_None, RF_Transactional));
	Asset->Shot.Targets.SetNum(2); Asset->Shot.Targets.Shrink();
	Asset->Shot.Targets[1].BoundsContributionWeight = .5f;
	auto Session = MakeShared<FComposableCameraShotAuthoringSession>(); Session->Bind(&Asset->Shot, Asset.Get());
	const auto Authoring = SNew(SShotEditorAuthoringPanel).Session(Session);
	Authoring->SelectSubjectsForTesting();
	Session->OnChanged = [&](bool bStructural) { if (bStructural) Authoring->Refresh(); };
	ON_SCOPE_EXIT { Session->OnChanged = nullptr; };
	const auto FindTagged = [](const TSharedRef<SWidget>& Root, FName Tag)
	{
		TSharedPtr<SWidget> Found;
		TFunction<void(const TSharedRef<SWidget>&)> Walk;
		Walk = [&](const TSharedRef<SWidget>& Widget)
		{
			if (Widget->GetTag() == Tag) { Found = Widget; return; }
			FChildren* Children = Widget->GetChildren();
			for (int32 Index = 0; Index < Children->Num() && !Found; ++Index) Walk(Children->GetChildAt(Index));
		};
		Walk(Root); return Found;
	};
	const auto Click = [&](const TSharedRef<SWidget>& Page, const TCHAR* Tag)
	{
		const auto Widget = FindTagged(Page, FName(Tag));
		if (!TestTrue(TEXT("Production action button exists"), Widget.IsValid())) return false;
		const auto Button = StaticCastSharedPtr<SButton>(Widget);
		if (!TestTrue(TEXT("Production action button is enabled"), Button->IsEnabled())) return false;
		const FKeyEvent Enter(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0);
		Button->OnKeyDown(FGeometry(), Enter);
		return Button->OnKeyUp(FGeometry(), Enter).IsEventHandled();
	};
	const auto Tick = [&]() { Authoring->Tick(FGeometry(), 0., 0.f); };
	const auto Edit = Authoring->GetEditSubjectsForTesting();
	const auto Create = Authoring->GetCreateSubjectsForTesting();
	if (!TestTrue(TEXT("Both production Subjects pages exist"), Edit.IsValid() && Create.IsValid())) return false;
	const auto Add = FindTagged(Edit.ToSharedRef(), TEXT("ShotEditor.Edit.AddSubject"));
	const auto FirstCard = Authoring->GetSubjectCardForTesting(0);
	const auto SecondCard = Authoring->GetSubjectCardForTesting(1);
	const auto CreateFirstCard = Authoring->GetSubjectCardForTesting(0, true);
	if (!TestTrue(TEXT("Existing production subject cards exist"), FirstCard.IsValid() && SecondCard.IsValid() && CreateFirstCard.IsValid())) return false;
	const auto First = StaticCastSharedPtr<SExpandableArea>(FirstCard);
	First->SetExpanded(false);
	const auto Component = StaticCastSharedPtr<SExpandableArea>(FindTagged(FirstCard.ToSharedRef(), TEXT("ShotEditor.Edit.Targets[0].ComponentGroup")));
	if (!TestTrue(TEXT("Production component foldout exists"), Component.IsValid())) return false;
	Component->SetExpanded(true);
	TestTrue(TEXT("Actual Edit Add Subject action succeeds"), Click(Edit.ToSharedRef(), TEXT("ShotEditor.Edit.AddSubject")));
	Tick();
	if (!TestEqual(TEXT("Edit action appends one slot"), Asset->Shot.Targets.Num(), 3)) return false;
	TestTrue(TEXT("Collection change retains Edit and Create page roots"), Edit == Authoring->GetEditSubjectsForTesting() && Create == Authoring->GetCreateSubjectsForTesting());
	TestTrue(TEXT("Add action widget survives append"), Add == FindTagged(Edit.ToSharedRef(), TEXT("ShotEditor.Edit.AddSubject")));
	TestTrue(TEXT("Append retains both existing cards"), FirstCard == Authoring->GetSubjectCardForTesting(0) && SecondCard == Authoring->GetSubjectCardForTesting(1));
	TestTrue(TEXT("Create shares retained-card behavior"), CreateFirstCard == Authoring->GetSubjectCardForTesting(0, true));
	TestFalse(TEXT("Append keeps whole Subject folded"), First->IsExpanded());
	TestTrue(TEXT("Append keeps component subsection open"), Component->IsExpanded());
	const auto Native = Authoring->GetSubjectPanelForTesting(1);
	if (!TestTrue(TEXT("Surviving subject has native parameter panel"), Native.IsValid())) return false;
	const uint32 BoundRows = Native->GetRebuildCountForTesting();
	Native->Tick(FGeometry(), 0., 0.f);
	TestEqual(TEXT("Array controls are rebound in the same refresh tick"), Native->GetRebuildCountForTesting(), BoundRows);
	TestTrue(TEXT("Reallocated target storage has working native handles"), Native->SetSubjectFloatForTesting(GET_MEMBER_NAME_CHECKED(FComposableCameraShotTarget, BoundsContributionWeight), .75f));
	TestEqual(TEXT("Native handle writes correct surviving subject"), Asset->Shot.Targets[1].BoundsContributionWeight, .75f);
	Authoring->SelectCreateForTesting();
	TestTrue(TEXT("Actual Create Add Subject action succeeds"), Click(Create.ToSharedRef(), TEXT("ShotEditor.Create.AddSubject")));
	Tick();
	if (!TestEqual(TEXT("Create action appends one slot"), Asset->Shot.Targets.Num(), 4)) return false;
	Authoring->SelectSubjectsForTesting();
	TestTrue(TEXT("Delete action works while Subject is folded"), Click(Edit.ToSharedRef(), TEXT("ShotEditor.Edit.Targets[0].RemoveSubject")));
	Tick();
	if (!TestEqual(TEXT("Actual Delete action removes subject"), Asset->Shot.Targets.Num(), 3)) return false;
	TestEqual(TEXT("Deleted slot now contains surviving subject"), Asset->Shot.Targets[0].BoundsContributionWeight, .75f);
	TestTrue(TEXT("Delete retains page and earlier role card"), Edit == Authoring->GetEditSubjectsForTesting() && FirstCard == Authoring->GetSubjectCardForTesting(0));
	TestTrue(TEXT("Delete has one-step Undo"), GEditor->UndoTransaction()); Tick();
	if (!TestEqual(TEXT("Undo restores removed slot"), Asset->Shot.Targets.Num(), 4)) return false;
	TestTrue(TEXT("Undo retains page and fold state"), Edit == Authoring->GetEditSubjectsForTesting() && !First->IsExpanded());
	TestTrue(TEXT("Delete has one-step Redo"), GEditor->RedoTransaction()); Tick();
	if (!TestEqual(TEXT("Redo removes slot again"), Asset->Shot.Targets.Num(), 3)) return false;
	TestTrue(TEXT("Shifted native handle edits current slot"), Authoring->GetSubjectPanelForTesting(0)->SetSubjectFloatForTesting(GET_MEMBER_NAME_CHECKED(FComposableCameraShotTarget, BoundsContributionWeight), .6f));
	TestEqual(TEXT("Shifted field write reaches surviving subject"), Asset->Shot.Targets[0].BoundsContributionWeight, .6f);
	TestEqual(TEXT("Shifted field write leaves next subject untouched"), Asset->Shot.Targets[1].BoundsContributionWeight, 1.f);
	Authoring->SelectCreateForTesting();
	const int32 Remaining = Asset->Shot.Targets.Num();
	for (int32 Attempt = 0; Attempt < Remaining; ++Attempt)
	{
		if (!TestTrue(TEXT("Create Delete action can remove through last slot"), Click(Create.ToSharedRef(), TEXT("ShotEditor.Create.Targets[0].RemoveSubject")))) break;
		Tick();
		if (!Session->CanEdit()) break;
	}
	TestEqual(TEXT("Last delete leaves zero subjects"), Asset->Shot.Targets.Num(), 0);
	TestTrue(TEXT("Empty state retains both page roots"), Edit == Authoring->GetEditSubjectsForTesting() && Create == Authoring->GetCreateSubjectsForTesting());
	TestTrue(TEXT("Empty state retains Add button"), Add == FindTagged(Edit.ToSharedRef(), TEXT("ShotEditor.Edit.AddSubject")));
	Authoring->SelectSubjectsForTesting();
	TestTrue(TEXT("Add remains functional after last deletion"), Click(Edit.ToSharedRef(), TEXT("ShotEditor.Edit.AddSubject"))); Tick();
	TestEqual(TEXT("Empty-state action adds new slot"), Asset->Shot.Targets.Num(), 1);
	TestEqual(TEXT("Collection edits do not recreate task pages"), Authoring->GetPageBuildCountForTesting(), 1u);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotEditorSubjectDeleteIconTest,
	"ComposableCameraSystem.ShotEditor.SubjectDeleteIcon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotEditorSubjectDeleteIconTest::RunTest(const FString&)
{
	if (!FSlateApplication::IsInitialized()) return false;
	TStrongObjectPtr<UComposableCameraShotAsset> Asset(NewObject<UComposableCameraShotAsset>());
	Asset->Shot.Targets.SetNum(2);
	auto Session = MakeShared<FComposableCameraShotAuthoringSession>(); Session->Bind(&Asset->Shot, Asset.Get());
	const auto Authoring = SNew(SShotEditorAuthoringPanel).Session(Session);
	for (const bool bCreate : { false, true })
	for (int32 SubjectIndex = 0; SubjectIndex < 2; ++SubjectIndex)
	{
		const auto Card = Authoring->GetSubjectCardForTesting(SubjectIndex, bCreate);
		if (!TestTrue(TEXT("Production Subject card exists"), Card.IsValid())) return false;
		const FName DeleteTag(*FString::Printf(TEXT("ShotEditor.%s.Targets[%d].RemoveSubject"), bCreate ? TEXT("Create") : TEXT("Edit"), SubjectIndex));
		for (const bool bExpanded : { false, true })
		for (const float Dpi : { 1.f, 1.5f, 2.f })
		for (const double Width : { 280., 1200. })
		{
			StaticCastSharedPtr<SExpandableArea>(Card)->SetExpanded(bExpanded);
			Card->Invalidate(EInvalidateWidgetReason::Layout); Card->SlatePrepass(Dpi);
			TOptional<FGeometry> ButtonGeometry, IconGeometry;
			TSharedPtr<SWidget> Button, Icon;
			int32 TextLabels = 0;
			TFunction<void(const TSharedRef<SWidget>&, const FGeometry&, bool)> Arrange;
			Arrange = [&](const TSharedRef<SWidget>& Widget, const FGeometry& Geometry, bool bInDelete)
			{
				if (Widget->GetTag() == DeleteTag) { Button = Widget; ButtonGeometry = Geometry; bInDelete = true; }
				if (bInDelete && Widget->GetType() == FName(TEXT("SImage"))) { Icon = Widget; IconGeometry = Geometry; }
				if (bInDelete && Widget->GetType() == FName(TEXT("STextBlock"))) ++TextLabels;
				FArrangedChildren Children(EVisibility::Visible); Widget->ArrangeChildren(Geometry, Children);
				for (int32 Index = 0; Index < Children.Num(); ++Index) Arrange(Children[Index].Widget, Children[Index].Geometry, bInDelete);
			};
			Arrange(Card.ToSharedRef(), FGeometry::MakeRoot(FVector2D(Width, Card->GetDesiredSize().Y), FSlateLayoutTransform(Dpi)), false);
			if (!TestTrue(TEXT("Delete icon stays visible in expanded and folded headers"), ButtonGeometry.IsSet() && IconGeometry.IsSet())) return false;
			TestEqual(TEXT("Delete uses an icon without a clipped text label"), TextLabels, 0);
			const FVector2D ButtonSize = ButtonGeometry->GetLocalSize(), DesiredButton = Button->GetDesiredSize();
			const FVector2D IconSize = IconGeometry->GetLocalSize(), DesiredIcon = Icon->GetDesiredSize();
			TestTrue(TEXT("Delete button fits its full desired content"), ButtonSize.X + .01 >= DesiredButton.X && ButtonSize.Y + .01 >= DesiredButton.Y);
			TestTrue(TEXT("Delete icon is nonempty and retains its intrinsic size"), DesiredIcon.X > 0. && DesiredIcon.Y > 0. && IconSize.Equals(DesiredIcon, .01));
			const FVector2D ButtonMin = ButtonGeometry->LocalToAbsolute(FVector2D::ZeroVector);
			const FVector2D ButtonMax = ButtonGeometry->LocalToAbsolute(ButtonSize);
			const FVector2D IconMin = IconGeometry->LocalToAbsolute(FVector2D::ZeroVector);
			const FVector2D IconMax = IconGeometry->LocalToAbsolute(IconSize);
			TestTrue(TEXT("Icon stays fully inside its button at every width/DPI"), IconMin.X >= ButtonMin.X && IconMin.Y >= ButtonMin.Y && IconMax.X <= ButtonMax.X && IconMax.Y <= ButtonMax.Y);
			TestTrue(TEXT("Delete icon is centered"), ((ButtonMin + ButtonMax) * .5).Equals((IconMin + IconMax) * .5, .01));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotEditorSubjectHeaderAlignmentTest,
	"ComposableCameraSystem.ShotEditor.SubjectHeaderAlignment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotEditorSubjectHeaderAlignmentTest::RunTest(const FString&)
{
	TStrongObjectPtr<UComposableCameraShotAsset> Asset(NewObject<UComposableCameraShotAsset>());
	Asset->Shot.Targets.SetNum(1);
	auto Session = MakeShared<FComposableCameraShotAuthoringSession>(); Session->Bind(&Asset->Shot, Asset.Get());
	const auto Authoring = SNew(SShotEditorAuthoringPanel).Session(Session);
	for (const bool bCreate : { false, true })
	{
		const auto Page = bCreate ? Authoring->GetCreateSubjectsForTesting() : Authoring->GetEditSubjectsForTesting();
		if (!TestTrue(TEXT("Production Subjects page exists"), Page.IsValid())) return false;
		const FName Tags[] = {
			FName(bCreate ? TEXT("ShotEditor.Create.Targets[0].ComponentGroup") : TEXT("ShotEditor.Edit.Targets[0].ComponentGroup")),
			FName(TEXT("Targets[0].PivotGroup")), FName(TEXT("Targets[0].BoundsGroup")), FName(TEXT("Targets[0].PreviewGroup"))
		};
		TArray<TSharedRef<SExpandableArea>> Groups;
		TFunction<void(const TSharedRef<SWidget>&)> FindGroups;
		FindGroups = [&](const TSharedRef<SWidget>& Widget)
		{
			for (const FName Tag : Tags)
				if (Widget->GetTag() == Tag) { Groups.Add(StaticCastSharedRef<SExpandableArea>(Widget)); break; }
			FChildren* Children = Widget->GetChildren();
			for (int32 Index = 0; Index < Children->Num(); ++Index) FindGroups(Children->GetChildAt(Index));
		};
		FindGroups(Page.ToSharedRef());
		if (!TestEqual(TEXT("All four production subsections exist"), Groups.Num(), 4)) return false;
		for (const bool bExpanded : { false, true })
		for (const float Dpi : { 1.f, 1.5f, 2.f })
		for (const double Width : { 300., 1200. })
		{
			for (const auto& Group : Groups) Group->SetExpanded(bExpanded);
			Page->Invalidate(EInvalidateWidgetReason::Layout); Page->SlatePrepass(Dpi);
			for (const auto& Group : Groups)
			{
				TOptional<FGeometry> Arrow, Title;
				TSharedPtr<SWidget> TitleWidget;
				TFunction<void(const TSharedRef<SWidget>&, const FGeometry&)> ArrangeHeader;
				ArrangeHeader = [&](const TSharedRef<SWidget>& Widget, const FGeometry& Geometry)
				{
					if (!Arrow.IsSet() && Widget->GetType() == FName(TEXT("SImage"))) Arrow = Geometry;
					if (Widget->GetType() == FName(TEXT("STextBlock"))) { Title = Geometry; TitleWidget = Widget; return; }
					FArrangedChildren Children(EVisibility::Visible);
					Widget->ArrangeChildren(Geometry, Children);
					for (int32 Index = 0; Index < Children.Num() && !Title.IsSet(); ++Index) ArrangeHeader(Children[Index].Widget, Children[Index].Geometry);
				};
				ArrangeHeader(Group, FGeometry::MakeRoot(FVector2D(Width, Group->GetDesiredSize().Y), FSlateLayoutTransform(Dpi)));
				if (!TestTrue(TEXT("Native arrow and title are arranged"), Arrow.IsSet() && Title.IsSet())) return false;
				const double ArrowCenter = Arrow->LocalToAbsolute(Arrow->GetLocalSize() * .5).Y;
				const double TitleCenter = Title->LocalToAbsolute(Title->GetLocalSize() * .5).Y;
				TestTrue(TEXT("Arrow and title share vertical center in both pages/states/DPI/widths"), FMath::IsNearlyEqual(ArrowCenter, TitleCenter, .01));
				// A stretched text geometry can share the arrow center while its glyphs still sit at the top.
				const double TitleHeight = Title->GetLocalSize().Y;
				const double LineHeight = TitleWidget->GetDesiredSize().Y;
				TestTrue(TEXT("Title uses intrinsic line height rather than filling the header"),
					FMath::IsNearlyEqual(TitleHeight, LineHeight, .01));
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotEditorSubjectLayoutTest,
	"ComposableCameraSystem.ShotEditor.SubjectLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotEditorSubjectLayoutTest::RunTest(const FString&)
{
	TStrongObjectPtr<UComposableCameraShotAsset> Asset(NewObject<UComposableCameraShotAsset>());
	Asset->Shot.Targets.SetNum(2);
	auto Session = MakeShared<FComposableCameraShotAuthoringSession>();
	Session->Bind(&Asset->Shot, Asset.Get());
	const auto Authoring = SNew(SShotEditorAuthoringPanel).Session(Session);
	Authoring->SelectSubjectsForTesting();
	TSharedPtr<SWidget> Subjects = Authoring->GetEditSubjectsForTesting();
	if (!TestTrue(TEXT("Production Subjects page exists"), Subjects.IsValid())) return false;
	const FName SubjectTag(TEXT("ShotEditor.Edit.Targets[0].SubjectGroup"));
	const FName ComponentTag(TEXT("ShotEditor.Edit.Targets[0].ComponentGroup"));
	const TSet<FName> GroupTags = { ComponentTag, FName(TEXT("Targets[0].PivotGroup")), FName(TEXT("Targets[0].BoundsGroup")), FName(TEXT("Targets[0].PreviewGroup")) };
	const FName OrderedTags[] = { ComponentTag, FName(TEXT("Targets[0].PivotGroup")), FName(TEXT("Targets[0].BoundsGroup")), FName(TEXT("Targets[0].PreviewGroup")) };
	const auto FindTagged = [](const TSharedRef<SWidget>& Root, FName Tag) -> TSharedPtr<SWidget>
	{
		TSharedPtr<SWidget> Found;
		TFunction<void(const TSharedRef<SWidget>&)> Walk;
		Walk = [&](const TSharedRef<SWidget>& Widget)
		{
			if (Widget->GetTag() == Tag) { Found = Widget; return; }
			FChildren* Children = Widget->GetChildren();
			for (int32 Index = 0; Index < Children->Num() && !Found; ++Index) Walk(Children->GetChildAt(Index));
		};
		Walk(Root);
		return Found;
	};
	const auto OuterWidget = FindTagged(Subjects.ToSharedRef(), SubjectTag);
	if (!TestTrue(TEXT("Whole Subject uses a real expandable area"), OuterWidget.IsValid())) return false;
	const auto Outer = StaticCastSharedPtr<SExpandableArea>(OuterWidget);
	TestTrue(TEXT("Subjects start expanded"), Outer->IsExpanded());
	for (const FName Tag : GroupTags)
	{
		const auto Group = FindTagged(Subjects.ToSharedRef(), Tag);
		if (!TestTrue(FString(TEXT("Subject exposes full-width group: ")) + Tag.ToString(), Group.IsValid())) return false;
		StaticCastSharedPtr<SExpandableArea>(Group)->SetExpanded(true);
	}
	for (double Width : { 300., 640., 1200., 380. })
	{
		Subjects->Invalidate(EInvalidateWidgetReason::Layout);
		Subjects->SlatePrepass(1.f);
		TMap<FName, FGeometry> Geometries;
		TFunction<void(const TSharedRef<SWidget>&, const FGeometry&)> Arrange;
		Arrange = [&](const TSharedRef<SWidget>& Widget, const FGeometry& Geometry)
		{
			if (GroupTags.Contains(Widget->GetTag())) Geometries.Add(Widget->GetTag(), Geometry);
			FArrangedChildren Children(EVisibility::Visible);
			Widget->ArrangeChildren(Geometry, Children);
			for (int32 Index = 0; Index < Children.Num(); ++Index) Arrange(Children[Index].Widget, Children[Index].Geometry);
		};
		Arrange(Subjects.ToSharedRef(), FGeometry::MakeRoot(FVector2D(Width, 8000.), FSlateLayoutTransform()));
		TestEqual(TEXT("All four groups arranged in production Subject body"), Geometries.Num(), 4);
		for (const FName Tag : GroupTags)
		{
			const FGeometry* Geometry = Geometries.Find(Tag);
			if (!Geometry) continue;
			TestTrue(FString(TEXT("Group fills whole row at each size: ")) + Tag.ToString(), FMath::IsNearlyEqual(Geometry->GetLocalSize().X, Width - 16., .01));
			TestTrue(TEXT("All four groups share left edge"), FMath::IsNearlyEqual(Geometry->LocalToAbsolute(FVector2D::ZeroVector).X, 8., .01));
		}
		for (int32 Index = 1; Index < UE_ARRAY_COUNT(OrderedTags); ++Index)
		{
			const FGeometry* Previous = Geometries.Find(OrderedTags[Index - 1]);
			const FGeometry* Current = Geometries.Find(OrderedTags[Index]);
			if (Previous && Current)
				TestTrue(TEXT("Groups stack vertically without sharing or overlapping rows"), Current->LocalToAbsolute(FVector2D::ZeroVector).Y
					>= Previous->LocalToAbsolute(Previous->GetLocalSize()).Y - .01);
		}
	}
	Subjects->SlatePrepass(1.f);
	const double ExpandedHeight = Outer->GetDesiredSize().Y;
	Outer->SetExpanded(false);
	Subjects->SlatePrepass(1.f);
	TestTrue(TEXT("Folding entire Subject hides all fields"), Outer->GetDesiredSize().Y < ExpandedHeight * .5);
	const auto Component = StaticCastSharedPtr<SExpandableArea>(FindTagged(Subjects.ToSharedRef(), ComponentTag));
	Component->SetExpanded(false);
	Authoring->Refresh();
	Authoring->Tick(FGeometry(), 0., 0.f);
	Subjects = Authoring->GetEditSubjectsForTesting();
	const auto RefreshedOuter = FindTagged(Subjects.ToSharedRef(), SubjectTag);
	const auto RefreshedComponent = FindTagged(Subjects.ToSharedRef(), ComponentTag);
	if (TestTrue(TEXT("Structural refresh keeps Subject groups"), RefreshedOuter.IsValid() && RefreshedComponent.IsValid()))
	{
		TestFalse(TEXT("Whole Subject fold survives refresh"), StaticCastSharedPtr<SExpandableArea>(RefreshedOuter)->IsExpanded());
		TestFalse(TEXT("Component fold survives refresh"), StaticCastSharedPtr<SExpandableArea>(RefreshedComponent)->IsExpanded());
		StaticCastSharedPtr<SExpandableArea>(RefreshedOuter)->SetExpanded(true);
		TestTrue(TEXT("Whole Subject can reopen"), StaticCastSharedPtr<SExpandableArea>(RefreshedOuter)->IsExpanded());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotEditorCompactActionsTest,
	"ComposableCameraSystem.ShotEditor.CompactActions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotEditorCompactActionsTest::RunTest(const FString&)
{
	using namespace ComposableCameraSystem::ShotEditorStyle;
	const auto Button = SNew(SButton).Text(FText::FromString(TEXT("Use Selected Actors")));
	const auto ActionWidget = Action(Button);
	ActionWidget->SlatePrepass(1.f);
	for (FVector2D Available : { FVector2D(300, 80), FVector2D(1400, 80) })
	{
		FArrangedChildren Outer(EVisibility::All);
		ActionWidget->ArrangeChildren(FGeometry::MakeRoot(Available, FSlateLayoutTransform()), Outer);
		if (!TestEqual(TEXT("Action wrapper contains one fixed-size child"), Outer.Num(), 1)) continue;
		FArrangedChildren Inner(EVisibility::All);
		Outer[0].Widget->ArrangeChildren(Outer[0].Geometry, Inner);
		if (!TestEqual(TEXT("Fixed wrapper contains button"), Inner.Num(), 1)) continue;
		const FVector2D ButtonSize = Inner[0].Geometry.GetLocalSize();
		TestTrue(TEXT("Action stays compact even in a wide fill slot"), ButtonSize.Equals(FVector2D(156, 24), .01));
		FArrangedChildren Content(EVisibility::All);
		Button->ArrangeChildren(Inner[0].Geometry, Content);
		if (!TestEqual(TEXT("Button content arranged"), Content.Num(), 1)) continue;
		const FVector2D TextSize = Content[0].Geometry.GetLocalSize();
		const FVector2D ButtonCenter = Inner[0].Geometry.LocalToAbsolute(ButtonSize * .5);
		const FVector2D TextCenter = Content[0].Geometry.LocalToAbsolute(TextSize * .5);
		TestTrue(TEXT("Action caption is centered horizontally and vertically"), ButtonCenter.Equals(TextCenter, .01));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotEditorCompactNavigationTest,
	"ComposableCameraSystem.ShotEditor.CompactNavigation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotEditorCompactNavigationTest::RunTest(const FString&)
{
	auto Session = MakeShared<FComposableCameraShotAuthoringSession>();
	const auto Panel = SNew(SShotEditorAuthoringPanel).Session(Session);
	const auto Navigation = Panel->BuildNavigation();
	Navigation->SlatePrepass(1.f);
	const FVector2D Available(1400, 80);
	int32 Found = 0;
	TFunction<void(const TSharedRef<SWidget>&, const FGeometry&)> Inspect;
	Inspect = [&](const TSharedRef<SWidget>& Widget, const FGeometry& Geometry)
	{
		if (Widget->GetTag() == FName(TEXT("ShotEditor.LevelPreview")))
		{
			++Found;
			const FVector2D ButtonSize = Geometry.GetLocalSize();
			const FVector2D Origin = Geometry.LocalToAbsolute(FVector2D::ZeroVector);
			TestTrue(TEXT("Level Preview has compact fixed size"), ButtonSize.Equals(FVector2D(104, 24), .01));
			TestTrue(TEXT("Level Preview remains beside playhead at right of navigation"), Origin.X > Available.X - 240);
			FArrangedChildren Text(EVisibility::All);
			Widget->ArrangeChildren(Geometry, Text);
			if (TestEqual(TEXT("Level Preview caption arranged"), Text.Num(), 1))
			{
				const FVector2D TextSize = Text[0].Geometry.GetLocalSize();
				TestTrue(TEXT("Actual Level Preview caption is centered"), Geometry.LocalToAbsolute(ButtonSize * .5)
					.Equals(Text[0].Geometry.LocalToAbsolute(TextSize * .5), .01));
			}
		}
		FArrangedChildren Children(EVisibility::All);
		Widget->ArrangeChildren(Geometry, Children);
		for (int32 Index = 0; Index < Children.Num(); ++Index) Inspect(Children[Index].Widget, Children[Index].Geometry);
	};
	Inspect(Navigation, FGeometry::MakeRoot(Available, FSlateLayoutTransform()));
	TestEqual(TEXT("Exactly one Level Preview action in real navigation"), Found, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotEditorParameterPageContentsTest,
	"ComposableCameraSystem.ShotEditor.ParameterPageContents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotEditorParameterPageContentsTest::RunTest(const FString&)
{
	TStrongObjectPtr<UComposableCameraShotAsset> Asset(NewObject<UComposableCameraShotAsset>());
	Asset->Shot.Targets.SetNum(2);
	auto Session = MakeShared<FComposableCameraShotAuthoringSession>();
	Session->Bind(&Asset->Shot, Asset.Get());
	const auto Authoring = SNew(SShotEditorAuthoringPanel).Session(Session);
	const auto CountTags = [](const TSharedRef<SWidget>& Root)
	{
		TMap<FName, int32> Counts;
		TFunction<void(const TSharedRef<SWidget>&)> Inspect;
		Inspect = [&](const TSharedRef<SWidget>& Widget)
		{
			++Counts.FindOrAdd(Widget->GetTag());
			FChildren* Children = Widget->GetChildren();
			for (int32 Index = 0; Index < Children->Num(); ++Index) Inspect(Children->GetChildAt(Index));
		};
		Inspect(Root);
		return Counts;
	};
	TArray<TSharedPtr<SShotEditorParameterPanel>> Panels;
	Authoring->GetEditParameterPanelsForTesting(Panels);
	for (const auto& Panel : Panels)
	{
		for (EShotPlacementMode Mode : { EShotPlacementMode::AnchorOrbit, EShotPlacementMode::AnchorAtScreen, EShotPlacementMode::FixedWorldPosition })
		{
			Asset->Shot.Placement.Mode = Mode;
			for (EShotAimMode AimMode : { EShotAimMode::LookAtAnchor, EShotAimMode::NoOp })
			{
				Asset->Shot.Aim.Mode = AimMode;
				Panel->RefreshForTesting();
				TSet<FString> Expected;
				Panel->GetRenderedPropertyPathsForTesting(Expected);
				const TMap<FName, int32> Actual = CountTags(Panel.ToSharedRef());
				TestTrue(TEXT("Current section generates native fields"), Expected.Num() > 0);
				for (const FString& Path : Expected)
					TestEqual(FString(TEXT("Generated field is attached exactly once in the left page: ")) + Path, Actual.FindRef(FName(*Path)), 1);
			}
		}
	}
	Authoring->SelectCreateForTesting();
	Authoring->SlatePrepass(1.f);
	TestEqual(TEXT("Create page contains template configuration"), CountTags(Authoring).FindRef(FName(TEXT("ShotEditor.Template"))), 1);
	Authoring->SelectSequenceForTesting();
	Authoring->SlatePrepass(1.f);
	TestEqual(TEXT("Sequence page contains its destination picker"), CountTags(Authoring).FindRef(FName(TEXT("ShotEditor.Destination"))), 1);
	Authoring->SelectPresetsForTesting();
	Authoring->SlatePrepass(1.f);
	TestEqual(TEXT("Presets page contains apply/restore configuration"), CountTags(Authoring).FindRef(FName(TEXT("ShotEditor.Preset"))), 1);
	TestEqual(TEXT("Navigation and mode changes retain authoring pages"), Authoring->GetPageBuildCountForTesting(), 1u);
	return true;
}

#endif
