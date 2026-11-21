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
#include "Widgets/ComposableCameraShotRecentMenu.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotEditorRecentPresentationTest,
	"ComposableCameraSystem.ShotEditor.RecentPresentation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShotEditorRecentPresentationTest::RunTest(const FString&)
{
	using namespace ComposableCameraSystem::ShotRecentMenu;
	// Unsaved fixtures only: no singleton, settings writes, asset loading or menu actions.
	const FString Root = TEXT("/Game/CCSRecentPresentation_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	TStrongObjectPtr<UPackage> PackageA(CreatePackage(*(Root + TEXT("/A/Shot"))));
	TStrongObjectPtr<UPackage> PackageB(CreatePackage(*(Root + TEXT("/B/Shot"))));
	TStrongObjectPtr<UComposableCameraShotAsset> AssetA(NewObject<UComposableCameraShotAsset>(PackageA.Get(), TEXT("SharedShot"), RF_Public));
	TStrongObjectPtr<UComposableCameraShotAsset> AssetB(NewObject<UComposableCameraShotAsset>(PackageB.Get(), TEXT("SharedShot"), RF_Public));
	const FText StaleLabel = FText::FromString(TEXT("Old saved breadcrumb"));
	const auto AssetInfo = Describe(AssetA.Get(), StaleLabel);
	TestEqual(TEXT("Asset name is separate from the breadcrumb"), AssetInfo.Title.ToString(), FString(TEXT("SharedShot")));
	TestFalse(TEXT("Same-name assets have distinct folder context"), AssetInfo.Context.EqualTo(Describe(AssetB.Get(), StaleLabel).Context));
	TestTrue(TEXT("Tooltip preserves the full source path"), AssetInfo.ToolTip.ToString().Contains(AssetA->GetPathName()));
	TestTrue(TEXT("Rename source asset"), AssetA->Rename(TEXT("RenamedShot"), nullptr, REN_DontCreateRedirectors | REN_NonTransactional));
	TestEqual(TEXT("Asset rename replaces stale persisted caption"), Describe(AssetA.Get(), StaleLabel).Title.ToString(), FString(TEXT("RenamedShot")));

	TStrongObjectPtr<ULevelSequence> Sequence(NewObject<ULevelSequence>(PackageA.Get(), TEXT("LS_Opening"), RF_Public));
	Sequence->Initialize();
	auto* Scene = Sequence->GetMovieScene();
	Scene->SetTickResolutionDirectly(FFrameRate(60, 1));
	const FGuid Binding = Scene->AddPossessable(TEXT("Opening Shot"), AActor::StaticClass());
	auto* InstanceTrack = Scene->AddTrack<UMovieSceneComposableCameraInstanceTrack>(Binding);
	if (!TestNotNull(TEXT("Instance track created"), InstanceTrack)) return false;
	const FGuid BeforeInstanceDescription = Scene->GetSignature();
	const auto InstanceInfo = Describe(InstanceTrack->Instance, StaleLabel);
	TestTrue(TEXT("Instance label lookup does not edit the Scene"), Scene->GetSignature() == BeforeInstanceDescription);
	TestEqual(TEXT("Instance title uses the native binding name"), InstanceInfo.Title.ToString(), FString(TEXT("Opening Shot")));
	TestTrue(TEXT("Instance context identifies the Sequence"), InstanceInfo.Context.ToString().Contains(TEXT("LS_Opening")));
	TestFalse(TEXT("Instance is distinguished from a standalone asset"), InstanceInfo.Kind.EqualTo(AssetInfo.Kind));
	const FString LongName = FString::ChrN(100, TEXT('X')) + TEXT(" Renamed Binding");
	Scene->SetObjectDisplayName(Binding, FText::FromString(LongName));
	const auto RenamedInfo = Describe(InstanceTrack->Instance, StaleLabel);
	TestEqual(TEXT("Full renamed title is retained for Slate ellipsis"), RenamedInfo.Title.ToString(), LongName);
	TestTrue(TEXT("Long title remains complete in tooltip"), RenamedInfo.ToolTip.ToString().Contains(LongName));

	auto* ShotTrack = Scene->AddTrack<UMovieSceneComposableCameraShotTrack>(Binding);
	if (!TestNotNull(TEXT("Shot track created"), ShotTrack)) return false;
	auto* Section = CastChecked<UMovieSceneComposableCameraShotSection>(ShotTrack->CreateNewSection());
	ShotTrack->AddSection(*Section);
	Section->Source = EComposableCameraShotSource::Inline;
	Section->ShotLabel = TEXT("Close up");
	Section->SetRange(TRange<FFrameNumber>(120, 360));
	Section->SetRowIndex(3);
	const FGuid BeforeSectionDescription = Scene->GetSignature();
	const auto SectionInfo = Describe(Section, StaleLabel);
	TestTrue(TEXT("Section binding label lookup does not edit the Scene"), Scene->GetSignature() == BeforeSectionDescription);
	TestEqual(TEXT("Section title honors the live authored label"), SectionInfo.Title.ToString(), FString(TEXT("Close up")));
	TestFalse(TEXT("Section differs from instance even in the same binding"), SectionInfo.Kind.EqualTo(InstanceInfo.Kind));
	TestTrue(TEXT("Section context includes its binding"), SectionInfo.Context.ToString().Contains(LongName));
	FNumberFormattingOptions Seconds;
	Seconds.SetMinimumFractionalDigits(2).SetMaximumFractionalDigits(2).SetUseGrouping(false);
	TestTrue(TEXT("Start is seconds at Scene tick resolution"), SectionInfo.Detail.ToString().Contains(FText::AsNumber(2.0, &Seconds).ToString()));
	TestTrue(TEXT("End is seconds at Scene tick resolution"), SectionInfo.Detail.ToString().Contains(FText::AsNumber(6.0, &Seconds).ToString()));
	Section->SetRowIndex(4);
	TestFalse(TEXT("Different rows disambiguate equal names and ranges"), SectionInfo.Detail.EqualTo(Describe(Section, StaleLabel).Detail));
	Section->SetRange(TRange<FFrameNumber>(TRangeBound<FFrameNumber>::Inclusive(120), TRangeBound<FFrameNumber>::Open()));
	const auto OpenInfo = Describe(Section, StaleLabel);
	TestTrue(TEXT("Open end keeps the finite start"), OpenInfo.Detail.ToString().Contains(FText::AsNumber(2.0, &Seconds).ToString()));
	TestFalse(TEXT("Open end does not retain the previous finite end"), OpenInfo.Detail.ToString().Contains(FText::AsNumber(6.0, &Seconds).ToString()));
	Section->ShotLabel.Empty();
	Section->Source = EComposableCameraShotSource::AssetReference;
	Section->ShotAssetRef = AssetB.Get();
	TestEqual(TEXT("Unlabelled reference uses its asset name"), Describe(Section, StaleLabel).Title.ToString(), FString(TEXT("SharedShot")));
	const auto ReferenceInfo = Describe(Section, StaleLabel);
	TestFalse(TEXT("Source mode is explicit in section details"), ReferenceInfo.Detail.EqualTo(OpenInfo.Detail));
	Section->ShotLabel = TEXT("Renamed section");
	const auto LabelledReference = Describe(Section, StaleLabel);
	TestEqual(TEXT("Section rename replaces the old caption"), LabelledReference.Title.ToString(), FString(TEXT("Renamed section")));
	TestTrue(TEXT("Authored title does not hide its source asset"), LabelledReference.Detail.ToString().Contains(TEXT("SharedShot")));
	Section->ShotAssetRef = TSoftObjectPtr<UComposableCameraShotAsset>(FSoftObjectPath(Root + TEXT("/Unloaded.UnloadedShot")));
	TestTrue(TEXT("Unloaded reference is described by path"), Describe(Section, StaleLabel).Detail.ToString().Contains(TEXT("UnloadedShot")));
	TestFalse(TEXT("Description never loads the referenced asset"), Section->ShotAssetRef.IsValid());
	Section->ShotLabel.Empty();
	Section->ShotAssetRef.Reset();
	TestFalse(TEXT("Missing reference has an explanatory title"), Describe(Section, StaleLabel).Title.IsEmpty());
	Section->Source = EComposableCameraShotSource::Inline;
	TestFalse(TEXT("Unlabelled inline section has a title"), Describe(Section, StaleLabel).Title.IsEmpty());

	TStrongObjectPtr<UComposableCameraTypeAsset> Type(NewObject<UComposableCameraTypeAsset>(PackageA.Get(), TEXT("CT_Composition"), RF_Public));
	auto* Node = NewObject<UComposableCameraCompositionFramingNode>(Type.Get(), TEXT("FramingA"));
	const auto NodeInfo = Describe(Node, StaleLabel);
	TestFalse(TEXT("Node has a readable title"), NodeInfo.Title.IsEmpty());
	TestFalse(TEXT("Node kind differs from asset"), NodeInfo.Kind.EqualTo(AssetInfo.Kind));
	TestTrue(TEXT("Node context identifies owning type"), NodeInfo.Context.ToString().Contains(TEXT("CT_Composition")));
	TestTrue(TEXT("Node identity disambiguates siblings"), NodeInfo.Context.ToString().Contains(TEXT("FramingA")));
	TestTrue(TEXT("Unavailable host safely retains the fallback"), Describe(nullptr, StaleLabel).Title.EqualTo(StaleLabel));
	return true;
}
#endif
