// Copyright 2026 Sulley. All Rights Reserved.
#include "Editors/ComposableCameraShotEditorHistory.h"
#include "DataAssets/ComposableCameraShotAsset.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "EdGraph/EdGraph.h"
#include "Editors/ComposableCameraNodeGraphNode.h"
#include "GameFramework/Actor.h"
#include "LevelSequence.h"
#include "Misc/AutomationTest.h"
#include "MovieScene.h"
#include "MovieScene/ComposableCameraInstanceData.h"
#include "MovieScene/MovieSceneComposableCameraInstanceTrack.h"
#include "MovieScene/MovieSceneComposableCameraShotSection.h"
#include "MovieScene/MovieSceneComposableCameraShotTrack.h"
#include "Nodes/ComposableCameraCompositionFramingNode.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotEditorHistoryValidityTest,
	"ComposableCameraSystem.ShotEditor.RecentHistoryValidity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShotEditorHistoryValidityTest::RunTest(const FString&)
{
	// No singleton, config changes, disk assets, editor launch or global GC.
	FShotEditorHistory History(false);
	const FString PackageName = TEXT("/Game/CCSRecentHistoryTest_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	TStrongObjectPtr<UPackage> Package(CreatePackage(*PackageName));
	TStrongObjectPtr<UComposableCameraShotAsset> Asset(
		NewObject<UComposableCameraShotAsset>(Package.Get(), TEXT("Shot"), RF_Public));
	TStrongObjectPtr<ULevelSequence> Sequence(NewObject<ULevelSequence>(Package.Get(), TEXT("Sequence"), RF_Public));
	Sequence->Initialize();
	auto* Scene = Sequence->GetMovieScene();
	const FGuid Binding = Scene->AddPossessable(TEXT("History Shot"), AActor::StaticClass());
	auto* Track = Scene->AddTrack<UMovieSceneComposableCameraInstanceTrack>(Binding);
	if (!TestNotNull(TEXT("Instance track created"), Track)) return false;
	History.Push(Asset.Get(), FText::FromString(TEXT("Asset")));
	History.Push(Track->Instance, FText::FromString(TEXT("Instance")));
	History.Push(Asset.Get(), FText::FromString(TEXT("Asset reopened")));
	if (!TestEqual(TEXT("Reopening deduplicates"), History.GetEntries().Num(), 2)) return false;
	TestTrue(TEXT("Most recent first"), History.GetEntries()[0].Host.Get() == Asset.Get());
	const FShotEditorHistoryEntry InstanceSnapshot = History.GetEntries()[1];

	// Deletion leaves a valid UObject for Undo, but must immediately disable a stale menu action.
	TestTrue(TEXT("Remove instance track"), Scene->RemoveTrack(*Track));
	TestTrue(TEXT("Removed instance is still alive"), IsValid(Track->Instance));
	TestFalse(TEXT("Detached instance unavailable"), FShotEditorHistory::IsHostAvailable(Track->Instance));
	TestNull(TEXT("Stale menu snapshot cannot reopen it"), FShotEditorHistory::ResolveHost(InstanceSnapshot));
	History.PruneUnavailable();
	if (!TestEqual(TEXT("Detached entry pruned"), History.GetEntries().Num(), 1)) return false;
	TestTrue(TEXT("Surviving order preserved"), History.GetEntries()[0].Host.Get() == Asset.Get());

	// A syntactically valid path is not evidence of an existing asset.
	FShotEditorHistoryEntry Missing;
	Missing.HostPath = FSoftObjectPath(PackageName + TEXT("_Missing.Shot"));
	Missing.DisplayLabel = FText::FromString(TEXT("Deleted asset"));
	TestTrue(TEXT("Deleted asset path is syntactically valid"), Missing.HostPath.IsValid());
	History.Entries.Insert(Missing, 0);
	History.Entries.Last().Host.Reset(); // Simulate a restored ini entry with only a path.
	History.PruneUnavailable();
	if (!TestEqual(TEXT("Missing package removed, existing unsaved asset retained"), History.GetEntries().Num(), 1)) return false;
	TestTrue(TEXT("Cold weak reference resolved"), History.GetEntries()[0].Host.Get() == Asset.Get());

	TestTrue(TEXT("Rename asset"), Asset->Rename(TEXT("RenamedShot"), nullptr, REN_DontCreateRedirectors | REN_NonTransactional));
	History.PruneUnavailable();
	if (!TestEqual(TEXT("Renamed asset retained"), History.GetEntries().Num(), 1)) return false;
	TestTrue(TEXT("Live rename refreshes persisted path"), History.GetEntries()[0].HostPath == FSoftObjectPath(Asset.Get()));
	Asset->ClearFlags(RF_Public | RF_Standalone);
	TestTrue(TEXT("Deleted asset remains alive"), IsValid(Asset.Get()));
	History.PruneUnavailable();
	TestTrue(TEXT("Asset with deleted flags removed"), History.GetEntries().IsEmpty());

	TStrongObjectPtr<UComposableCameraShotAsset> Draft(NewObject<UComposableCameraShotAsset>(GetTransientPackage(), NAME_None, RF_Transient));
	History.Push(Draft.Get(), FText::FromString(TEXT("Scratch draft")));
	TestTrue(TEXT("Scratch drafts do not enter Recent"), History.GetEntries().IsEmpty());
	Missing.HostPath = FSoftObjectPath(GetTransientPackage()->GetName() + TEXT(".ExpiredShotDraft"));
	History.Entries.Add(Missing);
	History.PruneUnavailable();
	TestTrue(TEXT("Old expired draft paths removed"), History.GetEntries().IsEmpty());

	// Replacing instance data or deleting the whole binding must invalidate the old host too.
	auto* ReplacementTrack = Scene->AddTrack<UMovieSceneComposableCameraInstanceTrack>(Binding);
	if (!ReplacementTrack) return false;
	History.Push(ReplacementTrack->Instance, FText::FromString(TEXT("Old instance data")));
	ReplacementTrack->Instance = NewObject<UComposableCameraInstanceData>(ReplacementTrack);
	History.PruneUnavailable();
	TestTrue(TEXT("Replaced instance data removed"), History.GetEntries().IsEmpty());
	History.Push(ReplacementTrack->Instance, FText::FromString(TEXT("Current instance data")));
	TestTrue(TEXT("Remove binding"), Scene->RemovePossessable(Binding));
	History.PruneUnavailable();
	TestTrue(TEXT("Deleted binding removes instance entry"), History.GetEntries().IsEmpty());

	// Legacy sections need both section membership and track membership.
	const FGuid LegacyBinding = Scene->AddPossessable(TEXT("Legacy"), AActor::StaticClass());
	auto* LegacyTrack = Scene->AddTrack<UMovieSceneComposableCameraShotTrack>(LegacyBinding);
	if (!LegacyTrack) return false;
	auto* Section = LegacyTrack->CreateNewSection();
	LegacyTrack->AddSection(*Section);
	History.Push(Section, FText::FromString(TEXT("Legacy section")));
	TestEqual(TEXT("Valid legacy section included"), History.GetEntries().Num(), 1);
	LegacyTrack->RemoveSection(*Section);
	History.PruneUnavailable();
	TestTrue(TEXT("Removed section pruned before GC"), History.GetEntries().IsEmpty());
	LegacyTrack->AddSection(*Section); // Equivalent ownership restoration after Undo.
	TestTrue(TEXT("Restored section can be reopened"), FShotEditorHistory::IsHostAvailable(Section));
	History.Push(Section, FText::FromString(TEXT("Restored section")));
	Scene->RemoveTrack(*LegacyTrack);
	History.PruneUnavailable();
	TestTrue(TEXT("Removed legacy track prunes its section"), History.GetEntries().IsEmpty());

	TStrongObjectPtr<UComposableCameraTypeAsset> Type(NewObject<UComposableCameraTypeAsset>(Package.Get(), TEXT("Type"), RF_Public));
	auto* Node = NewObject<UComposableCameraCompositionFramingNode>(Type.Get());
	Type->NodeTemplates.Add(Node);
	History.Push(Node, FText::FromString(TEXT("Composition node")));
	TestEqual(TEXT("Saved template node included"), History.GetEntries().Num(), 1);
	Type->EditorGraph = NewObject<UEdGraph>(Type.Get());
	auto* GraphNode = NewObject<UComposableCameraNodeGraphNode>(Type->EditorGraph);
	GraphNode->NodeTemplate = Node;
	Type->EditorGraph->AddNode(GraphNode);
	History.PruneUnavailable();
	TestEqual(TEXT("Live graph node retained"), History.GetEntries().Num(), 1);
	Type->EditorGraph->RemoveNode(GraphNode);
	History.PruneUnavailable();
	TestTrue(TEXT("Graph deletion overrides stale runtime template membership"), History.GetEntries().IsEmpty());
	History.Clear();
	History.PruneUnavailable();
	TestTrue(TEXT("Empty history remains empty"), History.GetEntries().IsEmpty());
	return true;
}
#endif
