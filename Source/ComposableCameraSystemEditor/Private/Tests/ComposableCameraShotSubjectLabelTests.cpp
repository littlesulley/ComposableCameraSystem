// Copyright 2026 Sulley. All Rights Reserved.
#include "Customizations/ComposableCameraShotTargetIndexCombo.h"
#include "DataAssets/ComposableCameraShotAsset.h"
#include "Editors/ComposableCameraShotAuthoringSession.h"
#include "Editors/ComposableCameraInstanceEditorSession.h"
#include "Editors/ComposableCameraShotEditorDiagnostics.h"
#include "Editors/ComposableCameraShotSubjectLabel.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameFramework/Actor.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "LevelSequence.h"
#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"
#include "MovieScene.h"
#include "MovieScene/MovieSceneComposableCameraShotSection.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "Widgets/SShotEditorParameterPanel.h"
#include "Widgets/SShotEditorAuthoringPanel.h"
#include "Widgets/Text/STextBlock.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
bool ContainsSubjectLabel(const TSharedRef<SWidget>& Widget, const FString& Text)
{
	if (Widget->GetType() == FName(TEXT("STextBlock"))
		&& StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString().Contains(Text)) return true;
	FChildren* Children = Widget->GetChildren();
	for (int32 Index = 0; Index < Children->Num(); ++Index)
		if (ContainsSubjectLabel(Children->GetChildAt(Index), Text)) return true;
	return false;
}

TSharedPtr<IPropertyHandle> FindSubjectLabelHandle(const TArray<TSharedRef<IDetailTreeNode>>& Nodes, FName Name)
{
	for (const auto& Node : Nodes)
	{
		const auto Handle = Node->CreatePropertyHandle();
		if (Handle && Handle->GetProperty() && Handle->GetProperty()->GetFName() == Name) return Handle;
		TArray<TSharedRef<IDetailTreeNode>> Children;
		Node->GetChildren(Children, true);
		if (const auto Found = FindSubjectLabelHandle(Children, Name)) return Found;
	}
	return nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotEditorAlphabeticSubjectLabelsTest,
	"ComposableCameraSystem.ShotEditor.AlphabeticSubjectLabels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotEditorAlphabeticSubjectLabelsTest::RunTest(const FString&)
{
	using namespace ComposableCameraSystem;
	struct FCase { int32 Index; const TCHAR* Label; };
	const FCase Cases[] = {
		{ 0, TEXT("Subject A") }, { 1, TEXT("Subject B") }, { 2, TEXT("Subject C") }, { 3, TEXT("Subject D") },
		{ 25, TEXT("Subject Z") }, { 26, TEXT("Subject AA") }, { 27, TEXT("Subject AB") },
		{ 51, TEXT("Subject AZ") }, { 52, TEXT("Subject BA") }, { 701, TEXT("Subject ZZ") }, { 702, TEXT("Subject AAA") },
		{ MAX_int32, TEXT("Subject FXSHRXX") }, { INDEX_NONE, TEXT("Subject ?") }
	};
	for (const auto& Case : Cases)
	{
		TCHAR Overlay[32];
		ShotSubjectLabels::WriteLabel(Case.Index, Overlay);
		TestEqual(TEXT("Viewport labels use alphabetic roles across boundaries"), FString(Overlay), FString(Case.Label));
		TestEqual(TEXT("UI labels match viewport roles"), ShotSubjectLabels::ToText(Case.Index).ToString(), FString(Case.Label));
	}
	if (!GEditor || GEditor->IsTransactionActive() || !FSlateApplication::IsInitialized()) return false;
	TStrongObjectPtr<UComposableCameraShotAsset> Asset(NewObject<UComposableCameraShotAsset>());
	Asset->Shot.Targets.SetNum(4);
	const auto Session = MakeShared<FComposableCameraShotAuthoringSession>();
	Session->Bind(&Asset->Shot, Asset.Get());
	const auto Panel = SNew(SShotEditorAuthoringPanel).Session(Session).PersistViewState(false);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const auto Card = Panel->GetSubjectCardForTesting(Index);
		if (!TestTrue(TEXT("Production Subject card exists"), Card.IsValid())) return false;
		TestTrue(TEXT("Production Subject header uses A/B/C/D"), ContainsSubjectLabel(Card.ToSharedRef(), Cases[Index].Label));
	}
	TestFalse(TEXT("Third Subject header never falls back to a number"),
		ContainsSubjectLabel(Panel->GetSubjectCardForTesting(2).ToSharedRef(), TEXT("Subject 3")));
	FShotEditorDiagnostic Diagnostic;
	Diagnostic.Issue = EShotEditorDiagnosticIssue::FollowAnchor;
	Diagnostic.TargetIndex = 2;
	TestTrue(TEXT("Diagnostics refer to the same Subject C"), Diagnostic.ToText().ToString().StartsWith(TEXT("Subject C ")));
	TStrongObjectPtr<UComposableCameraInstanceData> Instance(NewObject<UComposableCameraInstanceData>());
	Instance->InitializeFromShot(Asset->Shot);
	const FGuid SubjectId = Instance->Shot.Targets[2].SubjectId;
	const FName Path(*FString::Printf(TEXT("Shot.Targets.%s.Target.Offset.X"), *SubjectId.ToString(EGuidFormats::Digits)));
	TestTrue(TEXT("Sequencer parameter label uses Subject C"), CameraInstanceAuthoring::ParameterLabel(*Instance.Get(), Path).ToString().StartsWith(TEXT("Subject C / ")));
	TestTrue(TEXT("Label formatting preserves serialized Subject identity"), Instance->Shot.Targets[2].SubjectId == SubjectId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotEditorSubjectIndexLabelsTest,
	"ComposableCameraSystem.ShotEditor.SubjectIndexLabels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotEditorSubjectIndexLabelsTest::RunTest(const FString&)
{
	if (!GEditor || GEditor->IsTransactionActive() || !FSlateApplication::IsInitialized()) return false;
	TStrongObjectPtr<ULevelSequence> Sequence(NewObject<ULevelSequence>());
	Sequence->Initialize();
	UMovieScene* Scene = Sequence->GetMovieScene();
	TStrongObjectPtr<UMovieSceneComposableCameraShotSection> Section(NewObject<UMovieSceneComposableCameraShotSection>(Scene));
	auto& Shot = Section->InlineShot;
	Shot.Targets.SetNum(2);
	Shot.Placement.Mode = EShotPlacementMode::AnchorOrbit;
	Shot.Placement.BasisFrame = EShotPlacementBasisFrame::TwoTargetAxis;
	Shot.Placement.BasisActorIndex = 0;
	Shot.Placement.BasisSecondaryTargetIndex = 1;
	Shot.Placement.PlacementAnchor.TargetIndex = 0;
	Shot.Aim.Mode = EShotAimMode::LookAtAnchor;
	Shot.Aim.AimAnchor.TargetIndex = 1;
	Shot.Focus.Mode = EShotFocusMode::FollowCustomAnchor;
	Shot.Focus.FocusAnchor.TargetIndex = 1;
	// Actor references stay empty for bound sequence subjects. An inactive
	// Spawnable has authored metadata without any live playback actor.
	const FGuid Hero = Scene->AddPossessable(TEXT("Hero Binding"), AActor::StaticClass());
	TStrongObjectPtr<AActor> SpawnTemplate(NewObject<AActor>(Scene, NAME_None, RF_Transient | RF_ArchetypeObject));
	const FGuid Companion = Scene->AddSpawnable(TEXT("Companion Spawn"), *SpawnTemplate.Get());
	for (int32 Index = 0; Index < 2; ++Index)
	{
		auto& Override = Section->TargetActorOverrides.AddDefaulted_GetRef();
		Override.TargetIndex = Index;
		Override.Binding = UE::MovieScene::FRelativeObjectBindingID(Index == 0 ? Hero : Companion);
	}
	const auto Session = MakeShared<FComposableCameraShotAuthoringSession>();
	Session->Bind(&Shot, Section.Get());
	const auto Follow = SNew(SShotEditorParameterPanel).Session(Session).Section(EShotEditorParameterSection::Follow);
	const auto Aim = SNew(SShotEditorParameterPanel).Session(Session).Section(EShotEditorParameterSection::LookAt);
	const auto Focus = SNew(SShotEditorParameterPanel).Session(Session).Section(EShotEditorParameterSection::LensFocus);
	TestTrue(TEXT("Actual Follow anchor names the sequence-bound Subject A"), ContainsSubjectLabel(Follow, TEXT("Subject A (Hero Binding)")));
	TestTrue(TEXT("Actual secondary Basis index names inactive Spawnable Subject B"), ContainsSubjectLabel(Follow, TEXT("Subject B (Companion Spawn)")));
	TestTrue(TEXT("Actual Aim anchor uses the same Subject B name"), ContainsSubjectLabel(Aim, TEXT("Subject B (Companion Spawn)")));
	TestTrue(TEXT("Actual custom Focus anchor uses the same Subject B name"), ContainsSubjectLabel(Focus, TEXT("Subject B (Companion Spawn)")));
	TestTrue(TEXT("Displaying names does not copy bound actors into Shot data"), Shot.Targets[0].Target.Actor.IsNull() && Shot.Targets[1].Target.Actor.IsNull());

	const uint32 BeforeRename = Follow->GetRebuildCountForTesting();
	Scene->SetObjectDisplayName(Hero, FText::FromString(TEXT("Hero Renamed")));
	TestTrue(TEXT("Existing control reads a renamed Sequencer binding"), ContainsSubjectLabel(Follow, TEXT("Subject A (Hero Renamed)")));
	TestEqual(TEXT("Renaming needs no parameter-control rebuild"), Follow->GetRebuildCountForTesting(), BeforeRename);
	Shot.Placement.PlacementAnchor.Mode = EShotAnchorMode::WeightedWorldCentroid;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		auto& Weight = Shot.Placement.PlacementAnchor.WeightedTargets.AddDefaulted_GetRef();
		Weight.TargetIndex = Index;
		Weight.Weight = 1.f;
	}
	Follow->RefreshForTesting();
	// Inspect only the deeper weighted-entry group, so Basis rows cannot make
	// these assertions succeed when a weighted picker has lost its host.
	const auto WeightedWidget = Follow->RevealProperty(TEXT("Placement.PlacementAnchor.WeightedTargets"));
	if (TestTrue(TEXT("Production weighted-entry group is reachable"), WeightedWidget.IsValid()))
	{
		TestTrue(TEXT("Weighted index zero retains the Section host"), ContainsSubjectLabel(WeightedWidget.ToSharedRef(), TEXT("Subject A (Hero Renamed)")));
		TestTrue(TEXT("Weighted index one retains the Section host"), ContainsSubjectLabel(WeightedWidget.ToSharedRef(), TEXT("Subject B (Companion Spawn)")));
	}

	FPropertyRowGeneratorArgs Args;
	const auto Generator = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).CreatePropertyRowGenerator(Args);
	Generator->SetObjects({ Section.Get() });
	const auto Placement = FindSubjectLabelHandle(Generator->GetRootTreeNodes(), GET_MEMBER_NAME_CHECKED(FComposableCameraShot, Placement));
	if (!TestTrue(TEXT("Normal Section Details supplies the Shot property tree"), Placement.IsValid())) return false;
	const auto Anchor = Placement->GetChildHandle(GET_MEMBER_NAME_CHECKED(FShotPlacement, PlacementAnchor));
	const auto IndexHandle = Anchor ? Anchor->GetChildHandle(GET_MEMBER_NAME_CHECKED(FComposableCameraAnchorSpec, TargetIndex)) : nullptr;
	if (!TestTrue(TEXT("Normal Section Details supplies a target-index handle"), IndexHandle.IsValid())) return false;
	const auto Targets = FShotTargetIndexCombo::ResolveTargetsArrayUpwards(IndexHandle);
	TestTrue(TEXT("Normal Details discovers its own Section, without global editor context"),
		FShotTargetIndexCombo::FormatTargetEntryLabel(Targets, 0).ToString().Contains(TEXT("Hero Renamed")));
	FMenuBuilder Menu(true, nullptr);
	FShotTargetIndexCombo::BuildIndexMenu(Menu, IndexHandle, Targets, Section.Get());
	const auto MenuWidget = Menu.MakeWidget();
	TestTrue(TEXT("Actual index menu names Subject A"), ContainsSubjectLabel(MenuWidget, TEXT("Subject A (Hero Renamed)")));
	TestTrue(TEXT("Actual index menu names Subject B"), ContainsSubjectLabel(MenuWidget, TEXT("Subject B (Companion Spawn)")));
	TestTrue(TEXT("Out-of-range indices keep their invalid marker"), FShotTargetIndexCombo::FormatTargetEntryLabel(Targets, 12, Section.Get()).ToString().Contains(TEXT("invalid")));
	TestTrue(TEXT("Fixture can remove the authored Spawnable binding"), Scene->RemoveSpawnable(Companion));
	TestTrue(TEXT("A deleted binding stays explicit rather than becoming an unrelated actor"),
		FShotTargetIndexCombo::FormatTargetEntryLabel(Targets, 1, Section.Get()).ToString().Contains(TEXT("unresolved binding")));

	TStrongObjectPtr<ULevelSequence> OtherSequence(NewObject<ULevelSequence>());
	OtherSequence->Initialize();
	TStrongObjectPtr<UMovieSceneComposableCameraShotSection> OtherSection(NewObject<UMovieSceneComposableCameraShotSection>(OtherSequence->GetMovieScene()));
	OtherSection->InlineShot.Targets.SetNum(1);
	const FGuid OtherGuid = OtherSequence->GetMovieScene()->AddPossessable(TEXT("Other Shot Hero"), AActor::StaticClass());
	auto& OtherOverride = OtherSection->TargetActorOverrides.AddDefaulted_GetRef();
	OtherOverride.Binding = UE::MovieScene::FRelativeObjectBindingID(OtherGuid);
	Session->Bind(&OtherSection->InlineShot, OtherSection.Get());
	Follow->RefreshForTesting();
	TestTrue(TEXT("Source rebind refreshes the customization's explicit host"), ContainsSubjectLabel(Follow, TEXT("Other Shot Hero")));
	TestFalse(TEXT("Source rebind does not borrow the previous Section's name"), ContainsSubjectLabel(Follow, TEXT("Hero Renamed")));

	TStrongObjectPtr<UComposableCameraShotAsset> Asset(NewObject<UComposableCameraShotAsset>());
	Asset->Shot.Targets.SetNum(3);
	Asset->Shot.Targets[0].Target.Actor = TSoftObjectPtr<AActor>(FSoftObjectPath(TEXT("/Game/LabelFixture.LabelFixture:PersistentLevel.UnloadedHero")));
#if WITH_EDITORONLY_DATA
	Asset->Shot.Targets[1].Target.EditorPreviewMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/LabelFixture/PreviewCompanion.PreviewCompanion")));
#endif
	Session->Bind(&Asset->Shot, Asset.Get());
	Follow->RefreshForTesting();
	TestTrue(TEXT("Standalone asset keeps unloaded actor path naming"), ContainsSubjectLabel(Follow, TEXT("UnloadedHero")));
	Generator->SetStructure(MakeShared<FStructOnScope>(FComposableCameraShot::StaticStruct(), reinterpret_cast<uint8*>(&Asset->Shot)));
	const auto AssetTargets = FindSubjectLabelHandle(Generator->GetRootTreeNodes(), GET_MEMBER_NAME_CHECKED(FComposableCameraShot, Targets));
	if (!TestTrue(TEXT("Standalone asset has a targets array"), AssetTargets.IsValid())) return false;
#if WITH_EDITORONLY_DATA
	TestTrue(TEXT("Preview-only subject names its unloaded mesh"),
		FShotTargetIndexCombo::FormatTargetEntryLabel(AssetTargets->AsArray(), 1, Asset.Get()).ToString().Contains(TEXT("Subject B (PreviewCompanion)")));
	TestFalse(TEXT("Label lookup never force-loads a preview model"), Asset->Shot.Targets[1].Target.EditorPreviewMesh.IsValid());
#endif
	TestTrue(TEXT("Empty subjects remain distinguishable by role"),
		FShotTargetIndexCombo::FormatTargetEntryLabel(AssetTargets->AsArray(), 2, Asset.Get()).ToString().Contains(TEXT("Subject C (unset)")));
	FString Reason;
	TestTrue(TEXT("A later Subject append succeeds through the production session"), Session->AddTarget(nullptr, Reason));
	Follow->RefreshForTesting();
	Asset->Shot.Placement.PlacementAnchor.TargetIndex = 3;
	TestTrue(TEXT("Retained Follow page can name a newly appended empty role"), ContainsSubjectLabel(Follow, TEXT("Subject D (unset)")));
	TestTrue(TEXT("Metadata reads preserve the original actor binding identity"), Section->TargetActorOverrides[0].Binding.GetGuid() == Hero);
	return true;
}
#endif
