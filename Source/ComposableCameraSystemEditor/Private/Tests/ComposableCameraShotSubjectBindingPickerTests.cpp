// Copyright 2026 Sulley. All Rights Reserved.
#include "Editors/ComposableCameraShotAuthoringSession.h"
#include "Widgets/SShotEditorAuthoringPanel.h"
#include "Widgets/SShotEditorSubjectPicker.h"
#include "Bindings/MovieSceneSpawnableActorBinding.h"
#include "Components/SceneComponent.h"
#include "DataAssets/ComposableCameraShotAsset.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Actor.h"
#include "LevelSequence.h"
#include "Misc/AutomationTest.h"
#include "MovieScene.h"
#include "MovieSceneBindingReferences.h"
#include "MovieSceneSequence.h"
#include "MovieScene/MovieSceneComposableCameraShotSection.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Text/STextBlock.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
TSharedPtr<SShotEditorSubjectPicker> FindSubjectBindingPicker(const TSharedRef<SWidget>& Widget)
{
	if (Widget->GetType() == FName(TEXT("SShotEditorSubjectPicker"))) return StaticCastSharedRef<SShotEditorSubjectPicker>(Widget);
	FChildren* Children = Widget->GetChildren();
	for (int32 Index = 0; Index < Children->Num(); ++Index)
		if (const auto Picker = FindSubjectBindingPicker(Children->GetChildAt(Index))) return Picker;
	return nullptr;
}

bool ContainsBindingPickerText(const TSharedRef<SWidget>& Widget, const FString& Text)
{
	if (Widget->GetType() == FName(TEXT("STextBlock"))
		&& StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString().Contains(Text)) return true;
	FChildren* Children = Widget->GetChildren();
	for (int32 Index = 0; Index < Children->Num(); ++Index)
		if (ContainsBindingPickerText(Children->GetChildAt(Index), Text)) return true;
	return false;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotEditorSubjectBindingPickerTest,
	"ComposableCameraSystem.ShotEditor.SubjectBindingPicker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotEditorSubjectBindingPickerTest::RunTest(const FString&)
{
	if (!GEditor || GEditor->IsTransactionActive() || !FSlateApplication::IsInitialized()) return false;
	TStrongObjectPtr<ULevelSequence> Sequence(NewObject<ULevelSequence>(GetTransientPackage(), NAME_None, RF_Transactional));
	Sequence->Initialize();
	UMovieScene* Scene = Sequence->GetMovieScene();
	TStrongObjectPtr<AActor> Template(NewObject<AActor>(Scene, NAME_None, RF_Transient | RF_ArchetypeObject));
	const FGuid Spawned = Scene->AddSpawnable(TEXT("Inactive Spawn Hero"), *Template.Get());
	const FGuid Possessed = Scene->AddPossessable(TEXT("Possessed Companion"), AActor::StaticClass());
	const FGuid Component = Scene->AddPossessable(TEXT("Excluded Component"), USceneComponent::StaticClass());
	const FGuid CustomSpawned = Scene->AddPossessable(TEXT("Custom Spawn Hero"), AActor::StaticClass());
	TStrongObjectPtr<UMovieSceneSpawnableActorBinding> CustomBinding(NewObject<UMovieSceneSpawnableActorBinding>(Scene));
	CustomBinding->SetObjectTemplate(Template.Get());
	// ULevelSequence's const override hides the mutable base overload.
	UMovieSceneSequence* BindingSequence = Sequence.Get();
	FMovieSceneBindingReferences* References = BindingSequence->GetBindingReferences();
	if (!TestNotNull(TEXT("LevelSequence supports custom binding references"), References)) return false;
	References->AddBinding(CustomSpawned, CustomBinding.Get());
	TStrongObjectPtr<UComposableCameraShotAsset> Preset(NewObject<UComposableCameraShotAsset>());
	Preset->Shot.Targets.SetNum(1);
	Preset->Shot.Targets[0].Target.Offset = FVector(7, 8, 9);
	Preset->Shot.Targets[0].Target.ComponentName = TEXT("Body");
	Preset->Shot.Targets[0].Target.BoneName = TEXT("head");
	Preset->Shot.Targets[0].Target.bUseBoneAsPivot = true;
	Preset->Shot.Targets[0].BoundsShape = EShotTargetBoundsShape::ManualExtent;
	Preset->Shot.Targets[0].ManualBoundsExtent = FVector(12, 23, 34);
	Preset->Shot.Targets[0].BoundsContributionWeight = .35f;
	Preset->Shot.Targets[0].CachedAutoBoundsExtent = FVector(99);
	TStrongObjectPtr<UMovieSceneComposableCameraShotSection> Section(NewObject<UMovieSceneComposableCameraShotSection>(Scene, NAME_None, RF_Transactional));
	Section->Source = EComposableCameraShotSource::AssetReference;
	Section->ShotAssetRef = Preset.Get();
	Section->RefreshShotOverridesFromSource();
	const auto Session = MakeShared<FComposableCameraShotAuthoringSession>();
	Session->Bind(&Section->ShotOverrides, Section.Get());
	const auto Panel = SNew(SShotEditorAuthoringPanel).Session(Session).PersistViewState(false);
	const auto EditCard = Panel->GetSubjectCardForTesting(0);
	if (!TestTrue(TEXT("Edit provides Subject cards"), EditCard.IsValid())) return false;
	const auto Picker = FindSubjectBindingPicker(EditCard.ToSharedRef());
	if (!TestTrue(TEXT("Edit card uses the binding-aware picker"), Picker.IsValid())) return false;
	const auto Menu = Picker->BuildMenuForTesting();
	TestTrue(TEXT("Edit menu lists a Spawnable without a live actor"), ContainsBindingPickerText(Menu, TEXT("Inactive Spawn Hero")));
	TestTrue(TEXT("Edit menu lists an actor Possessable"), ContainsBindingPickerText(Menu, TEXT("Possessed Companion")));
	TestTrue(TEXT("Edit menu lists UE5.6 custom actor Spawnables"), ContainsBindingPickerText(Menu, TEXT("Custom Spawn Hero")));
	TestFalse(TEXT("Subject menu excludes component bindings"), ContainsBindingPickerText(Menu, TEXT("Excluded Component")));
	TestTrue(TEXT("Unbound level actors remain available"), ContainsBindingPickerText(Menu, TEXT("Level actors")));
	const int32 BindingCount = Scene->GetBindings().Num();
	TestTrue(TEXT("Real picker action accepts an inactive Spawnable"), Picker->ChooseBindingForTesting(Spawned));
	TestTrue(TEXT("Assignment stores a sequence-local binding ID"), Session->GetTargetBinding(0) == FMovieSceneObjectBindingID(UE::MovieScene::FRelativeObjectBindingID(Spawned)));
	TestNull(TEXT("Inactive binding has no fabricated live preview actor"), Session->ResolveTarget(0));
	TestTrue(TEXT("Shot data contains no actor template or transient instance"), Session->GetShot()->Targets[0].Target.Actor.IsNull());
	TestTrue(TEXT("Selected picker shows authored name while inactive"), ContainsBindingPickerText(Picker.ToSharedRef(), TEXT("Inactive Spawn Hero")));
	TestEqual(TEXT("Picking reuses the existing sequence binding"), Scene->GetBindings().Num(), BindingCount);
	TestTrue(TEXT("Binding preserves authored pivot"), Session->GetShot()->Targets[0].Target.Offset.Equals(FVector(7, 8, 9)));
	TestTrue(TEXT("Binding preserves component and bone"), Session->GetShot()->Targets[0].Target.ComponentName == TEXT("Body") && Session->GetShot()->Targets[0].Target.BoneName == TEXT("head"));
	TestTrue(TEXT("Binding preserves authored bounds"), Session->GetShot()->Targets[0].ManualBoundsExtent.Equals(FVector(12, 23, 34)));
	TestEqual(TEXT("Binding preserves framing weight"), Session->GetShot()->Targets[0].BoundsContributionWeight, .35f);
	TestTrue(TEXT("Assignment invalidates bounds from the previous actor"), Session->GetShot()->Targets[0].CachedAutoBoundsExtent.IsZero());
	TestTrue(TEXT("Binding assignment has an Undo step"), GEditor->UndoTransaction());
	TestFalse(TEXT("One Undo clears the prior empty-slot assignment"), Session->GetTargetBinding(0).IsValid());
	TestTrue(TEXT("Binding assignment can redo"), GEditor->RedoTransaction());
	TestTrue(TEXT("Redo restores the same binding identity"), Session->GetTargetBinding(0).GetGuid() == Spawned);
	Scene->SetObjectDisplayName(Spawned, FText::FromString(TEXT("Renamed Spawn Hero")));
	TestTrue(TEXT("Retained selected label reads a renamed binding"), ContainsBindingPickerText(Picker.ToSharedRef(), TEXT("Renamed Spawn Hero")));
	TestFalse(TEXT("Picker rejects non-actor bindings"), Picker->ChooseBindingForTesting(Component));
	TestFalse(TEXT("Picker rejects deleted or foreign binding GUIDs"), Picker->ChooseBindingForTesting(FGuid::NewGuid()));
	Scene->SetReadOnly(true);
	TestFalse(TEXT("Read-only source refuses assignment"), Picker->ChooseBindingForTesting(Possessed));
	Scene->SetReadOnly(false);
	Section->SetIsLocked(true);
	TestFalse(TEXT("Locked Section refuses assignment"), Picker->ChooseBindingForTesting(Possessed));
	Section->SetIsLocked(false);
	Session->BeginEdit(FText::FromString(TEXT("Binding picker gesture guard")));
	TestFalse(TEXT("An active gesture refuses binding assignment"), Picker->ChooseBindingForTesting(Possessed));
	Session->EndEdit();
	TestTrue(TEXT("Fixture removes the selected Spawnable"), Scene->RemoveSpawnable(Spawned));
	TestTrue(TEXT("Removed binding remains explicitly unresolved"), ContainsBindingPickerText(Picker.ToSharedRef(), TEXT("Unresolved binding")));
	TestTrue(TEXT("Clear works without a spawned actor"), Picker->ChooseBindingForTesting(FGuid()));
	TestFalse(TEXT("Clear removes the Section override"), Session->GetTargetBinding(0).IsValid());
	TestTrue(TEXT("Custom actor binding can be assigned without spawning"), Picker->ChooseBindingForTesting(CustomSpawned));
	TestTrue(TEXT("Shared preset remains unchanged"), Preset->Shot.Targets[0].Target.Actor.IsNull() && Preset->Shot.Targets[0].Target.Offset.Equals(FVector(7, 8, 9)));
	TestTrue(TEXT("Binding writes affect only the Section local copy"), Section->TargetActorOverrides.Num() == 1 && Session->GetHost() == Section.Get());
	Session->Bind(&Preset->Shot, Preset.Get());
	TestFalse(TEXT("A menu opened on another host cannot edit the new source"), Picker->ChooseBindingForTesting(Possessed));
	TestTrue(TEXT("Stale menu preserves the old Section binding"), Section->TargetActorOverrides[0].Binding.GetGuid() == CustomSpawned);
	return true;
}
#endif
