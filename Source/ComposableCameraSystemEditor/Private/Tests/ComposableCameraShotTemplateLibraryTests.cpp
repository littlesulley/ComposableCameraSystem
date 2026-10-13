// Copyright 2026 Sulley. All Rights Reserved.
#include "DataAssets/ComposableCameraShotTemplateLibrary.h"
#include "DataAssets/ComposableCameraShotAsset.h"
#include "AssetTypeCategories.h"
#include "Editors/ComposableCameraShotAuthoringSession.h"
#include "Factories/ComposableCameraShotTemplateLibraryFactory.h"
#include "Factories/ComposableCameraShotAssetFactory.h"
#include "MovieScene/MovieSceneComposableCameraInstanceTrack.h"
#include "MovieScene/MovieSceneComposableCameraInstanceSection.h"
#include "MovieScene/ComposableCameraInstanceData.h"
#include "Sequencer/ComposableCameraInstanceDragDropOp.h"
#include "Widgets/SShotEditorAuthoringPanel.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ISequencer.h"
#include "ISequencerModule.h"
#include "LevelSequence.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Modules/ModuleManager.h"
#include "MovieScene.h"
#include "ScopedTransaction.h"
#include "Serialization/ObjectReader.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
TSharedPtr<SWidget> FindTemplateWidget(const TSharedRef<SWidget>& Widget, FName Tag)
{
	if (Widget->GetTag() == Tag) return Widget;
	FChildren* Children = Widget->GetChildren();
	for (int32 Index = 0; Index < Children->Num(); ++Index)
		if (const auto Found = FindTemplateWidget(Children->GetChildAt(Index), Tag)) return Found;
	return nullptr;
}
bool TemplateHasText(const TSharedRef<SWidget>& Widget, const FString& Text)
{
	if (Widget->GetType() == FName(TEXT("STextBlock")) && StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString().Contains(Text)) return true;
	FChildren* Children = Widget->GetChildren();
	for (int32 Index = 0; Index < Children->Num(); ++Index)
		if (TemplateHasText(Children->GetChildAt(Index), Text)) return true;
	return false;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotTemplateLibraryRoundTripTest,
	"ComposableCameraSystem.ShotTemplateLibrary.RoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotTemplateLibraryRoundTripTest::RunTest(const FString&)
{
	TStrongObjectPtr<UComposableCameraShotTemplateLibraryFactory> Factory(NewObject<UComposableCameraShotTemplateLibraryFactory>());
	TStrongObjectPtr<UComposableCameraShotTemplateLibrary> Library(CastChecked<UComposableCameraShotTemplateLibrary>(
		Factory->FactoryCreateNew(UComposableCameraShotTemplateLibrary::StaticClass(), GetTransientPackage(), NAME_None, RF_Transient, nullptr, nullptr)));
	TestTrue(TEXT("New libraries have no hard-coded templates"), Library->Shots.IsEmpty());
	TestTrue(TEXT("Library is offered in the native right-click creation menu"), Factory->ShouldShowInNewMenu());
	const uint32 Categories = Factory->GetMenuCategories();
	TestTrue(TEXT("Library has a registered custom category, not Misc"), Categories != 0 && (Categories & EAssetTypeCategories::Misc) == 0);
	TestEqual(TEXT("Library shares the Shot asset's Composable Camera System submenu"),
		Categories, GetDefault<UComposableCameraShotAssetFactory>()->GetMenuCategories());
	int32 EditableFields = 0;
	for (TFieldIterator<FProperty> It(FComposableCameraShotTemplate::StaticStruct()); It; ++It)
		if (It->HasAnyPropertyFlags(CPF_Edit)) ++EditableFields;
	TestEqual(TEXT("Template entries expose only Name and Shot"), EditableFields, 2);
	const FObjectPropertyBase* ShotProperty = FindFProperty<FObjectPropertyBase>(FComposableCameraShotTemplate::StaticStruct(), TEXT("Shot"));
	TestTrue(TEXT("Shot is an existing Shot asset reference"), ShotProperty && ShotProperty->PropertyClass == UComposableCameraShotAsset::StaticClass());
	TStrongObjectPtr<UComposableCameraShotAsset> ShotAsset(NewObject<UComposableCameraShotAsset>());
	ShotAsset->Shot.Targets.SetNum(3);
	ShotAsset->Shot.Lens.ManualFOV = 47.f;
	ShotAsset->Shot.Aim.AimAnchor.TargetIndex = 2;
	ShotAsset->Shot.Targets[2].Target.Offset = FVector(7, 8, 9);
	auto& Entry = Library->Shots.AddDefaulted_GetRef();
	Entry.Name = TEXT("Conversation"); Entry.Shot = ShotAsset.Get();
	TArray<uint8> Bytes; FObjectWriter(Library.Get(), Bytes);
	TStrongObjectPtr<UComposableCameraShotTemplateLibrary> Loaded(NewObject<UComposableCameraShotTemplateLibrary>());
	FObjectReader(Loaded.Get(), Bytes);
	if (!TestEqual(TEXT("Shot list survives serialization"), Loaded->Shots.Num(), 1)) return false;
	TestTrue(TEXT("Template name survives"), Loaded->Shots[0].Name == Entry.Name);
	if (!TestTrue(TEXT("Serialization retains the asset reference, not an inline copy"), Loaded->Shots[0].Shot == ShotAsset.Get())) return false;
	TestEqual(TEXT("Subject count comes from referenced asset"), Loaded->Shots[0].Shot->Shot.Targets.Num(), 3);
	ShotAsset->Shot.Lens.ManualFOV = 52.f;
	TestEqual(TEXT("Library sees later changes to referenced asset"), Loaded->Shots[0].Shot->Shot.Lens.ManualFOV, 52.f);
	TestEqual(TEXT("Anchor role comes from referenced asset"), Loaded->Shots[0].Shot->Shot.Aim.AimAnchor.TargetIndex, 2);
	TestTrue(TEXT("Subject offset comes from referenced asset"), Loaded->Shots[0].Shot->Shot.Targets[2].Target.Offset.Equals(FVector(7, 8, 9)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotEditorTemplateLibraryTest,
	"ComposableCameraSystem.ShotEditor.TemplateLibrary", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotEditorTemplateLibraryTest::RunTest(const FString&)
{
	if (!GEditor || !GEngine || GEditor->IsTransactionActive() || !FSlateApplication::IsInitialized()) return false;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
	World->InitializeActorsForPlay(FURL()); World->BeginPlay();
	TStrongObjectPtr<ULevelSequence> Sequence(NewObject<ULevelSequence>(GetTransientPackage(), NAME_None, RF_Transactional));
	Sequence->Initialize();
	auto* Scene = Sequence->GetMovieScene();
	Scene->SetPlaybackRange(0, 1000);
	const FGuid OwnerBinding = Scene->AddPossessable(TEXT("Existing Shot"), AActor::StaticClass());
	const FGuid SubjectBinding = Scene->AddPossessable(TEXT("Inactive Subject"), AActor::StaticClass());
	auto* Owner = Scene->AddTrack<UMovieSceneComposableCameraInstanceTrack>(OwnerBinding);
	FComposableCameraShot Original; Original.Targets.SetNum(1); Original.Lens.ManualFOV = 31.f;
	Owner->Instance->InitializeFromShot(Original);
	auto* Activation = CastChecked<UMovieSceneComposableCameraInstanceSection>(Owner->CreateNewSection());
	Activation->SetRange(TRange<FFrameNumber>(0, 100)); Owner->AddSection(*Activation);
	FSequencerInitParams Params; Params.RootSequence = Sequence.Get(); Params.PlaybackContext = World;
	Params.ViewParams.UniqueName = TEXT("ShotTemplateLibraryRegression");
	const auto Sequencer = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer").CreateSequencer(Params);
	ON_SCOPE_EXIT { Sequencer->Close(); };
	const auto Session = MakeShared<FComposableCameraShotAuthoringSession>();
	Session->Bind(&Owner->Instance->Shot, Owner->Instance);
	const auto Panel = SNew(SShotEditorAuthoringPanel).Session(Session).PersistViewState(false);
	Panel->SelectCreateForTesting();
	const auto Create = Panel->GetCreatePageForTesting().ToSharedRef();
	TestFalse(TEXT("Create does not show subject guides"), Session->bShowSubjectGuide);
	TestFalse(TEXT("Create has no Use Selected Actors"), TemplateHasText(Create, TEXT("Use Selected Actors")));
	TestFalse(TEXT("Create has no Subject controls"), FindTemplateWidget(Create, TEXT("ShotEditor.Create.AddSubject")).IsValid());
	TStrongObjectPtr<UComposableCameraShotTemplateLibrary> Library(NewObject<UComposableCameraShotTemplateLibrary>(GetTransientPackage(), NAME_None, RF_Transactional));
	TStrongObjectPtr<UComposableCameraShotAsset> PairAsset(NewObject<UComposableCameraShotAsset>(GetTransientPackage(), NAME_None, RF_Transactional));
	TStrongObjectPtr<UComposableCameraShotAsset> EmptyAsset(NewObject<UComposableCameraShotAsset>());
	auto& Pair = Library->Shots.AddDefaulted_GetRef();
	Pair.Name = TEXT("Dialogue Pair"); Pair.Shot = PairAsset.Get();
	PairAsset->Shot.Targets.SetNum(2); PairAsset->Shot.Lens.ManualFOV = 42.f;
	PairAsset->Shot.Aim.AimAnchor.TargetIndex = 1;
	PairAsset->Shot.Targets[0].Target.Offset = FVector(2, 3, 4);
	PairAsset->Shot.Targets[0].SubjectId = FGuid::NewGuid();
	AActor* Actor = World->SpawnActor<AActor>();
	if (!Actor) return false;
	PairAsset->Shot.Targets[0].Target.Actor = Actor;
	auto& Empty = Library->Shots.AddDefaulted_GetRef();
	Empty.Name = TEXT("No Subjects"); Empty.Shot = EmptyAsset.Get();
	Library->Shots.AddDefaulted_GetRef().Name = TEXT("Unassigned");
	Panel->SetTemplateLibrary(Library.Get()); Panel->Tick(FGeometry(), 0., 0.f);
	TestTrue(TEXT("Selecting library alone leaves current instance intact"), Session->GetHost() == Owner->Instance);
	const auto Combo = FindTemplateWidget(Create, TEXT("ShotEditor.TemplateChoice"));
	if (!TestTrue(TEXT("Templates use a native dropdown"), Combo && Combo->GetType() == FName(TEXT("SComboButton")))) return false;
	TestFalse(TEXT("Create has no standalone template buttons"), FindTemplateWidget(Create, TEXT("ShotEditor.Template.0")).IsValid());
	FMenuBuilder Choices(true, nullptr);
	Panel->PopulateTemplateMenu(Choices);
	const auto ChoiceMenu = Choices.MakeWidget();
	TestTrue(TEXT("Dropdown lists authored names"), TemplateHasText(ChoiceMenu, TEXT("Dialogue Pair")) && TemplateHasText(ChoiceMenu, TEXT("No Subjects")));
	const auto& ChoiceBlocks = Choices.GetMultiBox()->GetBlocks();
	if (!TestEqual(TEXT("Dropdown contains each library entry"), ChoiceBlocks.Num(), 3)) return false;
	Panel->SlatePrepass();
	TestTrue(TEXT("Unassigned template remains visible for correction"), TemplateHasText(ChoiceMenu, TEXT("Unassigned (Missing Shot)")));
	TestFalse(TEXT("Unassigned template is disabled"), ChoiceBlocks[2]->GetDirectActions().CanExecute());
	Panel->CreateTemplateDraft(2);
	TestTrue(TEXT("Missing reference cannot replace current instance"), Session->GetHost() == Owner->Instance);
	TArray<FName> ChoiceOrder;
	TFunction<void(const TSharedRef<SWidget>&)> CollectChoices;
	CollectChoices = [&](const TSharedRef<SWidget>& Widget)
	{
		if (Widget->GetTag() == FName(TEXT("ShotEditor.TemplateChoice")) || Widget->GetTag() == FName(TEXT("ShotEditor.Create.NewDraft"))) ChoiceOrder.Add(Widget->GetTag());
		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Index < Children->Num(); ++Index) CollectChoices(Children->GetChildAt(Index));
	};
	CollectChoices(Create);
	TestTrue(TEXT("Empty draft is below the Shot dropdown"), ChoiceOrder.Num() == 2 && ChoiceOrder.Last() == FName(TEXT("ShotEditor.Create.NewDraft")));
	TestTrue(TEXT("Empty draft uses the requested caption"), TemplateHasText(Create, TEXT("Create Empty Shot")));
	const FUIAction Choose = ChoiceBlocks[0]->GetDirectActions();
	TestTrue(TEXT("Selecting the native dropdown entry creates a draft"), Choose.Execute());
	Panel->Tick(FGeometry(), 0., 0.f);
	TestTrue(TEXT("Closed dropdown shows selected template"), TemplateHasText(Combo.ToSharedRef(), TEXT("Dialogue Pair (2 Subjects)")));
	TestFalse(TEXT("Old popup actions expire after selection"), Choose.CanExecute());
	TestTrue(TEXT("Choosing a template creates a new Sequence draft"), Session->IsSequenceDraft() && Session->GetHost() != Owner->Instance);
	if (!TestEqual(TEXT("Template determines exact Subject count"), Session->GetShot()->Targets.Num(), 2)) return false;
	TestEqual(TEXT("Template FOV is copied"), Session->GetShot()->Lens.ManualFOV, 42.f);
	TestEqual(TEXT("Template anchor is copied"), Session->GetShot()->Aim.AimAnchor.TargetIndex, 1);
	TestTrue(TEXT("Authored pivot is copied"), Session->GetShot()->Targets[0].Target.Offset.Equals(FVector(2, 3, 4)));
	TestTrue(TEXT("Actor is assigned only after template creation"), Session->GetShot()->Targets[0].Target.Actor.IsNull());
	TestFalse(TEXT("Old template Subject identity is not reused"), Session->GetShot()->Targets[0].SubjectId == PairAsset->Shot.Targets[0].SubjectId);
	TestTrue(TEXT("Creation opens Edit Subjects"), Panel->ActivePage == SShotEditorAuthoringPanel::EPage::Edit && Session->bShowSubjectGuide);
	TestEqual(TEXT("Existing instance is preserved"), Owner->Instance->Shot.Lens.ManualFOV, 31.f);
	TestEqual(TEXT("Draft creates no output binding"), Scene->GetBindings().Num(), 2);
	FString Error;
	TestTrue(TEXT("Subjects can be bound after selecting Shot"), Session->SetTargetBinding(0, SubjectBinding, Error));
	TestTrue(TEXT("Binding stays on draft"), Session->GetTargetBinding(0).GetGuid() == SubjectBinding);
	TestTrue(TEXT("Referenced Shot asset is untouched"), PairAsset->Shot.Targets[0].Target.Actor.Get() == Actor && PairAsset->Shot.Lens.ManualFOV == 42.f);
	const auto Drag = FComposableCameraInstanceDragDropOp::New(Session, 1., Error);
	if (!Drag) return false;
	auto* Created = Drag->Drop(Sequencer, FFrameNumber(200));
	if (!Created) return false;
	TestEqual(TEXT("Template name becomes the LS instance name"), Scene->GetObjectDisplayName(Created->FindObjectBindingGuid()).ToString(), FString(TEXT("Dialogue Pair")));
	TestEqual(TEXT("Dropped instance keeps template subject slots"), Created->Instance->Shot.Targets.Num(), 2);
	Panel->NewSequenceDraft(); Panel->Tick(FGeometry(), 0., 0.f);
	TestTrue(TEXT("Empty draft clears the template selection"), TemplateHasText(Combo.ToSharedRef(), TEXT("Choose a Shot...")));
	TestEqual(TEXT("Create Empty Shot ignores selected library"), Session->GetShot()->Targets.Num(), 0);
	TestEqual(TEXT("Empty draft uses default FOV"), Session->GetShot()->Lens.ManualFOV, FComposableCameraShot().Lens.ManualFOV);
	TestEqual(TEXT("Empty draft does not alter dropped instance"), Created->Instance->Shot.Targets.Num(), 2);
	UObject* BeforeFailure = Session->GetHost();
	Scene->SetReadOnly(true); Panel->CreateTemplateDraft(0);
	TestTrue(TEXT("Read-only LS rejects creation without losing current source"), Session->GetHost() == BeforeFailure);
	Scene->SetReadOnly(false);
	{
		FScopedTransaction Transaction(NSLOCTEXT("ShotTemplateTest", "EditAsset", "Edit Referenced Shot"));
		PairAsset->Modify(); PairAsset->Shot.Targets.SetNum(3); PairAsset->Shot.Lens.ManualFOV = 65.f;
		FPropertyChangedEvent Event(nullptr, EPropertyChangeType::ValueSet); PairAsset->PostEditChangeProperty(Event);
	}
	Panel->Tick(FGeometry(), 0., 0.f);
	TestTrue(TEXT("Referenced asset edits refresh dropdown counts"), TemplateHasText(Panel->BuildTemplateMenu(), TEXT("Dialogue Pair (3 Subjects)")));
	TestEqual(TEXT("Editing referenced asset leaves an existing draft alone"), Session->GetShot()->Targets.Num(), 0);
	Panel->CreateTemplateDraft(0); Panel->Tick(FGeometry(), 0., 0.f);
	TestEqual(TEXT("Next draft copies current asset configuration"), Session->GetShot()->Lens.ManualFOV, 65.f);
	TestEqual(TEXT("Next draft copies current asset subject count"), Session->GetShot()->Targets.Num(), 3);
	TestTrue(TEXT("Referenced asset edit undoes"), GEditor->UndoTransaction()); Panel->Tick(FGeometry(), 0., 0.f);
	TestTrue(TEXT("Referenced asset Undo refreshes template counts"), TemplateHasText(Create, TEXT("Dialogue Pair (2 Subjects)")));
	TestEqual(TEXT("Asset Undo does not replace the independent draft"), Session->GetShot()->Targets.Num(), 3);
	TestEqual(TEXT("Asset edits never rewrite an inserted instance"), Created->Instance->Shot.Lens.ManualFOV, 42.f);
	{
		FScopedTransaction Transaction(NSLOCTEXT("ShotTemplateTest", "Rename", "Rename Template"));
		Library->Modify(); Library->Shots[0].Name = TEXT("Renamed Pair");
		FPropertyChangedEvent Event(nullptr, EPropertyChangeType::ValueSet); Library->PostEditChangeProperty(Event);
	}
	Panel->Tick(FGeometry(), 0., 0.f);
	TestTrue(TEXT("Editing library refreshes names"), TemplateHasText(Create, TEXT("Renamed Pair")));
	TestTrue(TEXT("Library rename undoes"), GEditor->UndoTransaction()); Panel->Tick(FGeometry(), 0., 0.f);
	TestTrue(TEXT("Undo refreshes template choices"), TemplateHasText(Create, TEXT("Dialogue Pair")));
	FMenuBuilder BeforeClear(true, nullptr); Panel->PopulateTemplateMenu(BeforeClear);
	const FUIAction StaleChoice = BeforeClear.GetMultiBox()->GetBlocks()[0]->GetDirectActions();
	UObject* BeforeClearHost = Session->GetHost();
	Panel->SetTemplateLibrary(nullptr); Panel->Tick(FGeometry(), 0., 0.f);
	Panel->SlatePrepass();
	TestFalse(TEXT("Clearing library disables the retained dropdown"), Combo->IsEnabled());
	TestTrue(TEXT("Clearing library resets the displayed choice"), TemplateHasText(Combo.ToSharedRef(), TEXT("Choose a template library first")));
	TestFalse(TEXT("Stale popup cannot choose a different library slot"), StaleChoice.Execute());
	TestTrue(TEXT("Stale popup preserves current draft"), Session->GetHost() == BeforeClearHost);
	TestTrue(TEXT("Empty draft remains available without library"), FindTemplateWidget(Create, TEXT("ShotEditor.Create.NewDraft")).IsValid());
	FComposableCameraShotAuthoringSession Standalone;
	TestTrue(TEXT("Templates also create standalone drafts"), Standalone.StartTemplateDraft(PairAsset->Shot, FText::FromString(TEXT("Standalone")), nullptr, Error));
	TestTrue(TEXT("Standalone copy is editable without LS"), Standalone.CanEdit() && !Standalone.GetSequence());
	TestEqual(TEXT("Standalone copy keeps subject count"), Standalone.GetShot()->Targets.Num(), 2);
	return true;
}
#endif
