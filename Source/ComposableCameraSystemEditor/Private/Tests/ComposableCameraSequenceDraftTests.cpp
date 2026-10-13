// Copyright 2026 Sulley. All Rights Reserved.
#include "Editors/ComposableCameraShotAuthoringSession.h"
#include "Editors/ComposableCameraInstanceEditorSession.h"
#include "Editors/ComposableCameraShotEditorViewportClient.h"
#include "Sequencer/ComposableCameraInstanceDragDropOp.h"
#include "Sequencer/ComposableCameraInstanceTrackEditor.h"
#include "Widgets/SShotEditorAuthoringPanel.h"
#include "Widgets/SShotEditorSequenceDragButton.h"
#include "Widgets/SShotEditorSubjectPicker.h"
#include "Widgets/SShotEditorViewport.h"
#include "Widgets/ComposableCameraShotEditorStyle.h"
#include "MovieScene/MovieSceneComposableCameraInstanceTrack.h"
#include "MovieScene/MovieSceneComposableCameraInstanceSection.h"
#include "MovieScene/MovieSceneComposableCameraShotTrack.h"
#include "LevelSequence/ComposableCameraLevelSequenceActor.h"
#include "LevelSequence/ComposableCameraLevelSequenceShotActor.h"
#include "CineCameraComponent.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "ISequencer.h"
#include "ISequencerModule.h"
#include "LevelSequence.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "MovieScene.h"
#include "MovieSceneSequence.h"
#include "Sections/MovieSceneBoolSection.h"
#include "Tracks/MovieSceneSpawnTrack.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"
#include "Modules/ModuleManager.h"
#include "Layout/ArrangedChildren.h"
#include "Widgets/Text/STextBlock.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
template<typename T> TSharedPtr<T> FindDraftWidget(const TSharedRef<SWidget>& Widget, FName Type)
{
	if (Widget->GetType() == Type) return StaticCastSharedRef<T>(Widget);
	FChildren* Children = Widget->GetChildren();
	for (int32 Index = 0; Index < Children->Num(); ++Index)
		if (const auto Found = FindDraftWidget<T>(Children->GetChildAt(Index), Type)) return Found;
	return nullptr;
}
bool DraftMenuHasText(const TSharedRef<SWidget>& Widget, const FString& Text)
{
	if (Widget->GetType() == FName(TEXT("STextBlock")) && StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString().Contains(Text)) return true;
	FChildren* Children = Widget->GetChildren();
	for (int32 Index = 0; Index < Children->Num(); ++Index)
		if (DraftMenuHasText(Children->GetChildAt(Index), Text)) return true;
	return false;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraSequenceDraftTest,
	"ComposableCameraSystem.CameraInstance.SequenceDraft", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCameraSequenceDraftTest::RunTest(const FString&)
{
	if (!GEditor || !GEngine || GEditor->IsTransactionActive() || !FSlateApplication::IsInitialized()) return false;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
	World->InitializeActorsForPlay(FURL()); World->BeginPlay();
	TStrongObjectPtr<ULevelSequence> Sequence(NewObject<ULevelSequence>(GetTransientPackage(), NAME_None, RF_Transactional));
	Sequence->Initialize();
	UMovieScene* Scene = Sequence->GetMovieScene();
	Scene->SetTickResolutionDirectly(FFrameRate(24, 1)); Scene->SetDisplayRate(FFrameRate(24, 1)); Scene->SetPlaybackRange(0, 1000);
	FSequencerInitParams Params;
	Params.RootSequence = Sequence.Get(); Params.PlaybackContext = World;
	Params.ViewParams.UniqueName = TEXT("SequenceDraftRegression");
	const auto Sequencer = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer").CreateSequencer(Params);
	const auto PreviousAuto = Sequencer->GetAutoChangeMode(); Sequencer->SetAutoChangeMode(EAutoChangeMode::None);
	ON_SCOPE_EXIT { Sequencer->SetAutoChangeMode(PreviousAuto); Sequencer->Close(); };
	auto* SourceActor = World->SpawnActor<AStaticMeshActor>();
	if (!SourceActor) return false;
	const FGuid Subject = Sequencer->MakeNewSpawnable(*SourceActor, nullptr, false);
	SourceActor->Destroy();
	if (!TestTrue(TEXT("Fixture has a real Sequencer Spawnable"), Subject.IsValid())) return false;
	Scene->SetObjectDisplayName(Subject, FText::FromString(TEXT("Draft Spawn Hero")));
	auto* SpawnTrack = Scene->FindTrack<UMovieSceneSpawnTrack>(Subject);
	if (!SpawnTrack) SpawnTrack = Scene->AddTrack<UMovieSceneSpawnTrack>(Subject);
	if (!SpawnTrack) return false;
	auto* SpawnSection = CastChecked<UMovieSceneBoolSection>(SpawnTrack->CreateNewSection());
	SpawnSection->SetRange(TRange<FFrameNumber>(0, 1000)); SpawnSection->GetChannel().SetDefault(true); SpawnTrack->AddSection(*SpawnSection);
	Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
	Sequencer->SetLocalTimeDirectly(FFrameTime(0)); Sequencer->ForceEvaluate();
	const int32 OriginalBindings = Scene->GetBindings().Num();
	const int32 OriginalTracks = Scene->GetTracks().Num();
	const auto Session = MakeShared<FComposableCameraShotAuthoringSession>();
	FString Error;
	if (!TestTrue(TEXT("Start an LS draft"), Session->StartSequenceDraft(Sequencer, Error))) return false;
	TestTrue(TEXT("Draft edits the existing instance data model"), Session->IsSequenceDraft() && Session->GetInstanceValues() == Session->GetHost());
	TestEqual(TEXT("Draft creates no binding"), Scene->GetBindings().Num(), OriginalBindings);
	TestEqual(TEXT("Draft creates no track"), Scene->GetTracks().Num(), OriginalTracks);
	TestNull(TEXT("Draft preview creates no output camera actor"), Session->GetOutputCamera());
	TestEqual(TEXT("Empty draft has no implicit Subject"), Session->GetShot()->Targets.Num(), 0);
	if (!TestTrue(TEXT("Add a Subject after choosing an empty draft"), Session->AddTarget(nullptr, Error))) return false;
	const auto Panel = SNew(SShotEditorAuthoringPanel).Session(Session).PersistViewState(false);
	const auto Card = Panel->GetSubjectCardForTesting(0);
	if (!Card) return false;
	const auto Picker = FindDraftWidget<SShotEditorSubjectPicker>(Card.ToSharedRef(), TEXT("SShotEditorSubjectPicker"));
	if (!Picker) return false;
	TestTrue(TEXT("Draft menu lists sequence subjects"), DraftMenuHasText(Picker->BuildMenuForTesting(), TEXT("Draft Spawn Hero")));
	if (!TestTrue(TEXT("Draft picker writes binding identity"), Picker->ChooseBindingForTesting(Subject))) return false;
	AActor* Live = Session->ResolveTarget(0);
	if (!TestNotNull(TEXT("Draft resolves the live Spawnable"), Live)) return false;
	const TWeakObjectPtr<AActor> PreviousActor = Live;
	Sequencer->SetLocalTimeDirectly(FFrameTime(1100)); Sequencer->ForceEvaluate();
	TestNull(TEXT("Outside spawn range the binding resolves nothing"), Session->ResolveTarget(0));
	TestTrue(TEXT("Inactive subject remains in the menu"), DraftMenuHasText(Picker->BuildMenuForTesting(), TEXT("Draft Spawn Hero")));
	TestTrue(TEXT("Inactive subject retains its ID"), Session->GetTargetBinding(0).GetGuid() == Subject);
	Sequencer->SetLocalTimeDirectly(FFrameTime(24)); Sequencer->ForceEvaluate();
	TestNotNull(TEXT("Scrubbing back resolves regenerated subject"), Session->ResolveTarget(0));
	TestTrue(TEXT("Respawn replaced the actor identity"), !PreviousActor.IsValid() || PreviousActor.Get() != Session->ResolveTarget(0));
	TestTrue(TEXT("No live actor serialized in draft"), Session->GetShot()->Targets[0].Target.Actor.IsNull());
	Session->BeginEdit(FText::FromString(TEXT("Compose draft")));
	Session->GetShot()->Lens.FOVMode = EShotFOVMode::Manual;
	Session->GetShot()->Lens.ManualFOV = 63.f;
	Session->GetShot()->Targets[0].Target.Offset = FVector(7, 8, 9);
	Session->Changed(); Session->EndEdit();
	const auto Preview = SNew(SShotEditorViewport);
	Preview->SetAuthoringSession(Session); Preview->SetActiveShot(Session->GetShot(), Session->GetHost());
	const auto Client = Preview->GetClientForTesting();
	if (!Client) return false;
	Client->Tick(0.f);
	TestEqual(TEXT("Draft solver previews authored FOV before insertion"), Client->ViewFOV, 63.f);
	FComposableCameraShot Effective;
	TestTrue(TEXT("Preview builds a resolved evaluation copy"), Client->BuildEffectiveShotForPreview(Effective));
	TestTrue(TEXT("Preview receives current LS actor without persisting it"), Effective.Targets[0].Target.Actor.Get() == Session->ResolveTarget(0)
		&& Session->GetShot()->Targets[0].Target.Actor.IsNull());
	const auto Button = SNew(SShotEditorSequenceDragButton).Session(Session).Duration(3.);
	auto Drag = Button->MakeOperation();
	if (!TestTrue(TEXT("Production drag button creates payload"), Drag.IsValid())) return false;
	TestTrue(TEXT("Native transport resolves transient snapshot"), Drag->GetAssets().Num() == 1 && Drag->GetAssets()[0].GetAsset() == Drag->GetSnapshot());
	CollectGarbage(RF_NoFlags);
	TestEqual(TEXT("Drag keeps copied configuration alive through GC"), Drag->GetSnapshot()->Shot.Lens.ManualFOV, 63.f);
	TestTrue(TEXT("Cancelled drag leaves draft and bindings intact"), Session->IsSequenceDraft() && Scene->GetBindings().Num() == OriginalBindings);
	const auto* CancelledSnapshot = Drag->GetSnapshot();
	Drag.Reset();
	TestFalse(TEXT("Cancelled payload leaves no registered live drag"), FComposableCameraInstanceDragDropOp::FindForSnapshot(CancelledSnapshot).IsValid());
	Drag = Button->MakeOperation();
	if (!Drag) return false;
	Scene->SetReadOnly(true); TestFalse(TEXT("Read-only destination is refused"), Drag->CanDrop(*Sequencer, Error)); Scene->SetReadOnly(false);
	{
		TStrongObjectPtr<ULevelSequence> Other(NewObject<ULevelSequence>()); Other->Initialize();
		FSequencerInitParams OtherParams; OtherParams.RootSequence = Other.Get(); OtherParams.PlaybackContext = World;
		OtherParams.ViewParams.UniqueName = TEXT("DraftWrongDestination");
		const auto OtherSequencer = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer").CreateSequencer(OtherParams);
		TestFalse(TEXT("Foreign sequence cannot reinterpret local Subject IDs"), Drag->CanDrop(*OtherSequencer, Error));
		OtherSequencer->Close();
	}
	TStrongObjectPtr<UMovieSceneComposableCameraInstanceTrack> Type(NewObject<UMovieSceneComposableCameraInstanceTrack>(Scene));
	const auto Editor = Sequencer->GetTrackEditor(Type.Get());
	if (!TestTrue(TEXT("Instance editor registered"), Editor.IsValid())) return false;
	// Exactly the fallback used for empty Sequencer space, after Slate has cleared its drag pointer.
	if (!TestTrue(TEXT("Native empty-area asset dispatch consumes the snapshot"), Editor->HandleAssetAdded(Drag->GetAssets()[0].GetAsset(), Subject))) return false;
	auto* Track = Session->GetInstanceTrack();
	if (!TestNotNull(TEXT("Drop transfers editing to the created instance"), Track)) return false;
	TestFalse(TEXT("Editing leaves draft mode"), Session->IsSequenceDraft());
	TestEqual(TEXT("Exactly one managed binding created"), Scene->GetBindings().Num(), OriginalBindings + 1);
	TestTrue(TEXT("Instance has its own binding, ignoring selected subject"), Track->FindObjectBindingGuid().IsValid() && Track->FindObjectBindingGuid() != Subject);
	TestTrue(TEXT("Duration and playhead carried to Activation"), Track->GetAllSections()[0]->GetRange() == TRange<FFrameNumber>(24, 96));
	TestEqual(TEXT("No folder"), Scene->GetRootFolders().Num(), 0);
	TestEqual(TEXT("FOV copied"), Track->Instance->Shot.Lens.ManualFOV, 63.f);
	TestTrue(TEXT("Offset copied"), Track->Instance->Shot.Targets[0].Target.Offset == FVector(7, 8, 9));
	TestTrue(TEXT("Subject identity copied"), Session->GetTargetBinding(0).GetGuid() == Subject);
	const auto* Camera = Session->GetOutputCamera();
	if (TestNotNull(TEXT("Preview transfers to managed output"), Camera))
	{
		TestTrue(TEXT("Output uses generic LSActor"), Camera->GetOwner()->IsA<AComposableCameraLevelSequenceActor>());
		TestFalse(TEXT("No specialized ShotActor"), Camera->GetOwner()->IsA<AComposableCameraLevelSequenceShotActor>());
	}
	for (const auto& Binding : Scene->GetBindings())
		for (const auto* Child : Binding.GetTracks()) TestFalse(TEXT("Draft never creates a legacy Shot Track"), Child->IsA<UMovieSceneComposableCameraShotTrack>());
	const FGuid OutputGuid = Track->FindObjectBindingGuid();
	TestTrue(TEXT("Creation can undo"), GEditor->UndoTransaction());
	TestNull(TEXT("Undo removes generated output binding"), Scene->FindBinding(OutputGuid));
	TestTrue(TEXT("Creation can redo"), GEditor->RedoTransaction());
	TestNotNull(TEXT("Redo restores output binding"), Scene->FindBinding(OutputGuid));
	Sequencer->ForceEvaluate(); Session->Tick();
	Session->Bind(&Track->Instance->Shot, Track->Instance);
	TestEqual(TEXT("Reopen samples FOV"), Session->GetShot()->Lens.ManualFOV, 63.f);
	TestTrue(TEXT("Reopen resolves Subject"), Session->ResolveTarget(0) != nullptr && Session->GetTargetBinding(0).GetGuid() == Subject);
	Button->SlatePrepass();
	TestFalse(TEXT("Bound instance disables the drag button"), Button->IsEnabled());
	TestFalse(TEXT("Bound instance cannot start a drag payload"), Button->MakeOperation().IsValid());
	if (!TestTrue(TEXT("A new draft restores insertion"), Session->StartSequenceDraft(Sequencer, Error))) return false;
	const auto Second = Button->MakeOperation();
	if (!Second) return false;
	FDragDropEvent Event(FPointerEvent(), Second.ToSharedRef());
	FSequencerDragDropParams DropParams(Track, 0, OutputGuid, FFrameNumber(200), TRange<FFrameNumber>());
	TestTrue(TEXT("Existing track accepts draft transport"), Editor->OnAllowDrop(Event, DropParams));
	// STrackAreaView::OnPaint reads these endpoints immediately on hover, before OnDrop.
	if (!TestTrue(TEXT("Handled hover supplies both preview bounds"), DropParams.FrameRange.HasLowerBound() && DropParams.FrameRange.HasUpperBound())) return false;
	TestEqual(TEXT("Hover starts at pointer frame"), DropParams.FrameRange.GetLowerBoundValue().Value, 200);
	TestEqual(TEXT("Hover includes the snapshot's three-second duration"), DropParams.FrameRange.GetUpperBoundValue().Value, 272);
	const int32 BindingsBeforeHover = Scene->GetBindings().Num();
	FSequencerDragDropParams NegativeParams(Track, 0, OutputGuid, FFrameNumber(-48), TRange<FFrameNumber>());
	TestTrue(TEXT("Hover supports negative sequence time"), Editor->OnAllowDrop(Event, NegativeParams));
	TestTrue(TEXT("Negative hover interval is finite"), NegativeParams.FrameRange == TRange<FFrameNumber>(-48, 24));
	Scene->SetReadOnly(true);
	FSequencerDragDropParams ReadOnlyParams(Track, 0, OutputGuid, FFrameNumber(200), TRange<FFrameNumber>());
	TestTrue(TEXT("Read-only own drag is consumed to prevent fallback"), Editor->OnAllowDrop(Event, ReadOnlyParams));
	Scene->SetReadOnly(false);
	if (!TestTrue(TEXT("Rejected hover still supplies painter-safe bounds"), ReadOnlyParams.FrameRange.HasLowerBound() && ReadOnlyParams.FrameRange.HasUpperBound())) return false;
	TestEqual(TEXT("Rejected hover lower endpoint"), ReadOnlyParams.FrameRange.GetLowerBoundValue().Value, 200);
	TestEqual(TEXT("Rejected hover upper endpoint"), ReadOnlyParams.FrameRange.GetUpperBoundValue().Value, 200);
	FSequencerDragDropParams OverflowParams(Track, 0, OutputGuid, FFrameNumber(MAX_int32 - 10), TRange<FFrameNumber>());
	TestTrue(TEXT("Overflowing own drag is consumed"), Editor->OnAllowDrop(Event, OverflowParams));
	TestTrue(TEXT("Overflow preview remains a bounded empty interval"), OverflowParams.FrameRange.HasLowerBound()
		&& OverflowParams.FrameRange.HasUpperBound() && OverflowParams.FrameRange.IsEmpty());
	TestEqual(TEXT("Hover validation creates no binding"), Scene->GetBindings().Num(), BindingsBeforeHover);
	Editor->OnAllowDrop(Event, DropParams);
	TestTrue(TEXT("Native track-area dispatch handles drop"), Editor->OnDrop(Event, DropParams).IsEventHandled());
	if (!TestTrue(TEXT("Dropping onto a track still creates another instance"), Session->GetInstanceTrack() && Session->GetInstanceTrack() != Track)) return false;
	TestEqual(TEXT("Previous Activation is unchanged"), Track->GetAllSections().Num(), 1);
	TestTrue(TEXT("Created activation matches the hover preview"), Session->GetInstanceTrack()->GetAllSections()[0]->GetRange() == DropParams.FrameRange);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotSequenceActionWidthTest,
	"ComposableCameraSystem.ShotEditor.SequenceActionWidth", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotSequenceActionWidthTest::RunTest(const FString&)
{
	for (const TCHAR* Caption : { TEXT("Add Shot to Sequence"), TEXT("Create Dialogue Set"), TEXT("Duplicate after current"), TEXT("Create Empty Shot"), TEXT("Go to Clip"), TEXT("Duplicate At Playhead") })
	{
		const auto Text = SNew(STextBlock).Text(FText::FromString(Caption));
		const auto Button = SNew(SButton)[Text];
		const auto Action = ComposableCameraSystem::ShotEditorStyle::AutoSizedAction(Button);
		for (float Scale : {1.f, 1.5f, 2.f})
		{
			Action->SlatePrepass(Scale);
			FArrangedChildren Outer(EVisibility::All), Inner(EVisibility::All), Content(EVisibility::All);
			Action->ArrangeChildren(FGeometry::MakeRoot(FVector2D(420., 100.), FSlateLayoutTransform(Scale)), Outer);
			if (Outer.Num() != 1) return false;
			Outer[0].Widget->ArrangeChildren(Outer[0].Geometry, Inner);
			if (Inner.Num() != 1) return false;
			Button->ArrangeChildren(Inner[0].Geometry, Content);
			if (Content.Num() != 1) return false;
			TestTrue(TEXT("Full caption fits its content allocation at each DPI"), Content[0].Geometry.GetLocalSize().X + .1 >= Text->GetDesiredSize().X);
			TestTrue(TEXT("Button contains text plus both horizontal paddings"), Inner[0].Geometry.GetLocalSize().X + .1 >= Text->GetDesiredSize().X + 20.);
		}
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotClipActionAlignmentTest,
	"ComposableCameraSystem.ShotEditor.ClipActionAlignment", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotClipActionAlignmentTest::RunTest(const FString&)
{
	const auto Session = MakeShared<FComposableCameraShotAuthoringSession>();
	const auto Panel = SNew(SShotEditorAuthoringPanel).Session(Session).PersistViewState(false);
	Panel->SelectSequenceForTesting(); Panel->SlatePrepass();
	TFunction<TSharedPtr<SWidget>(TSharedRef<SWidget>)> FindColumn;
	FindColumn = [&](TSharedRef<SWidget> Widget) -> TSharedPtr<SWidget>
	{
		if (Widget->GetTag() == FName(TEXT("ShotEditor.ClipActions"))) return Widget;
		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Index < Children->Num(); ++Index)
			if (const auto Found = FindColumn(Children->GetChildAt(Index))) return Found;
		return nullptr;
	};
	const auto Column = FindColumn(Panel);
	if (!TestTrue(TEXT("Production Clip action group exists"), Column.IsValid())) return false;
	for (float Width : {180.f, 300.f, 420.f}) for (float Scale : {1.f, 1.5f, 2.f})
	{
		Column->SlatePrepass(Scale);
		TMap<FName, FGeometry> Arranged;
		TFunction<void(TSharedRef<SWidget>, const FGeometry&)> Collect;
		Collect = [&](TSharedRef<SWidget> Widget, const FGeometry& Geometry)
		{
			if (!Widget->GetTag().IsNone()) Arranged.Add(Widget->GetTag(), Geometry);
			FArrangedChildren Children(EVisibility::Visible); Widget->ArrangeChildren(Geometry, Children);
			for (int32 Index = 0; Index < Children.Num(); ++Index) Collect(Children[Index].Widget, Children[Index].Geometry);
		};
		Collect(Column.ToSharedRef(), FGeometry::MakeRoot(FVector2D(Width, 800.), FSlateLayoutTransform(Scale)));
		for (const TCHAR* Tag : {TEXT("ShotEditor.NewDraft"), TEXT("ShotEditor.GoToClip"), TEXT("ShotEditor.DuplicateAtPlayhead"), TEXT("ShotEditor.DragRow")})
		{
			const auto* Geometry = Arranged.Find(FName(Tag));
			if (!TestNotNull(TEXT("Production action participates in layout"), Geometry)) return false;
			TestTrue(TEXT("All actions share the left edge at every width/DPI"), FMath::Abs(static_cast<double>(Geometry->GetAbsolutePosition().X)) < .1);
			TestTrue(TEXT("All action rows share the full column width"), FMath::Abs(static_cast<double>(Geometry->GetLocalSize().X) - Width) < .1);
			TestTrue(TEXT("Actions retain a usable height"), Geometry->GetLocalSize().Y >= 28.f);
		}
	}
	return true;
}
#endif
