// Copyright 2026 Sulley. All Rights Reserved.
#include "Widgets/SShotEditorSubjectPicker.h"

#include "AssetRegistry/AssetData.h"
#include "Editors/ComposableCameraShotAuthoringSession.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "MovieScene/ComposableCameraInstanceData.h"
#include "Editor.h"
#include "Engine/Selection.h"
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
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/StyleColors.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ShotEditorSubjectPicker"

void SShotEditorSubjectPicker::Construct(const FArguments& Args)
{
	WeakSession = Args._Session;
	SubjectIndex = Args._SubjectIndex;
	InputPath = Args._InputPath;
	ExpectedActorClass = Args._ActorClass ? Args._ActorClass : AActor::StaticClass();
	bRequiredInput = Args._RequiredInput;
	OnError = Args._OnError;
	ChildSlot[SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[SNew(SWidgetSwitcher).WidgetIndex_Lambda([this]()
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
			.ToolTipText(LOCTEXT("ActorPickerTip", "Choose an existing Sequence binding or reference a Level actor. Use Clear in the menu to remove this input binding."))
			.OnGetMenuContent(this, &SShotEditorSubjectPicker::BuildMenu)
			.ButtonContent()[SNew(STextBlock).Text(this, &SShotEditorSubjectPicker::GetLabel).OverflowPolicy(ETextOverflowPolicy::Ellipsis)]]]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 5.f)
		[SNew(STextBlock).AutoWrapText(true).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.Visibility(InputPath.IsNone() ? EVisibility::Collapsed : EVisibility::Visible)
			.Text_Lambda([this]() { return InspectBinding().GetMessage(); })
			.ColorAndOpacity_Lambda([this]()
			{
				const auto Status = InspectBinding().Status;
				return Status == EShotSubjectBindingStatus::Resolved || Status == EShotSubjectBindingStatus::Unbound
					? FStyleColors::Foreground : FStyleColors::Warning;
			})]
		+ SVerticalBox::Slot().AutoHeight()
		[SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6.f, 4.f))
			+ SWrapBox::Slot()[SNew(SButton).Text(LOCTEXT("LocateSequence", "Locate in Sequence"))
				.ToolTipText(LOCTEXT("LocateSequenceTip", "Select and reveal this binding in the focused Sequence, including inactive Spawnables. Does not move the playhead."))
				.IsEnabled_Lambda([this]() { const auto Session = WeakSession.Pin(); return Session && !Session->IsEditing() && GEditor && !GEditor->IsTransactionActive() && InspectBinding().bCanLocateBinding; })
				.OnClicked_Lambda([this]() { return LocateBinding(false); })]
			+ SWrapBox::Slot()[SNew(SButton).Text(LOCTEXT("LocateActor", "Locate Actor"))
				.ToolTipText(LOCTEXT("LocateActorTip", "Select and frame the currently resolved Actor in the level viewport. Does not move the playhead."))
				.IsEnabled_Lambda([this]() { const auto Session = WeakSession.Pin(); return Session && !Session->IsEditing() && GEditor && !GEditor->IsTransactionActive() && InspectBinding().Actor.IsValid(); })
				.OnClicked_Lambda([this]() { return LocateBinding(true); })]]];
}

bool SShotEditorSubjectPicker::CanPick() const
{
	const auto Session = WeakSession.Pin();
	return Session && Session->CanEdit() && !Session->IsEditing() && GEditor && !GEditor->IsTransactionActive();
}

FText SShotEditorSubjectPicker::GetLabel() const
{
	return InspectBinding().Label;
}

FShotSubjectBindingDiagnostic SShotEditorSubjectPicker::InspectBinding() const
{
	const auto Session = WeakSession.Pin();
	return Session ? ComposableCameraSystem::ShotSubjectBinding::Inspect(*Session, InputPath, SubjectIndex, ExpectedActorClass.Get(), bRequiredInput)
		: FShotSubjectBindingDiagnostic();
}

FReply SShotEditorSubjectPicker::LocateBinding(bool bActor)
{
	if (const auto Session = WeakSession.Pin())
	{
		FString Error;
		ComposableCameraSystem::ShotSubjectBinding::Locate(*Session, InputPath, SubjectIndex, ExpectedActorClass.Get(), bRequiredInput, bActor, Error);
		OnError.ExecuteIfBound(Error);
	}
	return FReply::Handled();
}

TSharedRef<SWidget> SShotEditorSubjectPicker::BuildMenu()
{
	const auto Session = WeakSession.Pin();
	MenuHost = Session ? Session->GetHost() : nullptr;
	const auto* Values = Session ? Session->GetInstanceValues() : nullptr;
	MenuTypeRevision = Values && Values->Camera.TypeAsset ? Values->Camera.TypeAsset->GetEvaluationRevision() : 0;
	const UClass* RequiredClass = Session && !InputPath.IsNone() ? Session->GetInputActorClass(InputPath) : AActor::StaticClass();
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
			if (Class && RequiredClass && Class->IsChildOf(RequiredClass)) Bindings.Add(&Binding);
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
	Menu.AddSubMenu(LOCTEXT("LevelActors", "Level actors"), LOCTEXT("LevelActorReferenceTip", "Reference an existing level actor. Creates or reuses its Sequence binding without duplicating the actor."),
		FNewMenuDelegate::CreateSP(this, &SShotEditorSubjectPicker::BuildLevelActorMenu));
	return Menu.MakeWidget();
}

void SShotEditorSubjectPicker::BuildLevelActorMenu(FMenuBuilder& Menu)
{
	const auto Session = WeakSession.Pin();
	AActor* Actor = IsCurrentMenu() ? (InputPath.IsNone() ? Session->ResolveTarget(SubjectIndex) : Session->ResolveInputActor(InputPath)) : nullptr;
	const UClass* RequiredClass = Session && !InputPath.IsNone() ? Session->GetInputActorClass(InputPath) : AActor::StaticClass();
	const TWeakObjectPtr<const UClass> WeakClass = RequiredClass;
	Menu.AddWidget(PropertyCustomizationHelpers::MakeActorPickerWithMenu(Actor, true,
		FOnShouldFilterActor::CreateLambda([WeakClass](const AActor* Candidate) { return IsValid(Candidate) && WeakClass.IsValid() && Candidate->IsA(WeakClass.Get()); }),
		FOnActorSelected::CreateSP(this, &SShotEditorSubjectPicker::PickActor),
		FSimpleDelegate::CreateSP(this, &SShotEditorSubjectPicker::CloseMenu),
		FSimpleDelegate::CreateSP(this, &SShotEditorSubjectPicker::UseSelectedActor)), FText(), true);
}

bool SShotEditorSubjectPicker::AssignBinding(const FGuid& Guid)
{
	const auto Session = WeakSession.Pin();
	FString Reason;
	const bool bCurrentSource = IsCurrentMenu();
	const bool bAssigned = bCurrentSource && (InputPath.IsNone() ? Session->SetTargetBinding(SubjectIndex, Guid, Reason) : Session->SetInputBinding(InputPath, Guid, Reason));
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
	if (!IsCurrentMenu()) return;
	if (!Actor) { AssignBinding(FGuid()); return; }
	FString Reason;
	if (InputPath.IsNone()) Session->SetTargetActor(SubjectIndex, Actor, Reason);
	else Session->SetInputActor(InputPath, Actor, Reason);
	OnError.ExecuteIfBound(Reason);
}

void SShotEditorSubjectPicker::CloseMenu()
{
	FSlateApplication::Get().DismissAllMenus();
}

bool SShotEditorSubjectPicker::IsCurrentMenu() const
{
	const auto Session = WeakSession.Pin();
	if (!Session || !MenuHost.IsValid() || Session->GetHost() != MenuHost.Get()) return false;
	const auto* Values = Session->GetInstanceValues();
	return InputPath.IsNone() || (Values && Values->Camera.TypeAsset
		&& Values->Camera.TypeAsset->GetEvaluationRevision() == MenuTypeRevision && Values->IsTypeInterfaceCurrent());
}

void SShotEditorSubjectPicker::UseSelectedActor()
{
	if (GEditor && GEditor->GetSelectedActors())
		if (AActor* Actor = GEditor->GetSelectedActors()->GetTop<AActor>()) { PickActor(Actor); CloseMenu(); }
}

#undef LOCTEXT_NAMESPACE
