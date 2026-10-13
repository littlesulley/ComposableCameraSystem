// Copyright 2026 Sulley. All Rights Reserved.
#include "Editors/ComposableCameraInstanceEditorSession.h"
#include "Editors/ComposableCameraShotAuthoringSession.h"
#include "DataAssets/ComposableCameraShotAsset.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Nodes/ComposableCameraLensNode.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Actor.h"
#include "ISequencer.h"
#include "ISequencerModule.h"
#include "LevelSequence.h"
#include "Misc/AutomationTest.h"
#include "MovieScene.h"
#include "MovieScene/MovieSceneComposableCameraInstanceTrack.h"
#include "MovieScene/MovieSceneComposableCameraInstanceParameterTrack.h"
#include "MovieScene/MovieSceneComposableCameraInstanceSection.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "TickableEditorObject.h"
#include "PropertyHandle.h"
#include "Widgets/SShotEditorRoot.h"
#include "Widgets/SShotEditorAuthoringPanel.h"
#include "Widgets/SShotEditorParameterPanel.h"
#include "UObject/StrongObjectPtr.h"
#include "Channels/BuiltInChannelEditors.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "LevelSequence/ComposableCameraLevelSequenceActor.h"
#include "LevelSequence/ComposableCameraLevelSequenceComponent.h"
#include "Misc/ScopeExit.h"
#include "Sequencer/ComposableCameraInstanceTrackEditor.h"
#include "Sequencer/ComposableCameraInstanceOutput.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "MVVM/ObjectBindingModelStorageExtension.h"
#include "MVVM/ViewModels/ObjectBindingModel.h"
#include "MVVM/ViewModels/SequencerEditorViewModel.h"
#include "Widgets/Text/STextBlock.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
struct FInstanceAuthoringFixture
{
	TStrongObjectPtr<ULevelSequence> Sequence{NewObject<ULevelSequence>(GetTransientPackage(), NAME_None, RF_Transactional)};
	TStrongObjectPtr<UComposableCameraShotAsset> Preset{NewObject<UComposableCameraShotAsset>()};
	TSharedPtr<ISequencer> Sequencer;
	UMovieSceneComposableCameraInstanceTrack* Track = nullptr;
	UMovieSceneComposableCameraInstanceSection* Activation = nullptr;
	FGuid Binding;
	EAutoChangeMode PreviousMode;
	explicit FInstanceAuthoringFixture(UObject* PlaybackContext = nullptr)
	{
		Sequence->Initialize();
		auto* Scene = Sequence->GetMovieScene();
		Binding = Scene->AddPossessable(TEXT("Test Shot"), AActor::StaticClass());
		Track = Scene->AddTrack<UMovieSceneComposableCameraInstanceTrack>(Binding);
		Preset->Shot.Targets.SetNum(2); Preset->Shot.Lens.ManualFOV = 50.f;
		Track->Instance->InitializeFromShot(Preset->Shot, Preset.Get());
		Track->OutputBinding = UE::MovieScene::FRelativeObjectBindingID(Binding);
		Activation = CastChecked<UMovieSceneComposableCameraInstanceSection>(Track->CreateNewSection());
		Activation->SetRange(TRange<FFrameNumber>(0, 100)); Track->AddSection(*Activation);
		FSequencerInitParams Params;
		Params.RootSequence = Sequence.Get();
		if (PlaybackContext) Params.PlaybackContext = PlaybackContext;
		Params.ViewParams.UniqueName = TEXT("CameraInstanceAuthoringRegression");
		Sequencer = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer").CreateSequencer(Params);
		PreviousMode = Sequencer->GetAutoChangeMode();
		Sequencer->SetAutoChangeMode(EAutoChangeMode::None);
		Sequencer->SetLocalTimeDirectly(FFrameTime(0));
	}
	~FInstanceAuthoringFixture() { Sequencer->SetAutoChangeMode(PreviousMode); Sequencer->Close(); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraInstanceHierarchyTest,
	"ComposableCameraSystem.CameraInstance.NativeHierarchy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCameraInstanceHierarchyTest::RunTest(const FString&)
{
	if (!GEditor || GEditor->IsTransactionActive() || !FSlateApplication::IsInitialized()) return false;
	using namespace ComposableCameraSystem::CameraInstanceAuthoring;
	FInstanceAuthoringFixture F;
	auto* Scene = F.Sequence->GetMovieScene();
	TestTrue(TEXT("Configuration is attached below the Shot binding"), F.Track->FindObjectBindingGuid() == F.Binding && F.Track->IsInMovieScene());
	TestFalse(TEXT("No parallel root instance track"), Scene->GetTracks().Contains(F.Track));
	TestEqual(TEXT("No generated folder"), Scene->GetRootFolders().Num(), 0);
	auto* Parameter = AddParameterTrack(*F.Track, TEXT("Shot.Lens.ManualFOV"), FFrameTime(0));
	if (!TestNotNull(TEXT("Root + creates parameter track"), Parameter)) return false;
	TestTrue(TEXT("Repeated insertion reuses this instance's parameter lane"), AddParameterTrack(*F.Track, Parameter->Input, FFrameTime(0)) == Parameter);
	TestTrue(TEXT("Parameter is a sibling under the same Shot"), Parameter->FindObjectBindingGuid() == F.Binding);
	auto* Section = CastChecked<UMovieSceneComposableCameraInstanceSection>(Parameter->GetAllSections()[0]);
	TestTrue(TEXT("First parameter section is unbounded"), Section->GetRange() == TRange<FFrameNumber>::All());
	TestTrue(TEXT("Native infinite-range controls are enabled"), Section->GetSupportsInfiniteRange());
	TestTrue(TEXT("Activation retains its finite range"), F.Activation->GetRange() == TRange<FFrameNumber>(0, 100));
	TestTrue(TEXT("Parameter section resolves the single owning instance"), UMovieSceneComposableCameraInstanceTrack::FindForSection(*Section) == F.Track);
	TStrongObjectPtr<UComposableCameraInstanceData> Sample(NewObject<UComposableCameraInstanceData>());
	Sample->CopyConfiguration(*F.Track->Instance);
	TArray<FComposableCameraInstanceField> Fields; Sample->GatherFields(Fields);
	auto* Field = Fields.FindByPredicate([](const auto& Value) { return Value.Path == TEXT("Shot.Lens.ManualFOV"); });
	if (!Field) return false;
	Field->WriteNumber(40); Section->AddKey(*Field, 0);
	Field->WriteNumber(80); Section->AddKey(*Field, 10);
	for (auto& Key : Section->Channels[0].Number.GetData().GetValues()) Key.InterpMode = RCIM_Linear;
	F.Track->EvaluateParameters(FFrameTime(FFrameNumber(5), .5f), Fields);
	TestEqual(TEXT("Child parameter curve is sampled at subframes"), Sample->Shot.Lens.ManualFOV, 62.f);
	Parameter->SetEvalDisabled(true);
	Field->WriteNumber(50); F.Track->EvaluateParameters(FFrameTime(5), Fields);
	TestEqual(TEXT("Muted parameter restores the instance default"), Sample->Shot.Lens.ManualFOV, 50.f);
	Parameter->SetEvalDisabled(false);
	Section->SetRange(TRange<FFrameNumber>(0, 10));
	Field->WriteNumber(50); F.Track->EvaluateParameters(FFrameTime(20), Fields);
	TestEqual(TEXT("A parameter gap has no stale value"), Sample->Shot.Lens.ManualFOV, 50.f);
	// Prototype curves transfer without re-keying or losing Section ranges.
	Field->WriteNumber(25);
	F.Activation->AddKey(*Field, 12);
	Scene->RemoveTrack(*Parameter);
	auto* Migrated = AddParameterTrack(*F.Track, Field->Path, FFrameTime(0));
	if (!TestNotNull(TEXT("Prototype channel becomes an independent lane"), Migrated)) return false;
	TestTrue(TEXT("Prototype storage has one owner after transfer"), F.Activation->Channels.IsEmpty());
	auto* MigratedSection = CastChecked<UMovieSceneComposableCameraInstanceSection>(Migrated->GetAllSections()[0]);
	TestTrue(TEXT("Migration retains range"), MigratedSection->GetRange() == F.Activation->GetRange());
	double MigratedValue = 0; MigratedSection->Channels[0].Number.Evaluate(FFrameTime(12), MigratedValue);
	TestEqual(TEXT("Migration retains key value"), MigratedValue, 25.0);
	auto* Roll = AddParameterTrack(*F.Track, TEXT("Shot.Roll"), FFrameTime(0));
	if (!Roll) return false;
	auto* Pasted = DuplicateObject<UMovieSceneComposableCameraInstanceSection>(MigratedSection, Roll);
	Roll->AddSection(*Pasted);
	TestTrue(TEXT("Pasted native Section targets its destination parameter"), Pasted->Channels[0].Input == Roll->Input);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraInstanceRenameTest,
	"ComposableCameraSystem.CameraInstance.Rename", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCameraInstanceRenameTest::RunTest(const FString&)
{
	if (!GEditor || !GEngine || GEditor->IsTransactionActive() || !FSlateApplication::IsInitialized()) return false;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
	World->InitializeActorsForPlay(FURL()); World->BeginPlay();
	FInstanceAuthoringFixture F(World);
	auto* Scene = F.Sequence->GetMovieScene();
	F.Track->SetDisplayName(FText::FromString(TEXT("Original Shot")));
	FString Error;
	if (!ComposableCameraSystem::CameraInstanceOutput::Ensure(F.Sequencer.ToSharedRef(), *F.Track, Error)) return false;
	const FGuid Binding = F.Track->FindObjectBindingGuid();
	if (!TestNotNull(TEXT("Managed output is a native Spawnable"), Scene->FindSpawnable(Binding))) return false;
	F.Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
	F.Sequencer->ForceEvaluate();
	const auto Root = SNew(SShotEditorRoot).PersistViewState(false);
	Root->SetActiveShot(&F.Track->Instance->Shot, F.Track->Instance);
	const auto Header = Root->HostNameLabel;
	const auto Rename = [&](const TCHAR* Name)
	{
		const auto View = F.Sequencer->GetViewModel();
		auto* Storage = View ? View->GetRootModel()->CastDynamic<UE::Sequencer::FObjectBindingModelStorageExtension>() : nullptr;
		const auto Model = Storage ? Storage->FindModelForObjectBinding(Binding) : nullptr;
		if (!Model) return false;
		// Exercise the actual UE Rename command, not the plugin's creation setter.
		Model->Rename(FText::FromString(Name));
		Root->Tick(FGeometry(), 0., 0.f);
		return true;
	};
	if (!Rename(TEXT("Renamed Shot"))) return false;
	TestEqual(TEXT("Native Rename is immediately visible in LS"), Scene->GetObjectDisplayName(Binding).ToString(), FString(TEXT("Renamed Shot")));
	TestTrue(TEXT("Already-open Shot Editor updates its header"), Header->GetText().ToString().Contains(TEXT("Renamed Shot")));
	TestTrue(TEXT("Rename retains the existing header widget"), Root->HostNameLabel == Header);
	TFunction<bool(TSharedRef<SWidget>)> MenuHasRenamedLabel;
	MenuHasRenamedLabel = [&](TSharedRef<SWidget> Widget)
	{
		if (Widget->GetType() == FName(TEXT("STextBlock"))
			&& StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString().Contains(TEXT("Renamed Shot"))) return true;
		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Index < Children->Num(); ++Index)
			if (MenuHasRenamedLabel(Children->GetChildAt(Index))) return true;
		return false;
	};
	TestTrue(TEXT("Recent menu uses the live binding name"), MenuHasRenamedLabel(Root->BuildHistoryMenu()));
	const FText StableLabel = Header->GetText();
	F.Sequencer->SetLocalTimeDirectly(FFrameTime(20)); Root->Tick(FGeometry(), 0., 0.f);
	TestTrue(TEXT("Scrubbing does not reformat the unchanged header"), Header->GetText().IdenticalTo(StableLabel));
	TestTrue(TEXT("Rename undo"), GEditor->UndoTransaction()); Root->Tick(FGeometry(), 0., 0.f);
	TestEqual(TEXT("Undo restores LS name"), Scene->GetObjectDisplayName(Binding).ToString(), FString(TEXT("Original Shot")));
	TestTrue(TEXT("Undo updates the open editor"), Header->GetText().ToString().Contains(TEXT("Original Shot")));
	TestTrue(TEXT("Rename redo"), GEditor->RedoTransaction()); Root->Tick(FGeometry(), 0., 0.f);
	TestTrue(TEXT("Redo updates the open editor"), Header->GetText().ToString().Contains(TEXT("Renamed Shot")));
	// Simulate an older saved instance, including a rename hidden beneath its old override.
	Scene->SetObjectDisplayName(Binding, FText::FromString(TEXT("Obsolete Display Override")));
	const auto ReopenedEditor = MakeShared<FComposableCameraInstanceTrackEditor>(F.Sequencer.ToSharedRef());
	ReopenedEditor->Tick(0.f);
	Root->Tick(FGeometry(), 0., 0.f);
	TestEqual(TEXT("Opening an old instance reveals its native name"), Scene->GetObjectDisplayName(Binding).ToString(), FString(TEXT("Renamed Shot")));
	if (!Rename(TEXT("Renamed Again"))) return false;
	ReopenedEditor->Tick(0.f); Root->Tick(FGeometry(), 0., 0.f);
	TestEqual(TEXT("Maintenance does not restore the obsolete caption"), Scene->GetObjectDisplayName(Binding).ToString(), FString(TEXT("Renamed Again")));
	TestTrue(TEXT("Migrated instance updates the editor"), Header->GetText().ToString().Contains(TEXT("Renamed Again")));
	TestTrue(TEXT("Migration added no extra Undo edit"), GEditor->UndoTransaction()); Root->Tick(FGeometry(), 0., 0.f);
	TestEqual(TEXT("One Undo restores the preceding native name"), Scene->GetObjectDisplayName(Binding).ToString(), FString(TEXT("Renamed Shot")));
	TestTrue(TEXT("Source preset untouched"), F.Track->Instance->SourcePreset == F.Preset.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraInstanceClipActionsTest,
	"ComposableCameraSystem.CameraInstance.ClipActions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCameraInstanceClipActionsTest::RunTest(const FString&)
{
	if (!GEditor || !GEngine || GEditor->IsTransactionActive() || !FSlateApplication::IsInitialized()) return false;
	using namespace ComposableCameraSystem::CameraInstanceAuthoring;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
	World->InitializeActorsForPlay(FURL()); World->BeginPlay();
	FInstanceAuthoringFixture F(World);
	auto* Scene = F.Sequence->GetMovieScene();
	FString Error;
	if (!TestTrue(TEXT("Fixture has a managed output"), ComposableCameraSystem::CameraInstanceOutput::Ensure(F.Sequencer.ToSharedRef(), *F.Track, Error))) return false;
	const FGuid SourceBinding = F.Track->FindObjectBindingGuid();
	const FGuid Subject = Scene->AddPossessable(TEXT("Clip Subject"), AActor::StaticClass());
	F.Track->Instance->Subjects[0].Binding = UE::MovieScene::FRelativeObjectBindingID(Subject);
	auto* Parameter = AddParameterTrack(*F.Track, TEXT("Shot.Lens.ManualFOV"), FFrameTime(0));
	if (!Parameter) return false;
	auto* Keys = CastChecked<UMovieSceneComposableCameraInstanceSection>(Parameter->GetAllSections()[0]);
	Keys->Channels[0].Number.GetData().AddKey(0, FMovieSceneDoubleValue(50.));
	Keys->Channels[0].Number.GetData().AddKey(20, FMovieSceneDoubleValue(70.));
	for (auto& Key : Keys->Channels[0].Number.GetData().GetValues()) Key.InterpMode = RCIM_Linear;
	// Multiple activation intervals belong to the same instance and move together.
	auto* Later = F.Track->CreateNewSection(); Later->SetRange(TRange<FFrameNumber>(200, 260)); F.Track->AddSection(*Later);
	F.Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
	const auto Session = MakeShared<FComposableCameraShotAuthoringSession>();
	Session->Bind(&F.Track->Instance->Shot, F.Track->Instance);
	const auto Panel = SNew(SShotEditorAuthoringPanel).Session(Session).PersistViewState(false);
	Panel->SelectSequenceForTesting();
	const auto FindAction = [&](FName Tag)
	{
		TFunction<TSharedPtr<SWidget>(TSharedRef<SWidget>)> Find;
		Find = [&](TSharedRef<SWidget> Widget) -> TSharedPtr<SWidget>
		{
			if (Widget->GetTag() == Tag) return Widget;
			FChildren* Children = Widget->GetChildren();
			for (int32 Index = 0; Index < Children->Num(); ++Index)
				if (auto Result = Find(Children->GetChildAt(Index))) return Result;
			return nullptr;
		};
		return Find(Panel);
	};
	const auto DragRow = FindAction(TEXT("ShotEditor.DragRow"));
	const auto Go = FindAction(TEXT("ShotEditor.GoToClip"));
	const auto Duplicate = FindAction(TEXT("ShotEditor.DuplicateAtPlayhead"));
	if (!DragRow || !Go || !Duplicate) return false;
	F.Sequencer->SetLocalTimeDirectly(FFrameTime(500)); Session->Tick(); Panel->SlatePrepass();
	TestFalse(TEXT("Bound drag icon and description disabled together"), DragRow->IsEnabled());
	TestFalse(TEXT("Source parameters are inactive outside its intervals"), Session->CanEdit());
	TestTrue(TEXT("Go and Duplicate still work outside source activation"), Go->IsEnabled() && Duplicate->IsEnabled());
	Panel->GoToClip();
	TArray<UMovieSceneSection*> Selected; F.Sequencer->GetSelectedSections(Selected);
	TestTrue(TEXT("Go selects the bound clip"), Selected.Contains(F.Activation));
	TestEqual(TEXT("Go does not move the playhead"), F.Sequencer->GetLocalTime().Time.FloorToFrame().Value, 500);
	const int32 OriginalBindings = Scene->GetBindings().Num();
	Panel->DuplicateAtPlayhead();
	auto* Copy = Session->GetInstanceTrack();
	if (!TestTrue(TEXT("Duplicate transfers the editor to an independent instance"), Copy && Copy != F.Track)) return false;
	const FGuid CopyBinding = Copy->FindObjectBindingGuid();
	TestEqual(TEXT("Duplicate creates one output binding"), Scene->GetBindings().Num(), OriginalBindings + 1);
	TestTrue(TEXT("Output identity is independent"), CopyBinding.IsValid() && CopyBinding != SourceBinding);
	TestTrue(TEXT("Configuration and built-in type are owned by the copy"), Copy->Instance != F.Track->Instance
		&& Copy->Instance->Camera.TypeAsset != F.Track->Instance->Camera.TypeAsset);
	TestTrue(TEXT("Subject binding and stable ID survive"), Copy->Instance->Subjects[0].Binding.GetGuid() == Subject
		&& Copy->Instance->Shot.Targets[0].SubjectId == F.Track->Instance->Shot.Targets[0].SubjectId);
	TestTrue(TEXT("Anchor starts at playhead"), Copy->GetAllSections()[0]->GetRange() == TRange<FFrameNumber>(500, 600));
	TestTrue(TEXT("Additional activation keeps relative timing"), Copy->GetAllSections()[1]->GetRange() == TRange<FFrameNumber>(700, 760));
	TestTrue(TEXT("Source activation unchanged"), F.Activation->GetRange() == TRange<FFrameNumber>(0, 100));
	auto* CopyParameter = FindParameterTrack(*Copy, Parameter->Input);
	if (!TestNotNull(TEXT("Parameter lane copied under the new instance"), CopyParameter)) return false;
	auto* CopyKeys = CastChecked<UMovieSceneComposableCameraInstanceSection>(CopyParameter->GetAllSections()[0]);
	TestTrue(TEXT("Infinite parameter range retained"), CopyKeys->GetRange() == TRange<FFrameNumber>::All());
	const auto KeyTimes = CopyKeys->Channels[0].Number.GetData().GetTimes();
	TestTrue(TEXT("Keys move with the instance"), KeyTimes.Num() == 2 && KeyTimes[0] == FFrameNumber(500) && KeyTimes[1] == FFrameNumber(520));
	double Sample = 0.; CopyKeys->Channels[0].Number.Evaluate(FFrameTime(510), Sample);
	TestEqual(TEXT("Interpolation retained"), Sample, 60.);
	if (!TestNotNull(TEXT("Copy opens its sampled editing view"), Session->GetShot())) return false;
	TestEqual(TEXT("Shot Editor opens at current keyed FOV"), Session->GetShot()->Lens.ManualFOV, 50.f);
	TestTrue(TEXT("New Cut targets the copy"), Copy->ManagedCut && Copy->ManagedCut->GetCameraBindingID() == Copy->OutputBinding);
	TestTrue(TEXT("Source keys unchanged"), Keys->Channels[0].Number.GetData().GetTimes()[0] == FFrameNumber(0));
	TestTrue(TEXT("Duplicate undo"), GEditor->UndoTransaction());
	TestNull(TEXT("Undo removes only copied binding"), Scene->FindBinding(CopyBinding));
	TestNotNull(TEXT("Undo preserves original output"), Scene->FindBinding(SourceBinding));
	TestTrue(TEXT("Duplicate redo"), GEditor->RedoTransaction());
	TestNotNull(TEXT("Redo restores copied binding"), Scene->FindBinding(CopyBinding));
	TestTrue(TEXT("Redo preserves copied animation timing"), CopyKeys->Channels[0].Number.GetData().GetTimes()[0] == FFrameNumber(500));
	Session->Bind(&F.Track->Instance->Shot, F.Track->Instance);
	Scene->SetReadOnly(true); Panel->SlatePrepass();
	TestTrue(TEXT("Read-only clips remain navigable"), Go->IsEnabled());
	TestFalse(TEXT("Read-only clips cannot duplicate"), Duplicate->IsEnabled());
	TestNull(TEXT("Service also rejects read-only destinations"), DuplicateAtTime(F.Sequencer.ToSharedRef(), *F.Track, *F.Activation, 900, Error));
	Scene->SetReadOnly(false);
	Keys->Channels[0].Number.GetData().AddKey(MAX_int32 - 1, FMovieSceneDoubleValue(80.));
	const int32 BeforeFailure = Scene->GetBindings().Num();
	TestNull(TEXT("Key overflow rejects the entire copy"), DuplicateAtTime(F.Sequencer.ToSharedRef(), *F.Track, *F.Activation, 900, Error));
	TestEqual(TEXT("Rejected copy creates no binding"), Scene->GetBindings().Num(), BeforeFailure);
	if (!Session->StartSequenceDraft(F.Sequencer.ToSharedRef(), Error)) return false;
	Panel->SlatePrepass();
	TestTrue(TEXT("Draft enables drag and explanation"), DragRow->IsEnabled());
	TestFalse(TEXT("Draft cannot navigate to a clip"), Go->IsEnabled());
	TestFalse(TEXT("Draft cannot duplicate a clip"), Duplicate->IsEnabled());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraInstanceSampledEditingTest,
	"ComposableCameraSystem.CameraInstance.SampledEditing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCameraInstanceSampledEditingTest::RunTest(const FString&)
{
	if (!GEditor || GEditor->IsTransactionActive() || !FSlateApplication::IsInitialized()) return false;
	using namespace ComposableCameraSystem::CameraInstanceAuthoring;
	FInstanceAuthoringFixture F;
	FComposableCameraInstanceEditorSession Editor;
	Editor.Bind(F.Track, F.Sequencer);
	TestTrue(TEXT("Instance can be edited at the playhead"), Editor.CanEdit());
	{
		FScopedTransaction Transaction(NSLOCTEXT("CameraInstanceTest", "Base", "Edit Instance Default"));
		Editor.GetValues()->Shot.Lens.ManualFOV = 55.f; Editor.Commit();
	}
	TestEqual(TEXT("Unanimated edit updates local default"), F.Track->Instance->Shot.Lens.ManualFOV, 55.f);
	TestEqual(TEXT("Preset remains unchanged"), F.Preset->Shot.Lens.ManualFOV, 50.f);
	F.Sequencer->SetAutoChangeMode(EAutoChangeMode::AutoKey);
	Editor.Refresh();
	{
		FScopedTransaction Transaction(NSLOCTEXT("CameraInstanceTest", "Key", "Auto Key Instance"));
		Editor.GetValues()->Shot.Lens.ManualFOV = 60.f; Editor.Commit();
	}
	auto* Parameter = FindParameterTrack(*F.Track, TEXT("Shot.Lens.ManualFOV"));
	if (!TestNotNull(TEXT("Auto Key creates a native parameter lane"), Parameter)) return false;
	TestNull(TEXT("Auto Key does not create an untouched parameter"), FindParameterTrack(*F.Track, TEXT("Shot.Roll")));
	F.Sequencer->SetLocalTimeDirectly(FFrameTime(10)); Editor.Refresh();
	{
		FScopedTransaction Transaction(NSLOCTEXT("CameraInstanceTest", "Key2", "Auto Key Second Frame"));
		Editor.GetValues()->Shot.Lens.ManualFOV = 80.f; Editor.Commit();
	}
	TestEqual(TEXT("Animated edits preserve base default"), F.Track->Instance->Shot.Lens.ManualFOV, 55.f);
	F.Sequencer->SetLocalTimeDirectly(FFrameTime(0)); Editor.Refresh();
	TestEqual(TEXT("Scrub restores the first sampled value"), Editor.GetValues()->Shot.Lens.ManualFOV, 60.f);
	F.Sequencer->SetAutoChangeMode(EAutoChangeMode::None);
	{
		FScopedTransaction Transaction(NSLOCTEXT("CameraInstanceTest", "Pending", "Preview Manual Key Value"));
		Editor.GetValues()->Shot.Lens.ManualFOV = 65.f; Editor.Commit();
	}
	auto* KeySection = CastChecked<UMovieSceneComposableCameraInstanceSection>(Parameter->GetAllSections()[0]);
	double KeyedValue = 0; KeySection->Channels[0].Number.Evaluate(FFrameTime(0), KeyedValue);
	TestEqual(TEXT("Auto Key off does not overwrite an existing key"), KeyedValue, 60.0);
	const auto PendingValue = FComposableCameraInstanceEditorSession::ReadEditorValue(KeySection, TEXT("Shot.Lens.ManualFOV"));
	TestTrue(TEXT("Native Key button can read the pending edited value"), PendingValue.IsSet() && PendingValue.GetValue() == 65.0);
	F.Sequencer->SetLocalTimeDirectly(FFrameTime(10)); Editor.Refresh();
	F.Sequencer->SetLocalTimeDirectly(FFrameTime(0)); Editor.Refresh();
	TestEqual(TEXT("Unkeyed edits are discarded when seeking"), Editor.GetValues()->Shot.Lens.ManualFOV, 60.f);
	F.Sequencer->SetLocalTimeDirectly(FFrameTime(10));
	FComposableCameraShotAuthoringSession ShotEditor;
	ShotEditor.Bind(&F.Track->Instance->Shot, F.Track->Instance);
	if (!TestNotNull(TEXT("Shot Editor accepts an instance host"), ShotEditor.GetShot())) return false;
	TestEqual(TEXT("Reopened quick pages display current keys"), ShotEditor.GetShot()->Lens.ManualFOV, 80.f);
	TestTrue(TEXT("Quick pages edit a sampled view, not the source asset"), ShotEditor.GetShot() != &F.Preset->Shot && ShotEditor.GetShot() != &F.Track->Instance->Shot);
	const FGuid Subject = F.Sequence->GetMovieScene()->AddPossessable(TEXT("Subject currently missing"), AActor::StaticClass());
	FString Reason;
	TestTrue(TEXT("Instance Subject picker accepts inactive binding metadata"), ShotEditor.SetTargetBinding(0, Subject, Reason));
	TestTrue(TEXT("Subject binding survives reopening"), ShotEditor.GetTargetBinding(0).GetGuid() == Subject);
	TestNull(TEXT("No actor template is substituted for a missing subject"), ShotEditor.ResolveTarget(0));
	ShotEditor.Bind(&F.Track->Instance->Shot, F.Track->Instance);
	TestTrue(TEXT("Saved binding is restored by the real session adapter"), ShotEditor.GetTargetBinding(0).GetGuid() == Subject);
	TestEqual(TEXT("Bindings do not create another camera instance"), F.Sequence->GetMovieScene()->FindBinding(F.Binding)->GetTracks().Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraInstanceSequencerReconnectTest,
	"ComposableCameraSystem.CameraInstance.SequencerReconnect", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCameraInstanceSequencerReconnectTest::RunTest(const FString&)
{
	if (!GEditor || GEditor->IsTransactionActive() || !FSlateApplication::IsInitialized()) return false;
	using namespace ComposableCameraSystem::CameraInstanceAuthoring;
	FInstanceAuthoringFixture F;
	auto* Scene = F.Sequence->GetMovieScene();
	const FGuid Subject = Scene->AddPossessable(TEXT("Subject currently missing"), AActor::StaticClass());
	F.Track->Instance->Subjects[0].Binding = UE::MovieScene::FRelativeObjectBindingID(Subject);
	auto* Parameter = AddParameterTrack(*F.Track, TEXT("Shot.Lens.ManualFOV"), FFrameTime(0));
	if (!Parameter) return false;
	auto* Section = CastChecked<UMovieSceneComposableCameraInstanceSection>(Parameter->GetAllSections()[0]);
	TStrongObjectPtr<UComposableCameraInstanceData> KeyValues(NewObject<UComposableCameraInstanceData>());
	KeyValues->CopyConfiguration(*F.Track->Instance);
	TArray<FComposableCameraInstanceField> Fields; KeyValues->GatherFields(Fields);
	const auto* FOV = Fields.FindByPredicate([](const auto& Field) { return Field.Path == TEXT("Shot.Lens.ManualFOV"); });
	if (!FOV) return false;
	FOV->WriteNumber(40); Section->AddKey(*FOV, 0);
	FOV->WriteNumber(80); Section->AddKey(*FOV, 10);
	for (auto& Key : Section->Channels[0].Number.GetData().GetValues()) Key.InterpMode = RCIM_Linear;
	F.Sequencer->SetLocalTimeDirectly(FFrameTime(5));
	FComposableCameraShotAuthoringSession Editor;
	Editor.Bind(&F.Track->Instance->Shot, F.Track->Instance);
	auto* RetainedValues = Editor.GetInstanceValues();
	TestTrue(TEXT("Original connection permits editing inside activation"), Editor.CanEdit());
	TestEqual(TEXT("Original connection samples the curve"), Editor.GetShot()->Lens.ManualFOV, 60.f);
	Editor.BeginEdit(NSLOCTEXT("CameraInstanceTest", "BeforeReconnect", "Preview Before Reopen"));
	Editor.GetShot()->Lens.ManualFOV = 95.f;
	Editor.Changed(); Editor.EndEdit();
	TestTrue(TEXT("Unkeyed preview exists before close"), FComposableCameraInstanceEditorSession::ReadEditorValue(Section, Parameter->Input).IsSet());
	int32 SourceRefreshes = 0;
	Editor.OnChanged = [&](bool bStructural) { if (bStructural) ++SourceRefreshes; };
	ON_SCOPE_EXIT { Editor.OnChanged = nullptr; };

	// Keep Shot Editor and its instance host alive while replacing the LS editor.
	const TWeakPtr<ISequencer> OldSequencer = F.Sequencer;
	F.Sequencer->Close(); F.Sequencer.Reset();
	Editor.Tick();
	TestFalse(TEXT("Old Sequencer is released"), OldSequencer.IsValid());
	TestFalse(TEXT("Closed LS cannot edit"), Editor.CanEdit());
	FComposableCameraShotAuthoringSession OpenedWithoutSequencer;
	OpenedWithoutSequencer.Bind(&F.Track->Instance->Shot, F.Track->Instance);
	TestFalse(TEXT("Opening an instance without LS cannot edit"), OpenedWithoutSequencer.CanEdit());

	FSequencerInitParams Params;
	Params.RootSequence = F.Sequence.Get();
	Params.ViewParams.UniqueName = TEXT("CameraInstanceAuthoringRegression");
	F.Sequencer = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer").CreateSequencer(Params);
	F.Sequencer->SetAutoChangeMode(EAutoChangeMode::None);
	F.Sequencer->SetLocalTimeDirectly(FFrameTime(5));
	Editor.Tick(); OpenedWithoutSequencer.Tick();
	TestTrue(TEXT("Existing Shot Editor reconnects without rebinding its host"), Editor.CanEdit() && Editor.GetSequencer() == F.Sequencer);
	TestTrue(TEXT("Reconnection retains the sampled UObject"), Editor.GetInstanceValues() == RetainedValues);
	TestEqual(TEXT("Same-time reopen discards pending preview and resamples keys"), Editor.GetShot()->Lens.ManualFOV, 60.f);
	TestTrue(TEXT("An initially missing connection also recovers"), OpenedWithoutSequencer.CanEdit());
	TestEqual(TEXT("Late connection initializes its sampled fields"), OpenedWithoutSequencer.GetShot()->Lens.ManualFOV, 60.f);
	TestTrue(TEXT("Subjects retain sequence binding identity"), Editor.GetTargetBinding(0).GetGuid() == Subject);
	TestNull(TEXT("Missing Subject remains unresolved without losing its binding"), Editor.ResolveTarget(0));
	TestFalse(TEXT("Old pending values cannot feed native key insertion"), FComposableCameraInstanceEditorSession::ReadEditorValue(Section, Parameter->Input).IsSet());
	TestEqual(TEXT("Replacement triggers one source refresh"), SourceRefreshes, 1);
	for (int32 Frame = 0; Frame <= 10; ++Frame)
	{
		F.Sequencer->SetLocalTimeDirectly(FFrameTime(Frame)); Editor.Tick();
		TestTrue(TEXT("Reconnected editor stays editable while seeking"), Editor.CanEdit());
		TestEqual(TEXT("Reconnected view follows the current curve"), Editor.GetShot()->Lens.ManualFOV, 40.f + 4.f * Frame);
	}
	TestEqual(TEXT("Ordinary seeking never repeats the structural refresh"), SourceRefreshes, 1);
	F.Sequencer->SetLocalTimeDirectly(FFrameTime(100)); Editor.Tick();
	TestFalse(TEXT("Activation end remains exclusive"), Editor.CanEdit());
	F.Sequencer->SetLocalTimeDirectly(FFrameTime(20)); Editor.Tick();
	F.Activation->SetIsLocked(true);
	TestFalse(TEXT("Activation lock remains respected"), Editor.CanEdit());
	F.Activation->SetIsLocked(false);
	Scene->SetReadOnly(true);
	TestFalse(TEXT("Sequence read-only remains respected"), Editor.CanEdit());
	Scene->SetReadOnly(false); Editor.Tick();
	F.Sequencer->SetAutoChangeMode(EAutoChangeMode::AutoKey);
	Editor.BeginEdit(NSLOCTEXT("CameraInstanceTest", "AfterReconnect", "Auto Key After Reopen"));
	Editor.GetShot()->Lens.ManualFOV = 75.f;
	Editor.Changed(); Editor.EndEdit();
	double KeyedValue = 0.; Section->Channels[0].Number.Evaluate(FFrameTime(20), KeyedValue);
	TestEqual(TEXT("Auto Key uses the reopened Sequencer"), KeyedValue, 75.0);
	TestEqual(TEXT("Reconnected edits preserve authored defaults"), F.Track->Instance->Shot.Lens.ManualFOV, 50.f);
	TestEqual(TEXT("Reconnection creates no duplicate parameter lane"), Scene->FindBinding(F.Binding)->GetTracks().Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraInstanceGenericEditorTest,
	"ComposableCameraSystem.CameraInstance.GenericEditor", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCameraInstanceGenericEditorTest::RunTest(const FString&)
{
	if (!GEditor || GEditor->IsTransactionActive() || !FSlateApplication::IsInitialized()) return false;
	FInstanceAuthoringFixture F;
	TStrongObjectPtr<UComposableCameraTypeAsset> Type(NewObject<UComposableCameraTypeAsset>());
	auto* Lens = NewObject<UComposableCameraLensNode>(Type.Get()); Lens->FocalLength = 65.f;
	Type->NodeTemplates.Add(Lens); Type->NodePinOverrides.AddDefaulted();
	auto& Input = Type->ExposedParameters.AddDefaulted_GetRef();
	Input.ParameterName = TEXT("LensLength"); Input.PinType = EComposableCameraPinType::Float;
	Input.TargetNodeIndex = 0; Input.TargetPinName = TEXT("FocalLength");
	F.Track->Instance->InitializeFromType(Type.Get()); F.Track->MarkAsChanged();
	FComposableCameraShotAuthoringSession Editor;
	Editor.Bind(nullptr, F.Track->Instance);
	TestTrue(TEXT("Ordinary CameraType opens in generic mode"), Editor.IsGenericInstance());
	TestNull(TEXT("Generic type has no fabricated composition Shot"), Editor.GetShot());
	TestTrue(TEXT("Generic editing uses the same Sequence instance"), Editor.GetInstanceTrack() == F.Track && Editor.CanEdit());
	TArray<FComposableCameraInstanceField> Fields; Editor.GetInstanceValues()->GatherFields(Fields);
	const auto* Field = Fields.FindByPredicate([](const auto& Value) { return Value.Path == TEXT("Parameters.LensLength"); });
	if (!TestNotNull(TEXT("Generic Details exposes typed input"), Field)) return false;
	F.Sequencer->SetAutoChangeMode(EAutoChangeMode::AutoKey);
	Editor.BeginEdit(NSLOCTEXT("CameraInstanceTest", "GenericKey", "Key Generic Input"));
	Field->WriteNumber(90); Editor.Changed(); Editor.EndEdit();
	TestNotNull(TEXT("Generic input receives a native parameter track"), ComposableCameraSystem::CameraInstanceAuthoring::FindParameterTrack(*F.Track, Field->Path));
	TestEqual(TEXT("Source CameraType defaults are preserved"), Lens->FocalLength, 65.f);
	// Generic property bags must also retain their backing memory while seeking.
	Editor.Tick();
	const uint8* BagMemory = Editor.GetInstanceValues()->Camera.Parameters.GetValue().GetMemory();
	int32 StructuralChanges = 0;
	Editor.OnChanged = [&](bool bStructural) { if (bStructural) ++StructuralChanges; };
	for (int32 Frame = 1; Frame <= 10; ++Frame)
	{
		F.Sequencer->SetLocalTimeDirectly(FFrameTime(Frame)); Editor.Tick();
		TestTrue(TEXT("Seeking retains generic Details storage"), Editor.GetInstanceValues()->Camera.Parameters.GetValue().GetMemory() == BagMemory);
		TArray<FComposableCameraInstanceField> SampledFields; Editor.GetInstanceValues()->GatherFields(SampledFields);
		const auto* Sampled = SampledFields.FindByPredicate([](const auto& Value) { return Value.Path == TEXT("Parameters.LensLength"); });
		if (TestNotNull(TEXT("Generic input still exists after seeking"), Sampled)) TestEqual(TEXT("Generic keys remain sampled"), Sampled->ReadNumber(), 90.0);
	}
	TestEqual(TEXT("Generic seeking never invalidates Details"), StructuralChanges, 0);
	Editor.OnChanged = nullptr;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraInstanceScrubTest,
	"ComposableCameraSystem.CameraInstance.ScrubRetainsControls", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCameraInstanceScrubTest::RunTest(const FString&)
{
	if (!GEditor || GEditor->IsTransactionActive() || !FSlateApplication::IsInitialized()) return false;
	using namespace ComposableCameraSystem::CameraInstanceAuthoring;
	FInstanceAuthoringFixture F;
	F.Track->Instance->Shot.Lens.FOVMode = EShotFOVMode::Manual;
	const FGuid SubjectBinding = F.Sequence->GetMovieScene()->AddPossessable(TEXT("Inactive Subject"), AActor::StaticClass());
	F.Track->Instance->Subjects[0].Binding = UE::MovieScene::FRelativeObjectBindingID(SubjectBinding);
	auto* Parameter = AddParameterTrack(*F.Track, TEXT("Shot.Lens.ManualFOV"), FFrameTime(0));
	if (!Parameter) return false;
	auto* Section = CastChecked<UMovieSceneComposableCameraInstanceSection>(Parameter->GetAllSections()[0]);
	Section->SetRange(TRange<FFrameNumber>(0, 11));
	TStrongObjectPtr<UComposableCameraInstanceData> KeyValues(NewObject<UComposableCameraInstanceData>());
	KeyValues->CopyConfiguration(*F.Track->Instance);
	TArray<FComposableCameraInstanceField> KeyFields; KeyValues->GatherFields(KeyFields);
	const auto* FOV = KeyFields.FindByPredicate([](const auto& Value) { return Value.Path == TEXT("Shot.Lens.ManualFOV"); });
	if (!FOV) return false;
	FOV->WriteNumber(40); Section->AddKey(*FOV, 0);
	FOV->WriteNumber(80); Section->AddKey(*FOV, 10);
	for (auto& Key : Section->Channels[0].Number.GetData().GetValues()) Key.InterpMode = RCIM_Linear;

	// Exercise the real Root -> Session -> Details/parameter-page refresh routing.
	const auto Root = SNew(SShotEditorRoot).PersistViewState(false);
	Root->SetActiveShot(&F.Track->Instance->Shot, F.Track->Instance);
	TArray<TSharedPtr<SShotEditorParameterPanel>> Panels;
	Root->AuthoringPanel->GetEditParameterPanelsForTesting(Panels);
	if (Panels.Num() != 4) return false;
	const auto Lens = Panels[2];
	const auto Subject = Root->AuthoringPanel->GetSubjectPanelForTesting(0);
	if (!Lens || !Subject) return false;
	Panels.Add(Subject);
	const auto Pump = [&]()
	{
		for (int32 Pass = 0; Pass < 3; ++Pass)
		{
			Root->Tick(FGeometry(), 0., 0.f);
			FTickableEditorObject::TickObjects(0.f);
			Root->AuthoringPanel->Tick(FGeometry(), 0., 0.f);
			for (const auto& Panel : Panels) Panel->Tick(FGeometry(), 0., 0.f);
		}
	};
	Pump();
	TArray<uint32> RebuildCounts;
	for (const auto& Panel : Panels) RebuildCounts.Add(Panel->GetRebuildCountForTesting());
	const auto* Handle = Lens->RenderedPropertyHandles.Find(TEXT("Lens.ManualFOV"));
	if (!Handle || !Handle->IsValid()) return false;
	const TSharedPtr<IPropertyHandle> NativeFOV = *Handle;
	const auto FOVRow = Lens->RevealProperty(TEXT("Lens.ManualFOV"));
	if (!TestTrue(TEXT("Native FOV row exists"), FOVRow.IsValid())) return false;
	const auto* Targets = Root->AuthoringSession->GetShot()->Targets.GetData();
	for (int32 Step = 0; Step <= 20; ++Step)
	{
		const int32 Frame = Step <= 10 ? Step : 20 - Step;
		F.Sequencer->SetLocalTimeDirectly(FFrameTime(Frame)); Pump();
		float DisplayedFOV = 0.f;
		TestTrue(TEXT("Retained native FOV handle is readable"), NativeFOV->GetValue(DisplayedFOV) == FPropertyAccess::Success);
		TestEqual(TEXT("Native control reads the current sample"), DisplayedFOV, 40.f + 4.f * Frame);
		TestTrue(TEXT("Seeking retains Subject storage"), Root->AuthoringSession->GetShot()->Targets.GetData() == Targets);
		TestTrue(TEXT("Seeking retains the same native row"), Lens->RevealProperty(TEXT("Lens.ManualFOV")) == FOVRow);
		for (int32 Index = 0; Index < Panels.Num(); ++Index)
			TestEqual(TEXT("Seeking never rebuilds parameter controls"), Panels[Index]->GetRebuildCountForTesting(), RebuildCounts[Index]);
		TestTrue(TEXT("Inactive Subject binding survives sampling"), Root->AuthoringSession->GetTargetBinding(0).GetGuid() == SubjectBinding);
	}
	F.Sequencer->SetLocalTimeDirectly(FFrameTime(20)); Pump();
	TestEqual(TEXT("A parameter gap restores the authored default"), Root->AuthoringSession->GetShot()->Lens.ManualFOV, 50.f);
	F.Sequencer->SetLocalTimeDirectly(FFrameTime(5)); Pump();
	Root->AuthoringSession->BeginEdit(NSLOCTEXT("CameraInstanceTest", "ScrubPending", "Preview FOV"));
	Root->AuthoringSession->GetShot()->Lens.ManualFOV = 95.f;
	Root->AuthoringSession->Changed(); Root->AuthoringSession->EndEdit();
	const auto Pending = FComposableCameraInstanceEditorSession::ReadEditorValue(Section, Parameter->Input);
	TestTrue(TEXT("Manual keying can still capture a pending sample"), Pending.IsSet() && Pending.GetValue() == 95.0);
	F.Sequencer->SetLocalTimeDirectly(FFrameTime(6)); Pump();
	TestEqual(TEXT("Seeking discards pending values without altering keys"), Root->AuthoringSession->GetShot()->Lens.ManualFOV, 64.f);
	for (int32 Index = 0; Index < Panels.Num(); ++Index)
		TestEqual(TEXT("Pending preview/seek retains native controls"), Panels[Index]->GetRebuildCountForTesting(), RebuildCounts[Index]);

	// Source/collection changes must still invalidate handles instead of keeping stale addresses.
	{
		FScopedTransaction Transaction(NSLOCTEXT("CameraInstanceTest", "ScrubSubjects", "Append Subject"));
		F.Track->Modify(); F.Track->Instance->Modify();
		F.Track->Instance->Shot.Targets.AddDefaulted();
		F.Track->Instance->EnsureSubjectIdentities(); F.Track->Instance->RefreshSubjectSlots();
		F.Track->MarkAsChanged();
	}
	Pump();
	TestEqual(TEXT("Structural refresh samples the new Subject array"), Root->AuthoringSession->GetShot()->Targets.Num(), 3);
	TestTrue(TEXT("Structural refresh regenerates native handles"), Lens->GetRebuildCountForTesting() > RebuildCounts[2]);
	TestTrue(TEXT("Subject append can undo"), GEditor->UndoTransaction()); Pump();
	TestEqual(TEXT("Undo restores the Subject layout"), Root->AuthoringSession->GetShot()->Targets.Num(), 2);
	TestTrue(TEXT("Subject append can redo"), GEditor->RedoTransaction()); Pump();
	TestEqual(TEXT("Redo restores the appended Subject"), Root->AuthoringSession->GetShot()->Targets.Num(), 3);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraInstanceManualKeyTest,
	"ComposableCameraSystem.CameraInstance.NativeManualKey", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCameraInstanceManualKeyTest::RunTest(const FString&)
{
	if (!GEditor || !GEngine || GEditor->IsTransactionActive() || !FSlateApplication::IsInitialized()) return false;
	using namespace ComposableCameraSystem::CameraInstanceAuthoring;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
	World->InitializeActorsForPlay(FURL()); World->BeginPlay();
	FInstanceAuthoringFixture F(World);
	auto* Actor = World->SpawnActor<AComposableCameraLevelSequenceActor>();
	if (!TestNotNull(TEXT("Native keying has a live bound output"), Actor)) return false;
	auto* Output = Actor->FindComponentByClass<UComposableCameraLevelSequenceComponent>();
	if (!Output) return false;
	Output->CameraInstanceOwner = F.Track;
	Output->bCameraInstanceDriven = true;
	F.Sequence->BindPossessableObject(F.Binding, *Actor, World);
	F.Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
	F.Sequencer->ForceEvaluate();
	bool bResolved = false;
	for (const auto Bound : F.Sequencer->FindBoundObjects(F.Binding, F.Sequencer->GetFocusedTemplateID())) bResolved |= Bound.Get() == Actor;
	if (!TestTrue(TEXT("External value path must resolve the output, not use fallback"), bResolved)) return false;
	auto* Parameter = AddParameterTrack(*F.Track, TEXT("Shot.Lens.ManualFOV"), FFrameTime(0));
	if (!Parameter) return false;
	auto* Section = CastChecked<UMovieSceneComposableCameraInstanceSection>(Parameter->GetAllSections()[0]);
	FComposableCameraInstanceEditorSession Editor;
	Editor.Bind(F.Track, F.Sequencer);
	const auto PreviewAndKey = [&](int32 Frame, float Value)
	{
		F.Sequencer->SetLocalTimeDirectly(FFrameTime(Frame)); Editor.Refresh();
		{
			FScopedTransaction Transaction(NSLOCTEXT("CameraInstanceTest", "NativePending", "Edit pending FOV"));
			Editor.GetValues()->Shot.Lens.ManualFOV = Value; Editor.Commit();
		}
		FKeyHandle Key;
		{
			FScopedTransaction Transaction(NSLOCTEXT("CameraInstanceTest", "NativeKey", "Add native FOV key"));
			// Match the native button: Modify first, then request external data and
			// call MovieSceneTools' real numeric keying implementation (including recomposition).
			Section->Modify();
			const auto Handle = Section->GetChannelProxy().MakeHandle<FMovieSceneDoubleChannel>(0);
			const auto* External = Handle.GetExtendedEditorData();
			if (!External) return false;
			Key = AddOrUpdateKey(Handle.Get(), Section, *External, FFrameNumber(Frame), *F.Sequencer, F.Binding, nullptr);
			Section->MarkAsChanged();
			F.Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
		}
		double Stored = 0; Section->Channels[0].Number.Evaluate(FFrameTime(Frame), Stored);
		TestTrue(TEXT("Native key handle is valid"), Key.IsValid());
		TestEqual(TEXT("Native key stores the pending FOV exactly"), Stored, static_cast<double>(Value));
		Editor.Refresh();
		TestEqual(TEXT("Resampling retains the manually keyed value"), Editor.GetValues()->Shot.Lens.ManualFOV, Value);
		return true;
	};
	if (!PreviewAndKey(0, 65.f)) return false;
	TestEqual(TEXT("First manual key is inserted"), Section->Channels[0].Number.GetNumKeys(), 1);
	TestTrue(TEXT("Native key insertion can undo"), GEditor->UndoTransaction()); Editor.Refresh();
	TestEqual(TEXT("Undo removes the key"), Section->Channels[0].Number.GetNumKeys(), 0);
	TestTrue(TEXT("Native key insertion can redo"), GEditor->RedoTransaction()); Editor.Refresh();
	TestEqual(TEXT("Redo restores the key"), Section->Channels[0].Number.GetNumKeys(), 1);
	if (!PreviewAndKey(10, 85.f) || !PreviewAndKey(5, 72.f) || !PreviewAndKey(5, 74.f)) return false;
	TestEqual(TEXT("Repeated native key at the same time updates, not duplicates"), Section->Channels[0].Number.GetNumKeys(), 3);
	TestEqual(TEXT("Manual keys preserve the instance default"), F.Track->Instance->Shot.Lens.ManualFOV, 50.f);
	F.Sequencer->SetAutoChangeMode(EAutoChangeMode::AutoKey);
	F.Sequencer->SetLocalTimeDirectly(FFrameTime(20)); Editor.Refresh();
	{
		FScopedTransaction Transaction(NSLOCTEXT("CameraInstanceTest", "AutoAfterManual", "Auto Key after manual keys"));
		Editor.GetValues()->Shot.Lens.ManualFOV = 95.f; Editor.Commit();
	}
	double AutoValue = 0; Section->Channels[0].Number.Evaluate(FFrameTime(20), AutoValue);
	TestEqual(TEXT("Auto Key still works on the same channel"), AutoValue, 95.0);
	TestEqual(TEXT("Source preset remains unchanged"), F.Preset->Shot.Lens.ManualFOV, 50.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraInstanceInfiniteParameterTest,
	"ComposableCameraSystem.CameraInstance.InfiniteParameterSection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCameraInstanceInfiniteParameterTest::RunTest(const FString&)
{
	if (!GEditor || GEditor->IsTransactionActive() || !FSlateApplication::IsInitialized()) return false;
	using namespace ComposableCameraSystem::CameraInstanceAuthoring;
	FInstanceAuthoringFixture F;
	auto* Parameter = AddParameterTrack(*F.Track, TEXT("Shot.Lens.ManualFOV"), FFrameTime(20));
	if (!Parameter) return false;
	auto* Section = CastChecked<UMovieSceneComposableCameraInstanceSection>(Parameter->GetAllSections()[0]);
	TestTrue(TEXT("New property is unbounded even when added mid-activation"), Section->GetRange() == TRange<FFrameNumber>::All());
	TestEqual(TEXT("Adding a property creates no keys"), Section->Channels[0].Number.GetNumKeys(), 0);
	const TRange<FFrameNumber> OldRange(20, 40);
	Section->SetRange(OldRange);
	Section->Channels[0].Number.GetData().UpdateOrAddKey(30, FMovieSceneDoubleValue(70.0));
	Section->SetIsLocked(true);
	TestFalse(TEXT("Locked parameter cannot be extended"), MakeParameterSectionInfinite(*Section, *F.Sequencer));
	Section->SetIsLocked(false);
	TestTrue(TEXT("Existing finite parameter converts explicitly"), MakeParameterSectionInfinite(*Section, *F.Sequencer));
	TestTrue(TEXT("Converted section has no bounds"), Section->GetRange() == TRange<FFrameNumber>::All());
	TestEqual(TEXT("Conversion preserves keys"), Section->Channels[0].Number.GetNumKeys(), 1);
	TestTrue(TEXT("Conversion preserves key times"), Section->Channels[0].Number.GetData().GetTimes()[0] == FFrameNumber(30));
	double Value = 0; Section->Channels[0].Number.Evaluate(FFrameTime(30), Value);
	TestEqual(TEXT("Conversion preserves key values"), Value, 70.0);
	TestTrue(TEXT("Activation range is independent of parameter bounds"), F.Activation->GetRange() == TRange<FFrameNumber>(0, 100));
	TestTrue(TEXT("Range conversion can undo"), GEditor->UndoTransaction());
	TestTrue(TEXT("Undo restores finite bounds"), Section->GetRange() == OldRange);
	TestTrue(TEXT("Range conversion can redo"), GEditor->RedoTransaction());
	TestTrue(TEXT("Redo restores unbounded range"), Section->GetRange() == TRange<FFrameNumber>::All());
	TestFalse(TEXT("Activation cannot use the parameter conversion action"), MakeParameterSectionInfinite(*F.Activation, *F.Sequencer));
	auto* Additional = AddInterval(*Parameter, FFrameTime(50));
	TestTrue(TEXT("Explicit additional interval stays finite"), Additional && Additional->HasStartFrame() && Additional->HasEndFrame());
	return true;
}
#endif
