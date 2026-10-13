// Copyright 2026 Sulley. All Rights Reserved.
#include "Widgets/SShotEditorSubjectPicker.h"

#include "AssetRegistry/AssetData.h"
#include "Editors/ComposableCameraShotAuthoringSession.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Evaluation/MovieSceneEvaluationTemplateInstance.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameFramework/Actor.h"
#include "ISequencer.h"
#include "MovieScene.h"
#include "MovieSceneCommonHelpers.h"
#include "MovieScene/MovieSceneComposableCameraShotSection.h"
#include "MovieSceneSequence.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ShotEditorSubjectPicker"

void SShotEditorSubjectPicker::Construct(const FArguments& Args)
{
	WeakSession = Args._Session;
	SubjectIndex = Args._SubjectIndex;
	OnError = Args._OnError;
	ChildSlot[SNew(SWidgetSwitcher).WidgetIndex_Lambda([this]()
		{ const auto Session = WeakSession.Pin(); return Session && Session->GetSequence() ? 1 : 0; })
		+ SWidgetSwitcher::Slot()[SNew(SObjectPropertyEntryBox).AllowedClass(AActor::StaticClass())
			.ObjectPath_Lambda([this]()
			{
				const auto Session = WeakSession.Pin();
				AActor* Actor = Session ? Session->ResolveTarget(SubjectIndex) : nullptr;
				return Actor ? Actor->GetPathName() : FString();
			})
			.OnObjectChanged_Lambda([this](const FAssetData& Asset)
			{
				const auto Session = WeakSession.Pin();
				if (!Session || Session->GetSequence()) return;
				FString Reason;
				Session->SetTargetActor(SubjectIndex, Cast<AActor>(Asset.GetAsset()), Reason);
				OnError.ExecuteIfBound(Reason);
			})]
		+ SWidgetSwitcher::Slot()[SNew(SComboButton).IsEnabled(this, &SShotEditorSubjectPicker::CanPick)
			.OnGetMenuContent(this, &SShotEditorSubjectPicker::BuildMenu)
			.ButtonContent()[SNew(STextBlock).Text(this, &SShotEditorSubjectPicker::GetLabel)]]];
}

bool SShotEditorSubjectPicker::CanPick() const
{
	const auto Session = WeakSession.Pin();
	return Session && Session->CanEdit() && !Session->IsEditing() && GEditor && !GEditor->IsTransactionActive();
}

FText SShotEditorSubjectPicker::GetLabel() const
{
	const auto Session = WeakSession.Pin();
	if (!Session) return LOCTEXT("None", "None");
	const FMovieSceneObjectBindingID Binding = Session->GetTargetBinding(SubjectIndex);
	if (Binding.IsValid())
	{
		UMovieSceneSequence* Sequence = Session->GetSequence();
		if (Binding != FMovieSceneObjectBindingID(UE::MovieScene::FRelativeObjectBindingID(Binding.GetGuid())))
		{
			const auto Sequencer = Session->GetSequencer();
			const FMovieSceneSequenceID SourceID = Sequencer && Sequencer->GetFocusedMovieSceneSequence() == Sequence
				? Sequencer->GetFocusedTemplateID() : MovieSceneSequenceID::Root;
			Sequence = Sequencer ? Sequencer->GetEvaluationTemplate().GetSequence(Binding.ResolveSequenceID(SourceID, *Sequencer)) : nullptr;
		}
		UMovieScene* Scene = Sequence ? Sequence->GetMovieScene() : nullptr;
		if (Scene && Scene->FindBinding(Binding.GetGuid())) return Scene->GetObjectDisplayName(Binding.GetGuid());
		return LOCTEXT("Unresolved", "Unresolved binding");
	}
	if (const AActor* Actor = Session->ResolveTarget(SubjectIndex)) return FText::FromString(Actor->GetActorLabel());
	return LOCTEXT("None", "None");
}

TSharedRef<SWidget> SShotEditorSubjectPicker::BuildMenu()
{
	const auto Session = WeakSession.Pin();
	MenuHost = Session ? Session->GetHost() : nullptr;
	FMenuBuilder Menu(true, nullptr);
	Menu.AddMenuEntry(LOCTEXT("Clear", "Clear"), FText(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SShotEditorSubjectPicker::PickBinding, FGuid())));
	Menu.BeginSection(NAME_None, LOCTEXT("SequenceActors", "Sequence actors"));
	UMovieSceneSequence* Sequence = Session ? Session->GetSequence() : nullptr;
	UMovieScene* Scene = Sequence ? Sequence->GetMovieScene() : nullptr;
	TArray<const FMovieSceneBinding*> Bindings;
	if (Scene)
		for (const auto& Binding : Scene->GetBindings())
		{
			const UClass* Class = MovieSceneHelpers::GetBoundObjectClass(Sequence, Binding.GetObjectGuid());
			if (Class && Class->IsChildOf(AActor::StaticClass())) Bindings.Add(&Binding);
		}
	Bindings.Sort([](const FMovieSceneBinding& A, const FMovieSceneBinding& B) { return A.GetName() < B.GetName(); });
	for (const auto* Binding : Bindings)
	{
		const FGuid Guid = Binding->GetObjectGuid();
		Menu.AddMenuEntry(Scene->GetObjectDisplayName(Guid),
			LOCTEXT("BindingTip", "Use this sequence actor binding, including when its Spawnable is not currently spawned."), FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SShotEditorSubjectPicker::PickBinding, Guid)));
	}
	if (Bindings.IsEmpty()) Menu.AddMenuEntry(LOCTEXT("NoActors", "No actor bindings"), FText(), FSlateIcon(), FUIAction());
	Menu.EndSection();
	Menu.AddSubMenu(LOCTEXT("LevelActors", "Level actors"), LOCTEXT("LevelActorTip", "Choose a level actor and create or reuse its sequence binding."),
		FNewMenuDelegate::CreateSP(this, &SShotEditorSubjectPicker::BuildLevelActorMenu));
	return Menu.MakeWidget();
}

void SShotEditorSubjectPicker::BuildLevelActorMenu(FMenuBuilder& Menu)
{
	const auto Session = WeakSession.Pin();
	AActor* Actor = Session && Session->GetHost() == MenuHost.Get() ? Session->ResolveTarget(SubjectIndex) : nullptr;
	Menu.AddWidget(PropertyCustomizationHelpers::MakeActorPickerWithMenu(Actor, true,
		FOnShouldFilterActor::CreateLambda([](const AActor* Candidate) { return IsValid(Candidate); }),
		FOnActorSelected::CreateSP(this, &SShotEditorSubjectPicker::PickActor),
		FSimpleDelegate::CreateSP(this, &SShotEditorSubjectPicker::CloseMenu),
		FSimpleDelegate::CreateSP(this, &SShotEditorSubjectPicker::UseSelectedActor)), FText(), true);
}

bool SShotEditorSubjectPicker::AssignBinding(const FGuid& Guid)
{
	const auto Session = WeakSession.Pin();
	FString Reason;
	const bool bCurrentSource = Session && MenuHost.IsValid() && Session->GetHost() == MenuHost.Get();
	const bool bAssigned = bCurrentSource && Session->SetTargetBinding(SubjectIndex, Guid, Reason);
	if (!bCurrentSource) Reason = TEXT("The Shot source changed. Reopen the Subject picker.");
	OnError.ExecuteIfBound(Reason);
	return bAssigned;
}

void SShotEditorSubjectPicker::PickBinding(FGuid Guid)
{
	AssignBinding(Guid);
}

void SShotEditorSubjectPicker::PickActor(AActor* Actor)
{
	const auto Session = WeakSession.Pin();
	if (!Session || !MenuHost.IsValid() || Session->GetHost() != MenuHost.Get()) return;
	if (!Actor) { AssignBinding(FGuid()); return; }
	FString Reason;
	Session->SetTargetActor(SubjectIndex, Actor, Reason);
	OnError.ExecuteIfBound(Reason);
}

void SShotEditorSubjectPicker::CloseMenu()
{
	FSlateApplication::Get().DismissAllMenus();
}

void SShotEditorSubjectPicker::UseSelectedActor()
{
	if (GEditor && GEditor->GetSelectedActors())
		if (AActor* Actor = GEditor->GetSelectedActors()->GetTop<AActor>()) { PickActor(Actor); CloseMenu(); }
}

#undef LOCTEXT_NAMESPACE
