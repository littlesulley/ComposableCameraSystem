// Copyright 2026 Sulley. All Rights Reserved.
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Editors/ComposableCameraInstanceEditorSession.h"
#include "Editors/ComposableCameraShotAuthoringSession.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Actor.h"
#include "ISequencer.h"
#include "ISequencerModule.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "LevelSequence.h"
#include "LevelSequence/ComposableCameraLevelSequenceComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackagePath.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Modules/ModuleManager.h"
#include "MovieScene.h"
#include "MovieSceneCommonHelpers.h"
#include "MovieScene/ComposableCameraInstanceData.h"
#include "MovieScene/MovieSceneComposableCameraInstanceParameterTrack.h"
#include "MovieScene/MovieSceneComposableCameraInstanceSection.h"
#include "MovieScene/MovieSceneComposableCameraInstanceTrack.h"
#include "Nodes/ComposableCameraLensNode.h"
#include "ScopedTransaction.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Sequencer/ComposableCameraInstanceDragDropOp.h"
#include "Sequencer/ComposableCameraInstanceTrackEditor.h"
#include "Sequencer/ComposableCameraInstanceOutput.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/LinkerInstancingContext.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
struct FInstanceCutFixture
{
	TStrongObjectPtr<UWorld> World{UWorld::CreateWorld(EWorldType::Game, false)};
	TStrongObjectPtr<ULevelSequence> Sequence;
	TStrongObjectPtr<UComposableCameraTypeAsset> Type;
	TSharedPtr<ISequencer> Sequencer;
	EAutoChangeMode PreviousAuto = EAutoChangeMode::None;
	explicit FInstanceCutFixture(UPackage* Package = nullptr)
		: Sequence(NewObject<ULevelSequence>(Package ? Package : GetTransientPackage(), NAME_None, RF_Transactional))
		, Type(NewObject<UComposableCameraTypeAsset>(Package ? Package : GetTransientPackage()))
	{
		if (!World.IsValid()) return;
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World.Get());
		World->InitializeActorsForPlay(FURL()); World->BeginPlay();
		Sequence->Initialize();
		auto* Scene = Sequence->GetMovieScene();
		Scene->SetTickResolutionDirectly(FFrameRate(30, 1)); Scene->SetDisplayRate(FFrameRate(30, 1)); Scene->SetPlaybackRange(0, 600);
		Type->NodeTemplates.Add(NewObject<UComposableCameraLensNode>(Type.Get()));
		Type->NodePinOverrides.AddDefaulted(); Type->ExecutionOrder.Add(0);
		auto& Entry = Type->FullExecChain.AddDefaulted_GetRef();
		Entry.EntryType = EComposableCameraExecEntryType::CameraNode; Entry.CameraNodeIndex = 0;
		FSequencerInitParams Params; Params.RootSequence = Sequence.Get(); Params.PlaybackContext = World.Get();
		Params.ViewParams.UniqueName = TEXT("InstanceCutOverwriteRegression");
		Sequencer = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer").CreateSequencer(Params);
		PreviousAuto = Sequencer->GetAutoChangeMode(); Sequencer->SetAutoChangeMode(EAutoChangeMode::None);
	}
	~FInstanceCutFixture()
	{
		if (Sequencer) { Sequencer->SetAutoChangeMode(PreviousAuto); Sequencer->Close(); }
		if (World.IsValid()) { GEngine->DestroyWorldContext(World.Get()); World->DestroyWorld(false); }
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInstanceCutOverwriteTest,
	"ComposableCameraSystem.CameraInstance.CutOverwrite", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FInstanceCutOverwriteTest::RunTest(const FString&)
{
	using namespace ComposableCameraSystem::CameraInstanceAuthoring;
	if (!GEditor || !GEngine || GEditor->IsTransactionActive() || !FSlateApplication::IsInitialized()) return false;
	FInstanceCutFixture F; if (!F.Sequencer) return false;
	auto* Scene = F.Sequence->GetMovieScene();
	FString Error;
	auto* Original = Create(F.Sequencer.ToSharedRef(), F.Type.Get(), 0, Error, 5.);
	if (!TestNotNull(TEXT("Original instance"), Original) || !TestNotNull(TEXT("Original Cut"), Original->ManagedCut.Get())) return false;
	auto* OldCut = Original->ManagedCut.Get();
	const auto Session = MakeShared<FComposableCameraShotAuthoringSession>();
	if (!Session->StartTypeDraft(F.Type.Get(), F.Sequencer, Error)) return false;
	const auto Drag = FComposableCameraInstanceDragDropOp::New(Session, 5., Error);
	if (!Drag) return false;
	TRange<FFrameNumber> Preview;
	Drag->UpdateFeedback(*F.Sequencer, 149, Preview);
	TestTrue(TEXT("Overwrite keeps the requested hover interval"), Preview == TRange<FFrameNumber>(149, 299));
	TestTrue(TEXT("Hover never trims existing Cuts"), OldCut->GetRange() == TRange<FFrameNumber>(0, 150));
	auto* Inserted = Drag->Drop(F.Sequencer.ToSharedRef(), 149);
	if (!TestNotNull(TEXT("Draft drop creates instance"), Inserted) || !TestNotNull(TEXT("One-frame overlap still creates Cut"), Inserted->ManagedCut.Get())) return false;
	TestTrue(TEXT("New Cut exactly covers Activation"), Inserted->ManagedCut->GetRange() == TRange<FFrameNumber>(149, 299));
	TestTrue(TEXT("New Cut references new output"), Inserted->ManagedCut->GetCameraBindingID() == Inserted->OutputBinding);
	TestTrue(TEXT("Old Cut retains only uncovered frames"), OldCut->GetRange() == TRange<FFrameNumber>(0, 149));
	TestNull(TEXT("Trimmed Cut is no longer Activation-managed"), Original->ManagedCut.Get());
	TestTrue(TEXT("Original Activation stays intact"), Original->GetAllSections()[0]->GetRange() == TRange<FFrameNumber>(0, 150));
	const FGuid NewBinding = Inserted->OutputBinding.GetGuid();
	TestTrue(TEXT("Overwrite undo"), GEditor->UndoTransaction());
	TestNull(TEXT("Undo removes new output"), Scene->FindBinding(NewBinding));
	TestTrue(TEXT("Undo restores old range and management"), OldCut->GetRange() == TRange<FFrameNumber>(0, 150) && Original->ManagedCut == OldCut);
	TestTrue(TEXT("Overwrite redo"), GEditor->RedoTransaction());
	TestNotNull(TEXT("Redo restores new output"), Scene->FindBinding(NewBinding));
	TestTrue(TEXT("Redo restores trimming"), OldCut->GetRange() == TRange<FFrameNumber>(0, 149) && !Original->ManagedCut);
	auto* Copy = DuplicateAtTime(F.Sequencer.ToSharedRef(), *Inserted, *Inserted->GetAllSections()[0], 200, Error);
	if (!TestNotNull(TEXT("Duplicate uses overwrite policy"), Copy) || !TestNotNull(TEXT("Duplicate owns new Cut"), Copy->ManagedCut.Get())) return false;
	TestTrue(TEXT("Duplicate Cut uses shifted interval"), Copy->ManagedCut->GetRange() == TRange<FFrameNumber>(200, 350));
	TestNull(TEXT("Duplicate releases overwritten source Cut management"), Inserted->ManagedCut.Get());
	{
		const FScopedTransaction Transaction(NSLOCTEXT("CutOverwriteTest", "MoveOld", "Move Old Activation"));
		Original->GetAllSections()[0]->Modify(); Original->GetAllSections()[0]->SetRange(TRange<FFrameNumber>(10, 140));
		F.Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
	}
	TestTrue(TEXT("Later Activation edits cannot resize an authored remainder"), OldCut->GetRange() == TRange<FFrameNumber>(0, 149));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInstanceSetCameraCutTest,
	"ComposableCameraSystem.CameraInstance.SetCameraCut", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FInstanceSetCameraCutTest::RunTest(const FString&)
{
	using namespace ComposableCameraSystem::CameraInstanceAuthoring;
	if (!GEditor || !GEngine || GEditor->IsTransactionActive() || !FSlateApplication::IsInitialized()) return false;
	FInstanceCutFixture F; if (!F.Sequencer) return false;
	auto* Scene = F.Sequence->GetMovieScene(); FString Error;
	auto* Owner = Create(F.Sequencer.ToSharedRef(), F.Type.Get(), 0, Error, 5.);
	if (!Owner || !Owner->ManagedCut) return false;
	auto* Activation = Owner->GetAllSections()[0];
	auto* Cuts = CastChecked<UMovieSceneCameraCutTrack>(Scene->GetCameraCutTrack());
	Cuts->SetIsAutoManagingSections(false); Cuts->RemoveSection(*Owner->ManagedCut);
	const int32 Bindings = Scene->GetBindings().Num();
	const auto Output = Owner->OutputBinding;
	F.Sequencer->SetLocalTimeDirectly(FFrameTime(450));
	if (!TestTrue(TEXT("Rebuild missing Cut outside Activation"), SetCameraCut(F.Sequencer.ToSharedRef(), *Owner, *Activation, Error))) return false;
	auto* Repaired = Owner->ManagedCut.Get();
	TestTrue(TEXT("Repair reuses output and instance identity"), Owner->OutputBinding == Output && Scene->GetBindings().Num() == Bindings);
	TestTrue(TEXT("Repaired Cut covers exact Activation"), Repaired && Repaired->GetRange() == Activation->GetRange() && Repaired->GetCameraBindingID() == Output);
	TestEqual(TEXT("Setting Cut does not seek"), F.Sequencer->GetLocalTime().Time.FloorToFrame().Value, 450);
	TestTrue(TEXT("Repeated request succeeds without replacing Cut"), SetCameraCut(F.Sequencer.ToSharedRef(), *Owner, *Activation, Error) && Owner->ManagedCut == Repaired);
	TestTrue(TEXT("One Undo removes repair even after repeated request"), GEditor->UndoTransaction());
	TestEqual(TEXT("Missing-Cut state restored"), Cuts->GetAllSections().Num(), 0);
	TestTrue(TEXT("Redo restores repair"), GEditor->RedoTransaction());
	TestTrue(TEXT("Redo restores managed pointer"), Owner->ManagedCut == Repaired && Cuts->HasSection(*Repaired));
	auto* Other = Create(F.Sequencer.ToSharedRef(), F.Type.Get(), 60, Error, 1.);
	if (!Other || !Other->ManagedCut) return false;
	auto* OtherCut = Other->ManagedCut.Get(); OtherCut->SetIsLocked(true);
	const int32 BeforeRejected = Cuts->GetAllSections().Num();
	TestFalse(TEXT("Locked overlap refuses repair"), SetCameraCut(F.Sequencer.ToSharedRef(), *Owner, *Activation, Error));
	TestEqual(TEXT("Rejected repair preserves sections"), Cuts->GetAllSections().Num(), BeforeRejected);
	OtherCut->SetIsLocked(false);
	TestTrue(TEXT("Repair replaces overlapping other Cut"), SetCameraCut(F.Sequencer.ToSharedRef(), *Owner, *Activation, Error));
	TestNull(TEXT("Old Cut loses management"), Other->ManagedCut.Get());
	TestTrue(TEXT("Other Activation remains intact"), Other->GetAllSections()[0]->GetRange() == TRange<FFrameNumber>(60, 90));
	TestTrue(TEXT("Overwrite undo restores other Cut"), GEditor->UndoTransaction() && Other->ManagedCut == OtherCut && Cuts->HasSection(*OtherCut));
	TestTrue(TEXT("Overwrite redo"), GEditor->RedoTransaction());
	Scene->SetReadOnly(true);
	TestFalse(TEXT("Read-only source refuses repair"), SetCameraCut(F.Sequencer.ToSharedRef(), *Owner, *Activation, Error)); Scene->SetReadOnly(false);
	Activation->SetIsLocked(true);
	TestFalse(TEXT("Locked Activation refuses repair"), SetCameraCut(F.Sequencer.ToSharedRef(), *Owner, *Activation, Error)); Activation->SetIsLocked(false);
	TestFalse(TEXT("Foreign Activation refuses repair"), SetCameraCut(F.Sequencer.ToSharedRef(), *Owner, *Other->GetAllSections()[0], Error));
	Owner->OutputBinding = FMovieSceneObjectBindingID();
	TestFalse(TEXT("Missing output refuses Cut repair without creating a camera"), SetCameraCut(F.Sequencer.ToSharedRef(), *Owner, *Activation, Error));
	TestEqual(TEXT("Repair failure never creates bindings"), Scene->GetBindings().Num(), Bindings + 1);
	Owner->OutputBinding = Output;
	Activation->SetRange(TRange<FFrameNumber>::All());
	TestFalse(TEXT("Unbounded Activation refuses repair"), SetCameraCut(F.Sequencer.ToSharedRef(), *Owner, *Activation, Error));
	Activation->SetRange(TRange<FFrameNumber>(0, 150));
	Scene->RemoveCameraCutTrack();
	TestTrue(TEXT("Repair can recreate the missing Cut track"), SetCameraCut(F.Sequencer.ToSharedRef(), *Owner, *Activation, Error));
	TestTrue(TEXT("Missing-track repair retains one output per instance"), Scene->GetBindings().Num() == Bindings + 1 && Owner->OutputBinding == Output);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInstanceCutOverwriteRangesTest,
	"ComposableCameraSystem.CameraInstance.CutOverwriteRanges", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FInstanceCutOverwriteRangesTest::RunTest(const FString&)
{
	using namespace ComposableCameraSystem::CameraInstanceAuthoring;
	if (!GEditor || !GEngine || GEditor->IsTransactionActive() || !FSlateApplication::IsInitialized()) return false;
	// Replace [100,200): trim either end, remove covered Cuts, split enclosing/open Cuts.
	for (const auto OldRange : { TRange<FFrameNumber>(0, 150), TRange<FFrameNumber>(150, 300),
		TRange<FFrameNumber>(120, 180), TRange<FFrameNumber>(0, 300), TRange<FFrameNumber>::All(), TRange<FFrameNumber>(0, 100) })
	{
		FInstanceCutFixture F; if (!F.Sequencer) return false;
		auto* Scene = F.Sequence->GetMovieScene();
		auto* Cuts = CastChecked<UMovieSceneCameraCutTrack>(Scene->AddCameraCutTrack(UMovieSceneCameraCutTrack::StaticClass()));
		const FMovieSceneObjectBindingID OldBinding = UE::MovieScene::FRelativeObjectBindingID(FGuid::NewGuid());
		auto* OldCut = CastChecked<UMovieSceneCameraCutSection>(Cuts->CreateNewSection());
		OldCut->SetRange(OldRange); OldCut->SetCameraBindingID(OldBinding); Cuts->AddSection(*OldCut);
		OldCut->Easing.bManualEaseIn = OldCut->Easing.bManualEaseOut = true;
		OldCut->Easing.ManualEaseInDuration = 7; OldCut->Easing.ManualEaseOutDuration = 9;
		auto* Inactive = CastChecked<UMovieSceneCameraCutSection>(Cuts->CreateNewSection());
		Inactive->SetRange(TRange<FFrameNumber>(0, 300)); Inactive->SetIsActive(false); Inactive->SetIsLocked(true); Cuts->AddSection(*Inactive);
		// Native auto-arrange is on by default; removing a covered Cut must not fill this gap.
		auto* Unrelated = CastChecked<UMovieSceneCameraCutSection>(Cuts->CreateNewSection());
		Unrelated->SetRange(TRange<FFrameNumber>(400, 500)); Cuts->AddSection(*Unrelated);
		FString Error;
		auto* Inserted = Create(F.Sequencer.ToSharedRef(), F.Type.Get(), 100, Error, 100. / 30.);
		if (!TestNotNull(TEXT("Range overwrite creates instance"), Inserted) || !TestNotNull(TEXT("Range overwrite creates Cut"), Inserted->ManagedCut.Get())) return false;
		const int32 ExpectedRemainders = OldRange == TRange<FFrameNumber>(120, 180) ? 0
			: OldRange == TRange<FFrameNumber>(0, 300) || OldRange == TRange<FFrameNumber>::All() ? 2 : 1;
		TestEqual(TEXT("Split/remove yields expected section count"), Cuts->GetAllSections().Num(), ExpectedRemainders + 3);
		for (const auto* Base : Cuts->GetAllSections())
		{
			const auto* Cut = CastChecked<UMovieSceneCameraCutSection>(Base);
			if (Cut == Inserted->ManagedCut || Cut == Inactive || Cut == Unrelated) continue;
			TestTrue(TEXT("Remainder keeps original binding"), Cut->GetCameraBindingID() == OldBinding);
			TestFalse(TEXT("Remainder cannot overlap new Cut"), Cut->GetRange().Overlaps(TRange<FFrameNumber>(100, 200)));
			TestEqual(TEXT("Only original entry keeps manual easing"), Cut->Easing.bManualEaseIn, Cut->GetRange().GetLowerBound() == OldRange.GetLowerBound());
			TestEqual(TEXT("Only original exit keeps manual easing"), Cut->Easing.bManualEaseOut, Cut->GetRange().GetUpperBound() == OldRange.GetUpperBound());
		}
		for (const int32 Frame : { -10, 0, 99, 100, 149, 199, 200, 299, 300, 600 })
		{
			int32 OldCoverage = 0;
			for (const auto* Base : Cuts->GetAllSections())
				if (const auto* Cut = Cast<UMovieSceneCameraCutSection>(Base); Cut && Cut->GetCameraBindingID() == OldBinding && Cut->GetRange().Contains(FFrameNumber(Frame))) ++OldCoverage;
			TestEqual(TEXT("Outside coverage is preserved exactly"), OldCoverage, OldRange.Contains(FFrameNumber(Frame)) && (Frame < 100 || Frame >= 200) ? 1 : 0);
		}
		TestTrue(TEXT("Inactive locked Cut is untouched"), Inactive->GetRange() == TRange<FFrameNumber>(0, 300) && Inactive->IsLocked() && !Inactive->IsActive());
		TestTrue(TEXT("Unrelated range and gap survive"), Unrelated->GetRange() == TRange<FFrameNumber>(400, 500));
		TestTrue(TEXT("Auto-arrange setting restored"), Cuts->IsAutoManagingSections());
		TestTrue(TEXT("Range edit undo"), GEditor->UndoTransaction());
		TestTrue(TEXT("Undo restores original section and range"), Cuts->HasSection(*OldCut) && OldCut->GetRange() == OldRange && Cuts->GetAllSections().Num() == 3);
		TestTrue(TEXT("Range edit redo"), GEditor->RedoTransaction());
		TestEqual(TEXT("Redo restores all remainders"), Cuts->GetAllSections().Num(), ExpectedRemainders + 3);
	}
	{
		FInstanceCutFixture Multi; if (!Multi.Sequencer) return false;
		auto* Scene = Multi.Sequence->GetMovieScene();
		auto* Cuts = CastChecked<UMovieSceneCameraCutTrack>(Scene->AddCameraCutTrack(UMovieSceneCameraCutTrack::StaticClass()));
		Cuts->SetIsAutoManagingSections(false);
		TArray<UMovieSceneCameraCutSection*> OldCuts;
		for (const auto Range : { TRange<FFrameNumber>(0, 120), TRange<FFrameNumber>(130, 170), TRange<FFrameNumber>(180, 300) })
		{
			auto* Cut = CastChecked<UMovieSceneCameraCutSection>(Cuts->CreateNewSection());
			Cut->SetRange(Range); Cuts->AddSection(*Cut); OldCuts.Add(Cut);
		}
		FString Error;
		auto* Inserted = Create(Multi.Sequencer.ToSharedRef(), Multi.Type.Get(), 100, Error, 100. / 30.);
		if (!TestNotNull(TEXT("One insertion spans several Cuts"), Inserted)) return false;
		TestEqual(TEXT("Spanning insertion retains two edges and new Cut"), Cuts->GetAllSections().Num(), 3);
		TestTrue(TEXT("First Cut is trimmed"), OldCuts[0]->GetRange() == TRange<FFrameNumber>(0, 100));
		TestFalse(TEXT("Fully covered middle Cut is removed"), Cuts->HasSection(*OldCuts[1]));
		TestTrue(TEXT("Last Cut is trimmed"), OldCuts[2]->GetRange() == TRange<FFrameNumber>(200, 300));
		TestFalse(TEXT("Disabled auto-arrange preference stays disabled"), Cuts->IsAutoManagingSections());
	}
	FInstanceCutFixture F; if (!F.Sequencer) return false;
	auto* Scene = F.Sequence->GetMovieScene(); FString Error;
	auto* Original = Create(F.Sequencer.ToSharedRef(), F.Type.Get(), 0, Error, 5.);
	if (!Original || !Original->ManagedCut) return false;
	Original->ManagedCut->SetIsLocked(true);
	const int32 BindingCount = Scene->GetBindings().Num();
	const auto Session = MakeShared<FComposableCameraShotAuthoringSession>();
	if (!Session->StartTypeDraft(F.Type.Get(), F.Sequencer, Error)) return false;
	const auto Drag = FComposableCameraInstanceDragDropOp::New(Session, 5., Error);
	if (!Drag) return false;
	TRange<FFrameNumber> Preview; Drag->UpdateFeedback(*F.Sequencer, 149, Preview);
	TestTrue(TEXT("Locked overlap hover is a bounded empty range"), Preview.HasLowerBound() && Preview.HasUpperBound() && Preview.IsEmpty());
	TestNull(TEXT("Create rejects locked overlap before mutation"), Create(F.Sequencer.ToSharedRef(), F.Type.Get(), 149, Error, 5.));
	TestFalse(TEXT("Locked rejection explains reason"), Error.IsEmpty());
	TestNull(TEXT("Duplicate rejects locked overlap"), DuplicateAtTime(F.Sequencer.ToSharedRef(), *Original, *Original->GetAllSections()[0], 149, Error));
	TestEqual(TEXT("Rejected operations create no output"), Scene->GetBindings().Num(), BindingCount);
	TestTrue(TEXT("Rejected operations preserve old Cut"), Original->ManagedCut->GetRange() == TRange<FFrameNumber>(0, 150));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInstanceManualCutEditingTest,
	"ComposableCameraSystem.CameraInstance.ManualCutEditing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FInstanceManualCutEditingTest::RunTest(const FString&)
{
	using namespace ComposableCameraSystem::CameraInstanceAuthoring;
	if (!GEditor || !GEngine || GEditor->IsTransactionActive() || !FSlateApplication::IsInitialized()) return false;
	FInstanceCutFixture F; if (!F.Sequencer) return false;
	auto* Scene = F.Sequence->GetMovieScene(); FString Error;
	auto* Owner = Create(F.Sequencer.ToSharedRef(), F.Type.Get(), 0, Error, 5.);
	if (!Owner || !Owner->ManagedCut) return false;
	auto* Activation = Owner->GetAllSections()[0];
	auto* Cuts = CastChecked<UMovieSceneCameraCutTrack>(Scene->GetCameraCutTrack());
	Cuts->SetIsAutoManagingSections(false); Cuts->RemoveSection(*Owner->ManagedCut);
	// Exercise the reported entry point, whose notification occurs after its transaction.
	if (!SetCameraCut(F.Sequencer.ToSharedRef(), *Owner, *Activation, Error)) return false;
	auto* Cut = Owner->ManagedCut.Get(); if (!Cut) return false;
	const auto Output = Owner->OutputBinding;
	const int32 BindingCount = Scene->GetBindings().Num();
	{
		const FScopedTransaction Transaction(NSLOCTEXT("CutEditingTest", "Move", "Move Camera Cut"));
		Cut->Modify(); Cut->MoveSection(FFrameNumber(15));
		F.Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
		Cut->MoveSection(FFrameNumber(15));
		F.Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
	}
	F.Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
	TestTrue(TEXT("Interactive Cut drag is not pulled back"), Cut->GetRange() == TRange<FFrameNumber>(30, 180));
	TestNull(TEXT("Manual range becomes authored"), Owner->ManagedCut.Get());
	TestTrue(TEXT("Cut drag preserves Activation and camera identity"), Activation->GetRange() == TRange<FFrameNumber>(0, 150)
		&& Cut->GetCameraBindingID() == Output && Owner->OutputBinding == Output && Scene->GetBindings().Num() == BindingCount);
	TestTrue(TEXT("One Undo restores range and management"), GEditor->UndoTransaction()
		&& Cut->GetRange() == TRange<FFrameNumber>(0, 150) && Owner->ManagedCut == Cut);
	TestTrue(TEXT("Redo restores authored range"), GEditor->RedoTransaction()
		&& Cut->GetRange() == TRange<FFrameNumber>(30, 180) && !Owner->ManagedCut);
	if (!TestTrue(TEXT("Undo again restores managed Cut"), GEditor->UndoTransaction() && Owner->ManagedCut == Cut)) return false;
	{
		const FScopedTransaction Transaction(NSLOCTEXT("CutEditingTest", "MoveActivation", "Move Activation After Undo"));
		Activation->Modify(); Activation->MoveSection(FFrameNumber(10));
		F.Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
	}
	TestTrue(TEXT("Restored management follows Activation without an intervening tick"), Owner->ManagedCut == Cut
		&& Cut->GetRange() == TRange<FFrameNumber>(10, 160));
	{
		const FScopedTransaction Transaction(NSLOCTEXT("CutEditingTest", "Trim", "Trim Camera Cut"));
		Cut->Modify(); Cut->SetRange(TRange<FFrameNumber>(10, 120));
		F.Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
	}
	TestTrue(TEXT("Trim stays authored"), !Owner->ManagedCut && Cut->GetRange() == TRange<FFrameNumber>(10, 120));
	{
		const FScopedTransaction Transaction(NSLOCTEXT("CutEditingTest", "LaterActivation", "Move Activation After Trim"));
		Activation->Modify(); Activation->MoveSection(FFrameNumber(10));
		F.Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
	}
	TestTrue(TEXT("Later Activation edits preserve authored trim"), Cut->GetRange() == TRange<FFrameNumber>(10, 120));
	TestTrue(TEXT("Set Camera Cut explicitly restores management"), SetCameraCut(F.Sequencer.ToSharedRef(), *Owner, *Activation, Error)
		&& Owner->ManagedCut && Owner->ManagedCut->GetRange() == Activation->GetRange()
		&& Owner->ManagedCut->GetCameraBindingID() == Output && Scene->GetBindings().Num() == BindingCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInstanceManagedCutFollowingTest,
	"ComposableCameraSystem.CameraInstance.ManagedCutFollowing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FInstanceManagedCutFollowingTest::RunTest(const FString&)
{
	using namespace ComposableCameraSystem::CameraInstanceAuthoring;
	if (!GEditor || !GEngine || GEditor->IsTransactionActive() || !FSlateApplication::IsInitialized()) return false;
	FInstanceCutFixture F; if (!F.Sequencer) return false;
	auto* Scene = F.Sequence->GetMovieScene(); FString Error;
	auto* Owner = Create(F.Sequencer.ToSharedRef(), F.Type.Get(), 0, Error, 5.);
	if (!Owner || !Owner->ManagedCut) return false;
	auto* Activation = Owner->GetAllSections()[0];
	auto* Cut = Owner->ManagedCut.Get();
	{
		const FScopedTransaction Transaction(NSLOCTEXT("CutEditingTest", "GroupMove", "Move Activation And Cut"));
		Activation->Modify(); Cut->Modify();
		Activation->MoveSection(FFrameNumber(10)); Cut->MoveSection(FFrameNumber(10));
		F.Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
	}
	TestTrue(TEXT("Equal group move retains management"), Owner->ManagedCut == Cut && Cut->GetRange() == TRange<FFrameNumber>(10, 160));
	{
		const FScopedTransaction Transaction(NSLOCTEXT("CutEditingTest", "TrimActivation", "Trim Activation"));
		Activation->Modify(); Activation->SetRange(TRange<FFrameNumber>(20, 140));
		F.Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
	}
	TestTrue(TEXT("Activation-only trim follows"), Owner->ManagedCut == Cut && Cut->GetRange() == TRange<FFrameNumber>(20, 140));
	auto* Cuts = CastChecked<UMovieSceneCameraCutTrack>(Scene->GetCameraCutTrack());
	Cuts->SetIsAutoManagingSections(false);
	auto* Other = CastChecked<UMovieSceneCameraCutSection>(Cuts->CreateNewSection());
	Other->SetRange(TRange<FFrameNumber>(180, 250)); Cuts->AddSection(*Other);
	F.Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
	{
		const FScopedTransaction Transaction(NSLOCTEXT("CutEditingTest", "ConflictingMove", "Move Activation Into Another Cut"));
		Activation->Modify(); Activation->SetRange(TRange<FFrameNumber>(100, 250));
		F.Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
	}
	TestTrue(TEXT("Automatic follow still respects Cut conflicts"), Owner->ManagedCut == Cut && Cut->GetRange() == TRange<FFrameNumber>(20, 140));
	{
		const FScopedTransaction Transaction(NSLOCTEXT("CutEditingTest", "Unrelated", "Disable Other Cut"));
		Other->Modify(); Other->SetIsActive(false);
		F.Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
	}
	TestTrue(TEXT("Unrelated edit cannot resync a previously blocked range"), Cut->GetRange() == TRange<FFrameNumber>(20, 140));
	{
		const FScopedTransaction Transaction(NSLOCTEXT("CutEditingTest", "BothDiffer", "Edit Both Ranges Independently"));
		Activation->Modify(); Cut->Modify();
		Activation->SetRange(TRange<FFrameNumber>(110, 260)); Cut->SetRange(TRange<FFrameNumber>(30, 150));
		F.Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
	}
	TestTrue(TEXT("Different simultaneous edits preserve authored Cut"), !Owner->ManagedCut
		&& Cut->GetRange() == TRange<FFrameNumber>(30, 150) && Activation->GetRange() == TRange<FFrameNumber>(110, 260));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInstanceManagedCutBatchMoveTest,
	"ComposableCameraSystem.CameraInstance.ManagedCutBatchMove", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FInstanceManagedCutBatchMoveTest::RunTest(const FString&)
{
	using namespace ComposableCameraSystem::CameraInstanceAuthoring;
	if (!GEditor || !GEngine || GEditor->IsTransactionActive() || !FSlateApplication::IsInitialized()) return false;
	FInstanceCutFixture F; if (!F.Sequencer) return false;
	auto* Scene = F.Sequence->GetMovieScene(); FString Error;
	TArray<UMovieSceneComposableCameraInstanceTrack*> Owners;
	TArray<UMovieSceneSection*> Activations;
	TArray<UMovieSceneCameraCutSection*> Cuts;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		auto* Owner = Create(F.Sequencer.ToSharedRef(), F.Type.Get(), 100 + Index * 100, Error, 100. / 30.);
		if (!TestNotNull(TEXT("Batch instance"), Owner) || !TestNotNull(TEXT("Batch managed Cut"), Owner->ManagedCut.Get())) return false;
		Owners.Add(Owner); Activations.Add(Owner->GetAllSections()[0]); Cuts.Add(Owner->ManagedCut.Get());
	}
	auto* CutTrack = CastChecked<UMovieSceneCameraCutTrack>(Scene->GetCameraCutTrack());
	CutTrack->SetIsAutoManagingSections(false);
	const auto Range = [](int32 Index, int32 Delta = 0) { return TRange<FFrameNumber>(100 + Index * 100 + Delta, 200 + Index * 100 + Delta); };
	const auto MoveActivations = [&](int32 Delta)
	{
		const FScopedTransaction Transaction(NSLOCTEXT("CutEditingTest", "BatchMove", "Move Camera Activations Together"));
		for (auto* Activation : Activations) { Activation->Modify(); Activation->MoveSection(FFrameNumber(Delta)); }
		F.Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
	};
	// Binding GUID order is unrelated to timeline order. Opposite directions with
	// the same owners expose either ordering of the old sequential conflict check.
	for (const int32 Delta : { 20, -20 })
	{
		MoveActivations(Delta);
		F.Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
		for (int32 Index = 0; Index < Owners.Num(); ++Index)
			TestTrue(TEXT("Every Cut follows the final batch range"), Activations[Index]->GetRange() == Range(Index, Delta)
				&& Cuts[Index]->GetRange() == Range(Index, Delta) && Owners[Index]->ManagedCut == Cuts[Index]);
		if (!TestTrue(TEXT("Batch move is one Undo"), GEditor->UndoTransaction())) return false;
		for (int32 Index = 0; Index < Owners.Num(); ++Index)
			TestTrue(TEXT("Undo restores all Activation and Cut ranges"), Activations[Index]->GetRange() == Range(Index)
				&& Cuts[Index]->GetRange() == Range(Index) && Owners[Index]->ManagedCut == Cuts[Index]);
		if (!TestTrue(TEXT("Batch move Redo"), GEditor->RedoTransaction())) return false;
		for (int32 Index = 0; Index < Owners.Num(); ++Index)
			TestTrue(TEXT("Redo restores all followed ranges"), Activations[Index]->GetRange() == Range(Index, Delta)
				&& Cuts[Index]->GetRange() == Range(Index, Delta));
		if (!TestTrue(TEXT("Restore baseline for opposite direction"), GEditor->UndoTransaction())) return false;
	}
	// The tail cannot move into this authored Cut. Rejecting it must also block
	// its predecessor, then the head, whose proposed ranges overlap the old ranges.
	auto* External = CastChecked<UMovieSceneCameraCutSection>(CutTrack->CreateNewSection());
	External->SetRange(TRange<FFrameNumber>(410, 450)); External->SetCameraBindingID(Owners[0]->OutputBinding);
	CutTrack->AddSection(*External);
	F.Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
	for (const bool bLocked : { false, true })
	{
		External->SetIsLocked(bLocked);
		MoveActivations(20);
		F.Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
		for (int32 Index = 0; Index < Owners.Num(); ++Index)
			TestTrue(TEXT("Blocked dependency chain preserves each old Cut"), Activations[Index]->GetRange() == Range(Index, 20)
				&& Cuts[Index]->GetRange() == Range(Index) && Owners[Index]->ManagedCut == Cuts[Index]);
		TestTrue(TEXT("Authored external Cut keeps range, binding and lock"), External->GetRange() == TRange<FFrameNumber>(410, 450)
			&& External->GetCameraBindingID() == Owners[0]->OutputBinding && External->IsLocked() == bLocked);
		if (!TestTrue(TEXT("Blocked batch Activation move can be undone"), GEditor->UndoTransaction())) return false;
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInstanceMultiActivationCutOwnershipTest,
	"ComposableCameraSystem.CameraInstance.MultiActivationCutOwnership", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FInstanceMultiActivationCutOwnershipTest::RunTest(const FString&)
{
	using namespace ComposableCameraSystem::CameraInstanceAuthoring;
	if (!GEditor || !GEngine || GEditor->IsTransactionActive() || !FSlateApplication::IsInitialized()) return false;
	FInstanceCutFixture F; if (!F.Sequencer) return false;
	FString Error;
	auto* Owner = Create(F.Sequencer.ToSharedRef(), F.Type.Get(), 0, Error, 5.);
	if (!Owner || !Owner->ManagedCut) return false;
	auto* Activation = Owner->GetAllSections()[0];
	auto* Later = Owner->CreateNewSection(); Later->SetRange(TRange<FFrameNumber>(200, 260)); Owner->AddSection(*Later);
	F.Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
	if (!TestTrue(TEXT("Set Camera Cut supports a multi-Activation instance"), SetCameraCut(F.Sequencer.ToSharedRef(), *Owner, *Activation, Error))) return false;
	auto* Cut = Owner->ManagedCut.Get(); if (!Cut) return false;
	const auto Binding = Owner->OutputBinding;
	{
		const FScopedTransaction Transaction(NSLOCTEXT("CutEditingTest", "MultiTrim", "Trim Multi-Activation Camera Cut"));
		Cut->Modify(); Cut->SetRange(TRange<FFrameNumber>(10, 120));
		F.Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
	}
	TestTrue(TEXT("Manual trim releases management with multiple Activations"), !Owner->ManagedCut
		&& Cut->GetRange() == TRange<FFrameNumber>(10, 120) && Cut->GetCameraBindingID() == Binding);
	if (!TestTrue(TEXT("Undo restores multi-Activation Cut ownership"), GEditor->UndoTransaction()
		&& Owner->ManagedCut == Cut && Cut->GetRange() == TRange<FFrameNumber>(0, 150))) return false;
	if (!TestTrue(TEXT("Redo restores authored trim and release"), GEditor->RedoTransaction()
		&& !Owner->ManagedCut && Cut->GetRange() == TRange<FFrameNumber>(10, 120))) return false;
	{
		const FScopedTransaction Transaction(NSLOCTEXT("CutEditingTest", "RemoveLater", "Remove Later Activation"));
		Owner->Modify(); Owner->RemoveSection(*Later);
		F.Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemRemoved);
	}
	{
		const FScopedTransaction Transaction(NSLOCTEXT("CutEditingTest", "MoveRemaining", "Move Remaining Activation"));
		Activation->Modify(); Activation->MoveSection(FFrameNumber(10));
		F.Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
	}
	TestTrue(TEXT("Returning to one Activation cannot restore automatic management"), Owner->GetAllSections().Num() == 1
		&& !Owner->ManagedCut && Cut->GetRange() == TRange<FFrameNumber>(10, 120)
		&& Activation->GetRange() == TRange<FFrameNumber>(10, 160) && Owner->OutputBinding == Binding);
	TestTrue(TEXT("Explicit Set Camera Cut restores a matching managed interval"), SetCameraCut(F.Sequencer.ToSharedRef(), *Owner, *Activation, Error)
		&& Owner->ManagedCut && Owner->ManagedCut->GetRange() == Activation->GetRange());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInstanceOutputPersistenceTest,
	"ComposableCameraSystem.CameraInstance.OutputPersistence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FInstanceOutputPersistenceTest::RunTest(const FString&)
{
	using namespace ComposableCameraSystem::CameraInstanceAuthoring;
	if (!GEditor || !GEngine || GEditor->IsTransactionActive() || !FSlateApplication::IsInitialized()) return false;
	const FString PackageName = TEXT("/Temp/CCSCameraOutput_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	TStrongObjectPtr<UPackage> SourcePackage(CreatePackage(*PackageName));
	TStrongObjectPtr<UPackage> LoadedPackage(CreatePackage(*(PackageName + TEXT("_Reload"))));
	const FString Filename = FPaths::CreateTempFilename(FPlatformProcess::UserTempDir(), TEXT("CCSCameraOutput"), TEXT(".uasset"));
	ON_SCOPE_EXIT { ResetLoaders(LoadedPackage.Get()); IFileManager::Get().Delete(*Filename, false, true); };
	FInstanceCutFixture F(SourcePackage.Get());
	if (!F.Sequencer) return false;
	F.Sequence->SetFlags(RF_Public); F.Type->SetFlags(RF_Public);
	auto& Input = F.Type->ExposedParameters.AddDefaulted_GetRef();
	Input.ParameterName = TEXT("FocalLength"); Input.PinType = EComposableCameraPinType::Float;
	Input.TargetNodeIndex = 0; Input.TargetPinName = TEXT("FocalLength");
	F.Type->Build(false);
	FString Error;
	auto* Owner = Create(F.Sequencer.ToSharedRef(), F.Type.Get(), 0, Error, 5.);
	if (!TestNotNull(TEXT("Create camera for persistence"), Owner)) return false;
	const FGuid Binding = Owner->OutputBinding.GetGuid();
	auto* Template = Cast<AActor>(MovieSceneHelpers::GetObjectTemplate(F.Sequence.Get(), Binding, F.Sequencer->GetSharedPlaybackState()));
	if (!TestNotNull(TEXT("Output has a stored template"), Template)) return false;
	TestFalse(TEXT("Output template can be exported"), Template->HasAnyFlags(RF_Transient));
	for (UActorComponent* Component : Template->GetComponents())
		TestFalse(TEXT("Output components can be exported"), Component->HasAnyFlags(RF_Transient));
	auto* Parameter = AddParameterTrack(*Owner, TEXT("Parameters.FocalLength"), FFrameTime(0));
	if (!TestNotNull(TEXT("Parameter lane exists before save"), Parameter)) return false;
	auto* ParameterSection = CastChecked<UMovieSceneComposableCameraInstanceSection>(Parameter->GetAllSections()[0]);
	TArray<FComposableCameraInstanceField> Fields; Owner->Instance->GatherFields(Fields);
	auto* Field = Fields.FindByPredicate([](const auto& Value) { return Value.Path == TEXT("Parameters.FocalLength"); });
	if (!Field) return false;
	Field->WriteNumber(45.); ParameterSection->AddKey(*Field, 1);
	const FName SequenceName = F.Sequence->GetFName();
	FSavePackageArgs SaveArgs; SaveArgs.TopLevelFlags = RF_Public; SaveArgs.bSlowTask = false;
	if (!TestTrue(TEXT("Save real package"), UPackage::SavePackage(SourcePackage.Get(), F.Sequence.Get(), *Filename, SaveArgs))) return false;
	F.Sequencer->SetAutoChangeMode(F.PreviousAuto); F.Sequencer->Close(); F.Sequencer.Reset();
	// Load into a distinct package: an in-memory Serialize/PostLoad call would not
	// catch templates omitted from exports, or accidentally reuse the source objects.
	FLinkerInstancingContext Context;
	Context.AddPackageMapping(SourcePackage->GetFName(), LoadedPackage->GetFName());
	if (!TestNotNull(TEXT("Reload real package"), LoadPackage(LoadedPackage.Get(), FPackagePath::FromLocalPath(Filename), LOAD_None, nullptr, &Context))) return false;
	auto* Reloaded = FindObject<ULevelSequence>(LoadedPackage.Get(), *SequenceName.ToString());
	if (!TestNotNull(TEXT("Reloaded Sequence"), Reloaded)) return false;
	TestTrue(TEXT("Reload uses distinct objects"), Reloaded != F.Sequence.Get());
	auto* ReloadedOwner = UMovieSceneComposableCameraInstanceTrack::Find(Reloaded->GetMovieScene(), Binding);
	if (!TestNotNull(TEXT("Original binding retains its Activation"), ReloadedOwner)) return false;
	TestTrue(TEXT("Output identity survives save/load"), ReloadedOwner->OutputBinding == Owner->OutputBinding);
	if (!TestNotNull(TEXT("Managed Cut survives save/load"), ReloadedOwner->ManagedCut.Get())) return false;
	TestTrue(TEXT("Cut keeps original output binding"), ReloadedOwner->ManagedCut->GetCameraBindingID() == ReloadedOwner->OutputBinding);
	auto* ReloadedParameter = FindParameterTrack(*ReloadedOwner, TEXT("Parameters.FocalLength"));
	if (!TestNotNull(TEXT("Parameter remains below original binding"), ReloadedParameter)) return false;
	if (!TestEqual(TEXT("Parameter section survives save/load"), ReloadedParameter->GetAllSections().Num(), 1)) return false;
	auto* ReloadedSection = CastChecked<UMovieSceneComposableCameraInstanceSection>(ReloadedParameter->GetAllSections()[0]);
	if (!TestEqual(TEXT("Parameter channel survives save/load"), ReloadedSection->Channels.Num(), 1)) return false;
	TestEqual(TEXT("Parameter key survives save/load"), ReloadedSection->Channels[0].Number.GetData().GetTimes().Num(), 1);
	FSequencerInitParams Params; Params.RootSequence = Reloaded; Params.PlaybackContext = F.World.Get();
	Params.ViewParams.UniqueName = TEXT("InstanceOutputPersistenceReload");
	const auto Seq = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer").CreateSequencer(Params);
	ON_SCOPE_EXIT { Seq->Close(); };
	auto* ReloadedTemplate = Cast<AActor>(MovieSceneHelpers::GetObjectTemplate(Reloaded, Binding, Seq->GetSharedPlaybackState()));
	if (!TestNotNull(TEXT("Reload retains persistent camera template"), ReloadedTemplate)) return false;
	TestTrue(TEXT("Template is owned by reloaded package"), ReloadedTemplate->GetOutermost() == LoadedPackage.Get());
	auto* Component = ReloadedTemplate->FindComponentByClass<UComposableCameraLevelSequenceComponent>();
	TestTrue(TEXT("Template ownership resolves to reloaded instance"), Component && Component->CameraInstanceOwner == ReloadedOwner);
	Seq->SetLocalTimeDirectly(FFrameTime(1)); Seq->ForceEvaluate();
	TestNotNull(TEXT("Reloaded Cut resolves a camera"), ReloadedOwner->ManagedCut->GetFirstCamera(*Seq, Seq->GetFocusedTemplateID()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInstanceRepairMissingTemplateTest,
	"ComposableCameraSystem.CameraInstance.RepairMissingOutputTemplate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FInstanceRepairMissingTemplateTest::RunTest(const FString&)
{
	using namespace ComposableCameraSystem;
	if (!GEditor || !GEngine || GEditor->IsTransactionActive() || !FSlateApplication::IsInitialized()) return false;
	FInstanceCutFixture F; if (!F.Sequencer) return false;
	FString Error;
	auto* Owner = CameraInstanceAuthoring::Create(F.Sequencer.ToSharedRef(), F.Type.Get(), 0, Error, 5.);
	if (!TestNotNull(TEXT("Instance to repair"), Owner)) return false;
	auto* Scene = F.Sequence->GetMovieScene();
	const auto Binding = Owner->OutputBinding;
	auto* Cut = Owner->ManagedCut.Get();
	auto* Spawnable = Scene->FindSpawnable(Binding.GetGuid());
	if (!TestNotNull(TEXT("Legacy spawnable fixture"), Spawnable) || !TestNotNull(TEXT("Existing Cut"), Cut)) return false;
	const int32 BindingCount = Scene->GetBindings().Num();
	auto* Parameter = Scene->AddTrack<UMovieSceneComposableCameraInstanceParameterTrack>(Binding.GetGuid());
	if (!TestNotNull(TEXT("Parameter lane before repair"), Parameter)) return false;
	auto* OldTemplate = CastChecked<AActor>(Spawnable->GetObjectTemplate());
	auto* OldComponent = OldTemplate->FindComponentByClass<UComposableCameraLevelSequenceComponent>();
	OldTemplate->SetFlags(RF_Transient); OldComponent->SetFlags(RF_Transient);
	{
		const FScopedTransaction Transaction(NSLOCTEXT("InstanceOutputTest", "Normalize", "Normalize Output Template"));
		TestTrue(TEXT("Repair also normalizes an unsaved old template"), CameraInstanceOutput::Ensure(F.Sequencer.ToSharedRef(), *Owner, Error));
	}
	TestFalse(TEXT("Old template and component become persistent"), OldTemplate->HasAnyFlags(RF_Transient) || OldComponent->HasAnyFlags(RF_Transient));
	TestTrue(TEXT("Normalization retains existing template and Cut"), Spawnable->GetObjectTemplate() == OldTemplate && Owner->ManagedCut == Cut);
	Cut->SetIsLocked(true);
	Spawnable->SetObjectTemplate(nullptr); // State left by the old transient-template save bug.
	Scene->SetReadOnly(true);
	TestFalse(TEXT("Read-only repair does not mutate assets"), CameraInstanceOutput::Ensure(F.Sequencer.ToSharedRef(), *Owner, Error));
	TestNull(TEXT("Read-only repair retains missing template"), Spawnable->GetObjectTemplate());
	Scene->SetReadOnly(false);
	{
		const FScopedTransaction Transaction(NSLOCTEXT("InstanceOutputTest", "Repair", "Repair Missing Output Template"));
		if (!TestTrue(TEXT("Repair existing spawnable"), CameraInstanceOutput::Ensure(F.Sequencer.ToSharedRef(), *Owner, Error))) return false;
	}
	TestEqual(TEXT("Repair creates no new binding"), Scene->GetBindings().Num(), BindingCount);
	TestTrue(TEXT("Activation and parameter lanes keep original binding"), Owner->FindObjectBindingGuid() == Binding.GetGuid() && Parameter->FindObjectBindingGuid() == Binding.GetGuid());
	TestTrue(TEXT("Repair preserves existing locked Cut"), Owner->ManagedCut == Cut && Cut->IsLocked() && Cut->GetCameraBindingID() == Binding);
	auto* Template = Cast<AActor>(Scene->FindSpawnable(Binding.GetGuid())->GetObjectTemplate());
	if (!TestNotNull(TEXT("Template restored in place"), Template)) return false;
	TestFalse(TEXT("Repaired template can be saved"), Template->HasAnyFlags(RF_Transient));
	auto* Component = Template->FindComponentByClass<UComposableCameraLevelSequenceComponent>();
	TestTrue(TEXT("Repaired template belongs to same instance"), Component && Component->bCameraInstanceDriven && Component->CameraInstanceOwner == Owner);
	TestTrue(TEXT("One Undo reverts template repair"), GEditor->UndoTransaction());
	TestNull(TEXT("Undo restores missing template without removing binding"), Scene->FindSpawnable(Binding.GetGuid())->GetObjectTemplate());
	TestTrue(TEXT("Redo restores repaired template"), GEditor->RedoTransaction());
	TestTrue(TEXT("Redo retains original Cut identity"), Owner->ManagedCut == Cut && Scene->FindSpawnable(Binding.GetGuid())->GetObjectTemplate() == Template);
	return true;
}
#endif
