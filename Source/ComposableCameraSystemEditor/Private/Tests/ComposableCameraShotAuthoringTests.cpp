// Copyright 2026 Sulley. All Rights Reserved.
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DataAssets/ComposableCameraShotAsset.h"
#include "Editor.h"
// Compile-time regression: UE's canonical class definition must agree with the session forward declaration.
#include "Misc/TransactionObjectEvent.h"
#include "Editors/ComposableCameraShotAuthoringSession.h"
#include "Editors/ComposableCameraShotTemplates.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "LevelSequence.h"
#include "Misc/AutomationTest.h"
#include "ScopedTransaction.h"
#include "MovieScene.h"
#include "MovieScene/MovieSceneComposableCameraShotSection.h"
#include "MovieScene/MovieSceneComposableCameraShotTrack.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Sections/MovieSceneSpawnSection.h"
#include "Sequencer/ComposableCameraShotAuthoring.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Tracks/MovieSceneSpawnTrack.h"
#include "Serialization/ObjectReader.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

#if WITH_DEV_AUTOMATION_TESTS
using namespace ComposableCameraSystem::ShotAuthoring;
namespace
{
struct FTemplateTestWorld
{
	UWorld* World;
	FTemplateTestWorld()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false);
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
	}
	~FTemplateTestWorld() { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
	AActor* Actor(FVector Location)
	{
		AActor* Result = World->SpawnActor<AActor>();
		USceneComponent* Root = NewObject<USceneComponent>(Result, TEXT("Root"));
		Result->SetRootComponent(Root); Root->RegisterComponent(); Result->SetActorLocation(Location);
		UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Result, TEXT("Body"));
		Mesh->SetupAttachment(Root); Mesh->RegisterComponent();
		Mesh->Bounds = FBoxSphereBounds(Location + FVector(0, 0, 80), FVector(25, 30, 80), 90);
		return Result;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotTemplateSemanticsTest,
	"ComposableCameraSystem.ShotAuthoring.TemplateSemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotTemplateSemanticsTest::RunTest(const FString&)
{
	FTemplateTestWorld Fixture;
	TArray<FComposableCameraShotTarget> Targets;
	Targets.Add(MakeTarget(Fixture.Actor(FVector::ZeroVector)));
	FComposableCameraShot Close, Repeated, Full;
	TestTrue(TEXT("Close-up accepts one subject"), BuildTemplate(EComposableShotTemplate::CloseUp, Targets, Close));
	TestTrue(TEXT("Repeated close-up applies"), BuildTemplate(EComposableShotTemplate::CloseUp, Close.Targets, Repeated));
	TestTrue(TEXT("Repeated template does not walk pivot upward"), Close.Targets[0].Target.Offset.Equals(Repeated.Targets[0].Target.Offset));
	BuildTemplate(EComposableShotTemplate::FullBody, Close.Targets, Full);
	TestTrue(TEXT("Full body recenters after close-up"), Full.Targets[0].Target.Offset.Equals(Targets[0].Target.Offset));
	TestTrue(TEXT("Full body is further away"), Full.Placement.Distance > Close.Placement.Distance);
	TestFalse(TEXT("Shoulder template requires two subjects"), BuildTemplate(EComposableShotTemplate::ShoulderLeft, Targets, Repeated));
	Targets.Add(MakeTarget(Fixture.Actor(FVector(0, 240, 0))));
	FComposableCameraShot Shoulder, Reverse;
	BuildTemplate(EComposableShotTemplate::ShoulderLeft, Targets, Shoulder);
	BuildTemplate(EComposableShotTemplate::ReverseShoulder, Targets, Reverse);
	TestTrue(TEXT("Shoulder uses pair axis"), Shoulder.Placement.BasisFrame == EShotPlacementBasisFrame::TwoTargetAxis);
	TestEqual(TEXT("Shoulder orbits A"), Shoulder.Placement.PlacementAnchor.TargetIndex, 0);
	TestEqual(TEXT("Shoulder aims at B"), Shoulder.Aim.AimAnchor.TargetIndex, 1);
	TestEqual(TEXT("Reverse orbits B"), Reverse.Placement.PlacementAnchor.TargetIndex, 1);
	TestEqual(TEXT("Reverse aims at A"), Reverse.Aim.AimAnchor.TargetIndex, 0);
	Targets.Add(MakeTarget(Fixture.Actor(FVector(300, 80, 0))));
	FComposableCameraShot Group;
	TestTrue(TEXT("Group accepts three subjects"), BuildTemplate(EComposableShotTemplate::Group, Targets, Group));
	TestEqual(TEXT("All subjects participate in centroid"), Group.Aim.AimAnchor.WeightedTargets.Num(), 3);
	TestTrue(TEXT("Group fits bounds"), Group.Lens.FOVMode == EShotFOVMode::SolvedFromBoundsFit);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotSubjectReorderTest,
	"ComposableCameraSystem.ShotAuthoring.SubjectReorder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotSubjectReorderTest::RunTest(const FString&)
{
	TStrongObjectPtr<ULevelSequence> Sequence(NewObject<ULevelSequence>()); Sequence->Initialize();
	auto* Track = NewObject<UMovieSceneComposableCameraShotTrack>(Sequence->GetMovieScene());
	auto* Section = NewObject<UMovieSceneComposableCameraShotSection>(Track);
	FComposableCameraShot& Shot = Section->InlineShot; Shot.Targets.SetNum(3);
	Shot.Placement.PlacementAnchor.TargetIndex = 0;
	Shot.Aim.AimAnchor.TargetIndex = 2;
	Shot.Focus.FocusAnchor.TargetIndex = 1;
	Shot.Placement.BasisActorIndex = 0; Shot.Placement.BasisSecondaryTargetIndex = 2;
	Shot.Aim.AimAnchor.WeightedTargets.AddDefaulted_GetRef().TargetIndex = 1;
	const FGuid ActorA = FGuid::NewGuid(); const FGuid ActorB = FGuid::NewGuid();
	auto& BindingA = Section->TargetActorOverrides.AddDefaulted_GetRef(); BindingA.TargetIndex = 0; BindingA.Binding = UE::MovieScene::FRelativeObjectBindingID(ActorA);
	auto& BindingB = Section->TargetActorOverrides.AddDefaulted_GetRef(); BindingB.TargetIndex = 2; BindingB.Binding = UE::MovieScene::FRelativeObjectBindingID(ActorB);
	FComposableCameraShotAuthoringSession Session; Session.Bind(&Shot, Section);
	TestTrue(TEXT("Move first subject to end"), Session.MoveTarget(0, 2));
	TestEqual(TEXT("Placement retains same subject"), Shot.Placement.PlacementAnchor.TargetIndex, 2);
	TestEqual(TEXT("Aim retains same subject"), Shot.Aim.AimAnchor.TargetIndex, 1);
	TestEqual(TEXT("Custom focus retains same subject"), Shot.Focus.FocusAnchor.TargetIndex, 0);
	TestEqual(TEXT("Stored centroid indices remap even when inactive"), Shot.Aim.AimAnchor.WeightedTargets[0].TargetIndex, 0);
	TestEqual(TEXT("Pair basis start remaps"), Shot.Placement.BasisActorIndex, 2);
	TestEqual(TEXT("Pair basis end remaps"), Shot.Placement.BasisSecondaryTargetIndex, 1);
	TestEqual(TEXT("Binding A follows actor"), Section->TargetActorOverrides[0].TargetIndex, 2);
	TestTrue(TEXT("Binding identity survives reorder"), Section->TargetActorOverrides[0].Binding.GetGuid() == ActorA);
	TestFalse(TEXT("Invalid move leaves data unchanged"), Session.MoveTarget(99, 0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotSubjectAppendTest,
	"ComposableCameraSystem.ShotAuthoring.SubjectAppend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotSubjectAppendTest::RunTest(const FString&)
{
	if (!GEditor || GEditor->IsTransactionActive()) return false;
	FTemplateTestWorld Fixture;
	TStrongObjectPtr<UComposableCameraShotAsset> Asset(NewObject<UComposableCameraShotAsset>(GetTransientPackage(), NAME_None, RF_Transactional));
	AActor* ActorA = Fixture.Actor(FVector::ZeroVector);
	AActor* ActorB = Fixture.Actor(FVector(0, 100, 0));
	Asset->Shot.Targets.Add(MakeTarget(ActorA)); Asset->Shot.Targets.Add(MakeTarget(ActorB));
	Asset->Shot.Targets[0].Target.Offset = FVector(12, 34, 56);
	Asset->Shot.Targets[0].BoundsContributionWeight = .35f;
	Asset->Shot.Placement.Distance = 777.f;
	Asset->Shot.Placement.PlacementAnchor.TargetIndex = 1;
	Asset->Shot.Aim.AimAnchor.TargetIndex = 0;
	Asset->Shot.Focus.FocusAnchor.TargetIndex = 1;
	Asset->Shot.Aim.AimAnchor.WeightedTargets.AddDefaulted_GetRef().TargetIndex = 1;
	Asset->Shot.Placement.BasisActorIndex = 1; Asset->Shot.Placement.BasisSecondaryTargetIndex = 0;
	Asset->Shot.Roll = 17.f;
	FComposableCameraShotAuthoringSession Session; Session.Bind(&Asset->Shot, Asset.Get());
	TArray<AActor*> Added { Fixture.Actor(FVector(200, 0, 0)), Fixture.Actor(FVector(300, 0, 0)) };
	int32 Notifications = 0;
	const auto Handle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda([&](UObject* Object, FPropertyChangedEvent&) { if (Object == Asset.Get()) ++Notifications; });
	FString Reason;
	TestTrue(TEXT("Append multiple actors without a template"), Session.AppendTargets(Added, Reason));
	TestEqual(TEXT("Both actors append to the existing subjects"), Asset->Shot.Targets.Num(), 4);
	TestEqual(TEXT("Batch append posts one host commit"), Notifications, 1);
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(Handle);
	TestTrue(TEXT("First subject keeps its actor"), Session.ResolveTarget(0) == ActorA);
	TestTrue(TEXT("Second subject keeps its actor"), Session.ResolveTarget(1) == ActorB);
	TestTrue(TEXT("New actors are appended in input order"), Session.ResolveTarget(2) == Added[0] && Session.ResolveTarget(3) == Added[1]);
	TestTrue(TEXT("Old pivot offset survives append"), Asset->Shot.Targets[0].Target.Offset.Equals(FVector(12, 34, 56)));
	TestEqual(TEXT("Old fractional bounds weight survives append"), Asset->Shot.Targets[0].BoundsContributionWeight, .35f);
	TestEqual(TEXT("Existing placement composition survives append"), Asset->Shot.Placement.Distance, 777.f);
	TestEqual(TEXT("Existing roll survives append"), Asset->Shot.Roll, 17.f);
	TestEqual(TEXT("Placement anchor index stays stable"), Asset->Shot.Placement.PlacementAnchor.TargetIndex, 1);
	TestEqual(TEXT("Aim anchor index stays stable"), Asset->Shot.Aim.AimAnchor.TargetIndex, 0);
	TestEqual(TEXT("Focus anchor index stays stable"), Asset->Shot.Focus.FocusAnchor.TargetIndex, 1);
	TestEqual(TEXT("Stored centroid membership is not automatically changed"), Asset->Shot.Aim.AimAnchor.WeightedTargets.Num(), 1);
	TestEqual(TEXT("Stored centroid subject index stays stable"), Asset->Shot.Aim.AimAnchor.WeightedTargets[0].TargetIndex, 1);
	TestEqual(TEXT("Basis A index stays stable"), Asset->Shot.Placement.BasisActorIndex, 1);
	TestEqual(TEXT("Basis B index stays stable"), Asset->Shot.Placement.BasisSecondaryTargetIndex, 0);
	TestTrue(TEXT("Whole append can undo once"), GEditor->UndoTransaction());
	TestEqual(TEXT("One undo restores old subject count"), Asset->Shot.Targets.Num(), 2);
	TestTrue(TEXT("Whole append can redo once"), GEditor->RedoTransaction());
	TestEqual(TEXT("One redo restores both appended subjects"), Asset->Shot.Targets.Num(), 4);
	Session.BeginEdit(FText::FromString(TEXT("Active Gesture Guard")));
	TestFalse(TEXT("Append rejects active gestures"), Session.AddTarget(nullptr, Reason));
	TestEqual(TEXT("Rejected append does not change count"), Asset->Shot.Targets.Num(), 4);
	Session.EndEdit();
	{
		FScopedTransaction NativeGesture(FText::FromString(TEXT("Native Gesture Guard")));
		TestFalse(TEXT("Native PropertyEditor transaction disables append"), Session.CanAddTargets());
		TestFalse(TEXT("Append rejects another active editor transaction"), Session.AddTarget(nullptr, Reason));
		TestEqual(TEXT("Native gesture rejection leaves subjects intact"), Asset->Shot.Targets.Num(), 4);
		NativeGesture.Cancel();
	}
	TArray<AActor*> Empty;
	TestFalse(TEXT("Empty batch is rejected"), Session.AppendTargets(Empty, Reason));

	FComposableCameraShotAuthoringSession Draft;
	TestTrue(TEXT("Empty editor can add a subject"), Draft.CanAddTargets());
	if (!TestTrue(TEXT("Adding empty subject creates a draft"), Draft.AddTarget(nullptr, Reason))) return false;
	TestNotNull(TEXT("Draft host is retained"), Draft.GetHost());
	TestEqual(TEXT("Draft starts with one empty slot"), Draft.GetShot()->Targets.Num(), 1);
	TestNull(TEXT("Empty slot waits for actor assignment"), Draft.ResolveTarget(0));
	TestTrue(TEXT("Empty slot addition can undo"), GEditor->UndoTransaction());
	TestEqual(TEXT("Undo returns draft to zero subjects"), Draft.GetShot()->Targets.Num(), 0);
	TestTrue(TEXT("Empty slot addition can redo"), GEditor->RedoTransaction());
	TestEqual(TEXT("Redo restores the empty subject"), Draft.GetShot()->Targets.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotSubjectRemovalTest,
	"ComposableCameraSystem.ShotAuthoring.SubjectRemoval",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotSubjectRemovalTest::RunTest(const FString&)
{
	if (!GEditor || GEditor->IsTransactionActive()) return false;
	TStrongObjectPtr<UComposableCameraShotAsset> Asset(NewObject<UComposableCameraShotAsset>(GetTransientPackage(), NAME_None, RF_Transactional));
	auto& Shot = Asset->Shot;
	Shot.Targets.SetNum(3);
	Shot.Targets[0].BoundsContributionWeight = .2f; Shot.Targets[1].BoundsContributionWeight = .4f; Shot.Targets[2].BoundsContributionWeight = .8f;
	Shot.Placement.PlacementAnchor.TargetIndex = 2;
	Shot.Aim.AimAnchor.TargetIndex = 1; Shot.Focus.FocusAnchor.TargetIndex = 0;
	Shot.Placement.BasisActorIndex = 0; Shot.Placement.BasisSecondaryTargetIndex = 2;
	for (auto* Anchor : { &Shot.Placement.PlacementAnchor, &Shot.Aim.AimAnchor, &Shot.Focus.FocusAnchor })
	{
		Anchor->WeightedTargets.SetNum(3);
		for (int32 Index = 0; Index < 3; ++Index)
		{ Anchor->WeightedTargets[Index].TargetIndex = Index; Anchor->WeightedTargets[Index].Weight = .25f * (Index + 1); }
	}
	FComposableCameraShotAuthoringSession Session; Session.Bind(&Shot, Asset.Get());
	FString Reason;
	TestFalse(TEXT("Invalid removal is refused"), Session.RemoveTarget(99, Reason));
	TestTrue(TEXT("Middle subject can be removed"), Session.RemoveTarget(1, Reason));
	if (!TestEqual(TEXT("Middle removal leaves two subjects"), Shot.Targets.Num(), 2)) return false;
	TestEqual(TEXT("Surviving identity shifts into removed slot"), Shot.Targets[1].BoundsContributionWeight, .8f);
	TestEqual(TEXT("Placement anchor keeps surviving identity"), Shot.Placement.PlacementAnchor.TargetIndex, 1);
	TestEqual(TEXT("Deleted Aim reference becomes unresolved"), Shot.Aim.AimAnchor.TargetIndex, INDEX_NONE);
	TestEqual(TEXT("Earlier Focus reference stays unchanged"), Shot.Focus.FocusAnchor.TargetIndex, 0);
	TestEqual(TEXT("Pair primary identity is retained"), Shot.Placement.BasisActorIndex, 0);
	TestEqual(TEXT("Pair secondary identity shifts"), Shot.Placement.BasisSecondaryTargetIndex, 1);
	for (const auto* Anchor : { &Shot.Placement.PlacementAnchor, &Shot.Aim.AimAnchor, &Shot.Focus.FocusAnchor })
	{
		if (!TestEqual(TEXT("All anchor lists lose deleted membership"), Anchor->WeightedTargets.Num(), 2)) return false;
		TestEqual(TEXT("Weighted surviving identity remaps"), Anchor->WeightedTargets[1].TargetIndex, 1);
		TestEqual(TEXT("Weighted contribution is preserved"), Anchor->WeightedTargets[1].Weight, .75f);
	}
	TestTrue(TEXT("One Undo restores removal"), GEditor->UndoTransaction());
	TestEqual(TEXT("Undo restores complete target list"), Shot.Targets.Num(), 3);
	TestEqual(TEXT("Undo restores deleted anchor reference"), Shot.Aim.AimAnchor.TargetIndex, 1);
	TestEqual(TEXT("Undo restores weighted membership"), Shot.Focus.FocusAnchor.WeightedTargets.Num(), 3);
	TestTrue(TEXT("One Redo reapplies removal"), GEditor->RedoTransaction());
	TestEqual(TEXT("Redo restores target count"), Shot.Targets.Num(), 2);
	{
		FScopedTransaction OtherGesture(FText::FromString(TEXT("Removal guard")));
		TestFalse(TEXT("Other active transaction disables delete"), Session.CanRemoveTarget(0));
		TestFalse(TEXT("Other active transaction refuses delete"), Session.RemoveTarget(0, Reason));
		OtherGesture.Cancel();
	}
	TestTrue(TEXT("First subject can be removed"), Session.RemoveTarget(0, Reason));
	if (!TestEqual(TEXT("First removal leaves one subject"), Shot.Targets.Num(), 1)) return false;
	TestEqual(TEXT("First removal retains final subject"), Shot.Targets[0].BoundsContributionWeight, .8f);
	TestEqual(TEXT("Deleted primary basis is unresolved"), Shot.Placement.BasisActorIndex, INDEX_NONE);
	TestTrue(TEXT("Last subject can be removed"), Session.RemoveTarget(0, Reason));
	TestEqual(TEXT("Last removal leaves empty editable Shot"), Shot.Targets.Num(), 0);
	TestFalse(TEXT("Empty list has no delete action"), Session.CanRemoveTarget(0));
	TestTrue(TEXT("Empty list can add again"), Session.AddTarget(nullptr, Reason));
	TestEqual(TEXT("Re-add creates one empty slot"), Shot.Targets.Num(), 1);

	for (const EComposableCameraShotSource Source : { EComposableCameraShotSource::Inline, EComposableCameraShotSource::AssetReference })
	{
		TStrongObjectPtr<UMovieScene> Scene(NewObject<UMovieScene>());
		TStrongObjectPtr<UMovieSceneComposableCameraShotSection> Section(NewObject<UMovieSceneComposableCameraShotSection>(Scene.Get(), NAME_None, RF_Transactional));
		TStrongObjectPtr<UComposableCameraShotAsset> Preset(NewObject<UComposableCameraShotAsset>());
		Preset->Shot.Targets.SetNum(3); Section->Source = Source;
		if (Source == EComposableCameraShotSource::Inline) Section->InlineShot = Preset->Shot;
		else { Section->ShotAssetRef = Preset.Get(); Section->RefreshShotOverridesFromSource(); }
		FGuid IDs[3] = { FGuid::NewGuid(), FGuid::NewGuid(), FGuid::NewGuid() };
		for (int32 Index = 0; Index < 3; ++Index)
		{
			auto& Override = Section->TargetActorOverrides.AddDefaulted_GetRef();
			Override.TargetIndex = Index; Override.Binding = UE::MovieScene::FRelativeObjectBindingID(IDs[Index]);
		}
		Section->TargetActorOverrides.AddDefaulted_GetRef().TargetIndex = 99;
		FComposableCameraShotAuthoringSession SectionSession; SectionSession.Bind(Section->ResolveShotEditorShot(), Section.Get());
		TestTrue(TEXT("Section removal needs no open Sequencer"), SectionSession.RemoveTarget(1, Reason));
		if (!TestEqual(TEXT("Section keeps surviving overrides only"), Section->TargetActorOverrides.Num(), 2)) return false;
		TestTrue(TEXT("Earlier binding remains"), Section->TargetActorOverrides[0].Binding.GetGuid() == IDs[0]);
		TestTrue(TEXT("Later binding identity shifts with subject"), Section->TargetActorOverrides[1].Binding.GetGuid() == IDs[2] && Section->TargetActorOverrides[1].TargetIndex == 1);
		TestEqual(TEXT("Shared preset stays unchanged"), Preset->Shot.Targets.Num(), 3);
		TestTrue(TEXT("Section removal is one Undo"), GEditor->UndoTransaction());
		TestEqual(TEXT("Section Undo restores subjects"), SectionSession.GetShot()->Targets.Num(), 3);
		TestEqual(TEXT("Section Undo restores all overrides"), Section->TargetActorOverrides.Num(), 4);
		TestTrue(TEXT("Section removal is one Redo"), GEditor->RedoTransaction());
		TestEqual(TEXT("Section Redo reapplies subjects"), SectionSession.GetShot()->Targets.Num(), 2);
		Section->SetIsLocked(true);
		TestFalse(TEXT("Locked Section refuses delete"), SectionSession.RemoveTarget(0, Reason));
		Section->SetIsLocked(false); Scene->SetReadOnly(true);
		TestFalse(TEXT("Read-only sequence refuses delete"), SectionSession.RemoveTarget(0, Reason));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotSectionSubjectAppendTest,
	"ComposableCameraSystem.ShotAuthoring.SectionSubjectAppend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotSectionSubjectAppendTest::RunTest(const FString&)
{
	if (!GEditor || GEditor->IsTransactionActive()) return false;
	TStrongObjectPtr<ULevelSequence> Sequence(NewObject<ULevelSequence>()); Sequence->Initialize();
	auto* Track = NewObject<UMovieSceneComposableCameraShotTrack>(Sequence->GetMovieScene());
	auto* Section = NewObject<UMovieSceneComposableCameraShotSection>(Track, NAME_None, RF_Transactional);
	TStrongObjectPtr<UComposableCameraShotAsset> Preset(NewObject<UComposableCameraShotAsset>());
	Preset->Shot.Targets.SetNum(2); Preset->Shot.Placement.Distance = 432.f;
	Section->Source = EComposableCameraShotSource::AssetReference;
	Section->ShotAssetRef = Preset.Get(); Section->RefreshShotOverridesFromSource();
	const FGuid Original = FGuid::NewGuid(); const FGuid Stale = FGuid::NewGuid();
	auto& Existing = Section->TargetActorOverrides.AddDefaulted_GetRef(); Existing.TargetIndex = 0; Existing.Binding = UE::MovieScene::FRelativeObjectBindingID(Original);
	auto& OutOfRange = Section->TargetActorOverrides.AddDefaulted_GetRef(); OutOfRange.TargetIndex = 2; OutOfRange.Binding = UE::MovieScene::FRelativeObjectBindingID(Stale);
	FComposableCameraShotAuthoringSession Session; Session.Bind(&Section->ShotOverrides, Section);
	FString Reason;
	if (!TestTrue(TEXT("Section can append an empty slot without open Sequencer"), Session.AddTarget(nullptr, Reason))) return false;
	TestEqual(TEXT("Section local target count increases"), Session.GetShot()->Targets.Num(), 3);
	TestEqual(TEXT("Shared preset is not edited"), Preset->Shot.Targets.Num(), 2);
	TestEqual(TEXT("Existing local composition is kept"), Session.GetShot()->Placement.Distance, 432.f);
	TestEqual(TEXT("Stale future-slot override is removed"), Section->TargetActorOverrides.Num(), 1);
	TestTrue(TEXT("Existing binding identity is kept"), Section->TargetActorOverrides[0].Binding.GetGuid() == Original);
	TestEqual(TEXT("Existing binding target index is kept"), Section->TargetActorOverrides[0].TargetIndex, 0);
	TestNull(TEXT("New empty slot does not inherit stale binding"), Session.ResolveTarget(2));
	TestTrue(TEXT("Section append can undo once"), GEditor->UndoTransaction());
	TestEqual(TEXT("Undo restores local count"), Session.GetShot()->Targets.Num(), 2);
	TestEqual(TEXT("Undo restores complete binding list"), Section->TargetActorOverrides.Num(), 2);
	TestTrue(TEXT("Section append can redo once"), GEditor->RedoTransaction());
	TestEqual(TEXT("Redo restores local added slot"), Session.GetShot()->Targets.Num(), 3);
	Sequence->GetMovieScene()->SetReadOnly(true);
	TestFalse(TEXT("Read-only sequence rejects append"), Session.AddTarget(nullptr, Reason));
	Sequence->GetMovieScene()->SetReadOnly(false); Section->SetIsLocked(true);
	TestFalse(TEXT("Locked section rejects append"), Session.AddTarget(nullptr, Reason));
	Section->SetIsLocked(false);
	FTemplateTestWorld Fixture;
	TestFalse(TEXT("Actor append needs the owning Sequencer focused"), Session.AddTarget(Fixture.Actor(FVector::ZeroVector), Reason));
	TestEqual(TEXT("Binding preflight failure leaves target count intact"), Session.GetShot()->Targets.Num(), 3);
	TestEqual(TEXT("Binding preflight failure leaves old binding intact"), Section->TargetActorOverrides.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotInteractiveSourceTest,
	"ComposableCameraSystem.ShotAuthoring.InteractiveSourceAndUndo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotInteractiveSourceTest::RunTest(const FString&)
{
	if (!GEditor || GEditor->IsTransactionActive()) return false;
	TStrongObjectPtr<UComposableCameraShotAsset> Asset(NewObject<UComposableCameraShotAsset>(GetTransientPackage(), NAME_None, RF_Transactional));
	FComposableCameraShotAuthoringSession Session; Session.Bind(&Asset->Shot, Asset.Get());
	const float Before = Asset->Shot.Placement.Distance;
	int32 Notifications = 0;
	const auto Handle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda([&](UObject* Object, FPropertyChangedEvent&) { if (Object == Asset.Get()) ++Notifications; });
	Session.BeginEdit(FText::FromString(TEXT("Test Live Shot Edit")));
	Asset->Shot.Placement.Distance = Before + 100.f; Session.Changed();
	TestEqual(TEXT("Authoring reads live value before release"), Session.GetShot()->Placement.Distance, Before + 100.f);
	TestEqual(TEXT("No property broadcast/Sequencer rebuild during drag"), Notifications, 0);
	Asset->Shot.Placement.Distance = Before + 200.f; Session.Changed(); Session.EndEdit();
	TestEqual(TEXT("One host notification at commit"), Notifications, 1);
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(Handle);
	TestTrue(TEXT("Single gesture can undo"), GEditor->UndoTransaction());
	TestEqual(TEXT("Undo restores pre-drag value"), Asset->Shot.Placement.Distance, Before);
	TestTrue(TEXT("Single gesture can redo"), GEditor->RedoTransaction());
	TestEqual(TEXT("Redo restores final drag value"), Asset->Shot.Placement.Distance, Before + 200.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotLocalPresetTest,
	"ComposableCameraSystem.ShotAuthoring.SectionLocalPreset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotLocalPresetTest::RunTest(const FString&)
{
	FTemplateTestWorld Fixture;
	TStrongObjectPtr<ULevelSequence> Sequence(NewObject<ULevelSequence>()); Sequence->Initialize();
	auto* Track = NewObject<UMovieSceneComposableCameraShotTrack>(Sequence->GetMovieScene());
	auto* Section = NewObject<UMovieSceneComposableCameraShotSection>(Track);
	TStrongObjectPtr<UComposableCameraShotAsset> Preset(NewObject<UComposableCameraShotAsset>());
	Preset->Shot.Targets.SetNum(2); Preset->Shot.Placement.Distance = 123.f;
	Preset->Shot.Targets[0].Target.Actor = Fixture.Actor(FVector::ZeroVector);
	Section->Source = EComposableCameraShotSource::AssetReference; Section->ShotAssetRef = Preset.Get(); Section->RefreshShotOverridesFromSource();
	Section->ShotOverrides.Targets[0].Target.BoneName = TEXT("head");
	const FGuid Subject = FGuid::NewGuid(); auto& Binding = Section->TargetActorOverrides.AddDefaulted_GetRef(); Binding.Binding = UE::MovieScene::FRelativeObjectBindingID(Subject);
	FComposableCameraShotAuthoringSession Session; Session.Bind(&Section->ShotOverrides, Section);
	TestNull(TEXT("Missing explicit sequence binding never previews the preset's actor"), Session.ResolveTarget(0));
	Session.BeginEdit(FText::FromString(TEXT("Local Edit"))); Session.GetShot()->Placement.Distance = 456.f; Session.Changed(); Session.EndEdit();
	TestEqual(TEXT("Section edit leaves shared preset unchanged"), Preset->Shot.Placement.Distance, 123.f);
	FString Reason; TestTrue(TEXT("Restore matching preset"), Session.ApplyPreset(Preset.Get(), Reason));
	TestEqual(TEXT("Restore composition"), Section->ShotOverrides.Placement.Distance, 123.f);
	TestTrue(TEXT("Restore keeps local bone selection"), Section->ShotOverrides.Targets[0].Target.BoneName == FName(TEXT("head")));
	TestTrue(TEXT("Restore keeps local binding"), Section->TargetActorOverrides[0].Binding.GetGuid() == Subject);
	Sequence->GetMovieScene()->SetReadOnly(true);
	TestFalse(TEXT("Read-only sequence disables editing"), Session.CanEdit());
	TestFalse(TEXT("Read-only source rejects preset"), Session.ApplyPreset(Preset.Get(), Reason));
	Sequence->GetMovieScene()->SetReadOnly(false); Section->Source = EComposableCameraShotSource::Inline;
	TestTrue(TEXT("Source adapter re-resolves Inline instead of old override pointer"), Session.GetShot() == &Section->InlineShot);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotSequenceRangeTest,
	"ComposableCameraSystem.ShotAuthoring.PreserveCameraCuts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotSequenceRangeTest::RunTest(const FString&)
{
	TStrongObjectPtr<ULevelSequence> Sequence(NewObject<ULevelSequence>()); Sequence->Initialize(); UMovieScene* Scene = Sequence->GetMovieScene();
	const FGuid CameraA = Scene->AddPossessable(TEXT("Camera A"), AActor::StaticClass());
	const FGuid CameraB = Scene->AddPossessable(TEXT("Camera B"), AActor::StaticClass());
	FString Reason; const TRange<FFrameNumber> Requested(FFrameNumber(10), FFrameNumber(20));
	TestTrue(TEXT("Empty range of timeline accepts a new camera"), CanCreateRange(*Scene, FGuid(), Requested, Reason));
	auto* Cuts = Cast<UMovieSceneCameraCutTrack>(Scene->AddCameraCutTrack(UMovieSceneCameraCutTrack::StaticClass()));
	auto* Cut = Cast<UMovieSceneCameraCutSection>(Cuts->CreateNewSection()); Cut->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(100)));
	Cut->SetCameraBindingID(UE::MovieScene::FRelativeObjectBindingID(CameraA)); Cuts->AddSection(*Cut);
	TestFalse(TEXT("New camera cannot overwrite an existing cut"), CanCreateRange(*Scene, FGuid(), Requested, Reason));
	TestFalse(TEXT("Other camera cannot overwrite an existing cut"), CanCreateRange(*Scene, CameraB, Requested, Reason));
	TestTrue(TEXT("Current camera can add shot inside a covering cut"), CanCreateRange(*Scene, CameraA, Requested, Reason));
	TestFalse(TEXT("Partial overlap cannot resize existing cut"), CanCreateRange(*Scene, CameraA, TRange<FFrameNumber>(FFrameNumber(90), FFrameNumber(110)), Reason));
	auto* Track = Scene->AddTrack<UMovieSceneComposableCameraShotTrack>(CameraA);
	auto* Shot = Track->CreateNewSection(); Shot->SetRange(Requested); Track->AddSection(*Shot);
	TestFalse(TEXT("Cannot overwrite another Shot on same camera"), CanCreateRange(*Scene, CameraA, Requested, Reason));
	TestTrue(TEXT("Adjacent range can append a shot"), CanCreateRange(*Scene, CameraA, TRange<FFrameNumber>(FFrameNumber(20), FFrameNumber(30)), Reason));
	Scene->SetReadOnly(true);
	TestFalse(TEXT("Read-only sequence refuses creation"), CanCreateRange(*Scene, CameraA, TRange<FFrameNumber>(FFrameNumber(20), FFrameNumber(30)), Reason));
	TestEqual(TEXT("Checks preserve existing Cut count"), Cuts->GetAllSections().Num(), 1);
	TestEqual(TEXT("Checks preserve existing Cut range"), Cut->GetExclusiveEndFrame().Value, 100);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotSpawnRangeTest,
	"ComposableCameraSystem.ShotAuthoring.SpawnableAppend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotSpawnRangeTest::RunTest(const FString&)
{
	FTemplateTestWorld Fixture;
	TStrongObjectPtr<ULevelSequence> Sequence(NewObject<ULevelSequence>()); Sequence->Initialize();
	UMovieScene* Scene = Sequence->GetMovieScene();
	const FGuid Camera = Scene->AddSpawnable(TEXT("Camera"), *Fixture.Actor(FVector::ZeroVector));
	auto* Track = Scene->AddTrack<UMovieSceneSpawnTrack>(Camera); Track->SetObjectId(Camera);
	auto* Original = Cast<UMovieSceneSpawnSection>(Track->CreateNewSection());
	Original->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(100)));
	Original->GetChannel().SetDefault(true); Original->GetChannel().GetData().UpdateOrAddKey(FFrameNumber(50), false); Track->AddSection(*Original);
	const TRange<FFrameNumber> Appended(FFrameNumber(50), FFrameNumber(150));
	TestTrue(TEXT("Append guarantees camera coverage"), EnsureCameraSpawnRange(*Scene, Camera, Appended));
	TestEqual(TEXT("Original spawn keys remain"), Original->GetChannel().GetTimes().Num(), 1);
	TestEqual(TEXT("Original spawn range remains"), Original->GetExclusiveEndFrame().Value, 100);
	bool OriginalValue = true; Original->GetChannel().Evaluate(FFrameNumber(60), OriginalValue);
	TestFalse(TEXT("Original false key was not overwritten"), OriginalValue);
	TestEqual(TEXT("Adds one scoped section"), Track->GetAllSections().Num(), 2);
	auto* New = Cast<UMovieSceneSpawnSection>(Track->GetAllSections()[1]);
	bool Value = false; New->GetChannel().Evaluate(FFrameNumber(60), Value);
	TestTrue(TEXT("New camera interval explicitly spawns"), Value);
	TestTrue(TEXT("New interval wins overlapping old false value"), New->GetOverlapPriority() > Original->GetOverlapPriority());
	TestEqual(TEXT("New interval starts at new shot"), New->GetInclusiveStartFrame().Value, 50);
	TestEqual(TEXT("New interval ends at new shot"), New->GetExclusiveEndFrame().Value, 150);
	Scene->SetReadOnly(true);
	TestFalse(TEXT("Read-only camera coverage cannot change"), EnsureCameraSpawnRange(*Scene, Camera, Appended));
	TestEqual(TEXT("Read-only refusal leaves section count unchanged"), Track->GetAllSections().Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotAuthoringRoundTripTest,
	"ComposableCameraSystem.ShotAuthoring.AuthoringRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotAuthoringRoundTripTest::RunTest(const FString&)
{
	TStrongObjectPtr<UComposableCameraShotAsset> Source(NewObject<UComposableCameraShotAsset>());
	Source->Shot.Targets.SetNum(2); Source->Shot.Placement.BasisFrame = EShotPlacementBasisFrame::TwoTargetAxis;
	Source->Shot.Placement.BasisActorIndex = 1; Source->Shot.Placement.BasisSecondaryTargetIndex = 0;
	Source->Shot.Targets[0].Target.ComponentName = TEXT("FaceMesh");
#if WITH_EDITORONLY_DATA
	const FTransform Relative(FRotator(0, -90, 0), FVector(0, 0, -88));
	Source->Shot.Targets[0].Target.EditorPreviewMeshRelativeTransform = Relative;
#endif
	TArray<uint8> Bytes; FObjectWriter(Source.Get(), Bytes);
	TStrongObjectPtr<UComposableCameraShotAsset> Loaded(NewObject<UComposableCameraShotAsset>());
	FObjectReader(Loaded.Get(), Bytes);
	TestTrue(TEXT("Pair basis survives property serialization"), Loaded->Shot.Placement.BasisFrame == EShotPlacementBasisFrame::TwoTargetAxis);
	TestEqual(TEXT("Second basis slot survives"), Loaded->Shot.Placement.BasisSecondaryTargetIndex, 0);
	TestTrue(TEXT("Named component survives"), Loaded->Shot.Targets[0].Target.ComponentName == FName(TEXT("FaceMesh")));
#if WITH_EDITORONLY_DATA
	TestTrue(TEXT("Character mesh transform survives preset round-trip"), Loaded->Shot.Targets[0].Target.EditorPreviewMeshRelativeTransform.Equals(Relative));
#endif
	return true;
}
#endif
