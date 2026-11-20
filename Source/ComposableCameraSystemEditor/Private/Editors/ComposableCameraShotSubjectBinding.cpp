// Copyright 2026 Sulley. All Rights Reserved.
#include "Editors/ComposableCameraShotSubjectBinding.h"
#include "Editors/ComposableCameraShotAuthoringSession.h"
#include "MovieScene/ComposableCameraInstanceData.h"
#include "Editor.h"
#include "Evaluation/MovieSceneEvaluationTemplateInstance.h"
#include "GameFramework/Actor.h"
#include "ISequencer.h"
#include "MovieScene.h"
#include "MovieSceneCommonHelpers.h"
#include "MovieSceneSequence.h"
#include "MVVM/ObjectBindingModelStorageExtension.h"
#include "MVVM/ViewModels/ObjectBindingModel.h"
#include "MVVM/ViewModels/SequencerEditorViewModel.h"
#include "MVVM/Views/SOutlinerView.h"

#define LOCTEXT_NAMESPACE "ShotSubjectBinding"

FText FShotSubjectBindingDiagnostic::GetMessage() const
{
	switch (Status)
	{
	case EShotSubjectBindingStatus::Unbound: return LOCTEXT("Optional", "Optional input - no Actor bound.");
	case EShotSubjectBindingStatus::RequiredUnbound: return LOCTEXT("Required", "Required input - choose an Actor to preview this camera.");
	case EShotSubjectBindingStatus::Resolved: return LOCTEXT("Resolved", "Actor resolved at the current frame.");
	case EShotSubjectBindingStatus::MissingBinding: return LOCTEXT("Missing", "Binding no longer exists. Choose another Actor or clear this input.");
	case EShotSubjectBindingStatus::Unspawned: return LOCTEXT("Unspawned", "Spawnable is not present at this frame. Locate its Sequence binding to check spawn keys.");
	case EShotSubjectBindingStatus::Unresolved: return LOCTEXT("Unresolved", "Binding exists, but its Actor is unavailable. Check deleted or unloaded actors and binding assignments.");
	case EShotSubjectBindingStatus::ClassMismatch: return LOCTEXT("ClassMismatch", "Bound object does not match this input's Actor class. Choose a compatible binding.");
	case EShotSubjectBindingStatus::SequenceUnavailable: return LOCTEXT("SequenceUnavailable", "Open and focus the source Level Sequence to resolve this binding.");
	case EShotSubjectBindingStatus::InterfaceChanged: return LOCTEXT("InterfaceChanged", "Camera Type interface changed. Finish the current edit; restore incompatible source fields or create a new draft. Saved bindings are retained.");
	}
	return FText::GetEmpty();
}

namespace ComposableCameraSystem::ShotSubjectBinding
{
FShotSubjectBindingDiagnostic Inspect(const FComposableCameraShotAuthoringSession& Session,
	FName Input, int32 SubjectIndex, const UClass* ExpectedClass, bool bRequired)
{
	FShotSubjectBindingDiagnostic Result;
	Result.Status = bRequired ? EShotSubjectBindingStatus::RequiredUnbound : EShotSubjectBindingStatus::Unbound;
	Result.Label = LOCTEXT("ChooseActor", "Choose Actor...");
	if (!Input.IsNone())
	{
		const auto* Values = Session.GetInstanceValues();
		if (!Values || !Values->IsTypeInterfaceCurrent() || !Values->IsTypeInterfaceValid() || !ExpectedClass)
		{ Result.Label = LOCTEXT("RetainedInput", "Input awaiting interface refresh"); Result.Status = EShotSubjectBindingStatus::InterfaceChanged; return Result; }
	}
	const auto Binding = Input.IsNone() ? Session.GetTargetBinding(SubjectIndex) : Session.GetInputBinding(Input);
	Result.BindingGuid = Binding.GetGuid();
	if (!Binding.IsValid())
	{
		if (Input.IsNone()) if (AActor* Actor = Session.ResolveTarget(SubjectIndex))
		{
			Result.Actor = Actor; Result.Label = FText::FromString(Actor->GetActorLabel());
			Result.Status = EShotSubjectBindingStatus::Resolved;
		}
		return Result;
	}
	const auto Seq = Session.GetSequencer();
	auto* BindingSequence = Session.GetSequence();
	const bool bSourceFocused = Seq && BindingSequence && Seq->GetFocusedMovieSceneSequence() == BindingSequence;
	if (Binding != FMovieSceneObjectBindingID(UE::MovieScene::FRelativeObjectBindingID(Binding.GetGuid())))
		BindingSequence = bSourceFocused ? Seq->GetEvaluationTemplate().GetSequence(Binding.ResolveSequenceID(Seq->GetFocusedTemplateID(), *Seq)) : nullptr;
	// UE5.6 exposes GetObjectDisplayName as non-const, even for this label query.
	UMovieScene* Scene = BindingSequence ? BindingSequence->GetMovieScene() : nullptr;
	if (!Scene)
	{ Result.Label = LOCTEXT("UnresolvedBinding", "Unresolved binding"); Result.Status = EShotSubjectBindingStatus::SequenceUnavailable; return Result; }
	if (!Scene->FindBinding(Binding.GetGuid()))
	{ Result.Label = LOCTEXT("MissingBinding", "Missing binding"); Result.Status = EShotSubjectBindingStatus::MissingBinding; return Result; }
	Result.Label = Scene->GetObjectDisplayName(Binding.GetGuid());
	if (!bSourceFocused) { Result.Status = EShotSubjectBindingStatus::SequenceUnavailable; return Result; }
	// A relative binding may resolve into another Sequence. Never select its GUID in this outliner.
	Result.bCanLocateBinding = BindingSequence == Seq->GetFocusedMovieSceneSequence();
	for (const auto Object : Binding.ResolveBoundObjects(Seq->GetFocusedTemplateID(), *Seq))
		if (auto* Actor = Cast<AActor>(Object.Get()); IsValid(Actor))
		{
			Result.Actor = Actor;
			if (!ExpectedClass || Actor->IsA(ExpectedClass)) { Result.Status = EShotSubjectBindingStatus::Resolved; return Result; }
		}
	const UClass* BoundClass = MovieSceneHelpers::GetBoundObjectClass(BindingSequence, Binding.GetGuid());
	if (Result.Actor.IsValid() || (ExpectedClass && BoundClass && !BoundClass->IsChildOf(ExpectedClass)))
		Result.Status = EShotSubjectBindingStatus::ClassMismatch;
	else Result.Status = MovieSceneHelpers::IsBoundToAnySpawnable(BindingSequence, Binding.GetGuid(), Seq->GetSharedPlaybackState())
		? EShotSubjectBindingStatus::Unspawned : EShotSubjectBindingStatus::Unresolved;
	return Result;
}

bool Locate(const FComposableCameraShotAuthoringSession& Session, FName Input, int32 SubjectIndex,
	const UClass* ExpectedClass, bool bRequired, bool bActor, FString& Error)
{
	Error.Reset();
	if (!GEditor || GIsTransacting || Session.IsEditing() || GEditor->IsTransactionActive())
	{ Error = TEXT("Finish the current edit before locating a Subject."); return false; }
	const auto Current = Inspect(Session, Input, SubjectIndex, ExpectedClass, bRequired);
	if (bActor)
	{
		auto* Actor = Current.Actor.Get();
		if (!IsValid(Actor)) { Error = Current.GetMessage().ToString(); return false; }
		GEditor->SelectNone(false, true, false);
		GEditor->SelectActor(Actor, true, true, true);
		GEditor->MoveViewportCamerasToActor(*Actor, false);
		return true;
	}
	const auto Seq = Session.GetSequencer();
	if (!Seq || !Current.bCanLocateBinding)
	{ Error = TEXT("The binding must exist in the focused source Sequence to locate it. External bindings keep their original Sequence identity."); return false; }
	using namespace UE::Sequencer;
	const auto ViewModel = Seq->GetViewModel();
	const auto* Storage = ViewModel ? ViewModel->GetRootModel()->CastDynamic<FObjectBindingModelStorageExtension>() : nullptr;
	const auto Model = Storage ? Storage->FindModelForObjectBinding(Current.BindingGuid) : nullptr;
	if (!Model) { Error = TEXT("The Sequence outliner is refreshing. Try locating this binding again."); return false; }
	Seq->EmptySelection(); Seq->SelectObject(Current.BindingGuid);
	if (const auto Tree = Seq->GetOutlinerViewWidget())
	{
		for (auto Parent : Model->GetAncestorsOfType<IOutlinerExtension>()) Tree->SetItemExpansion(Parent, true);
		Tree->RequestScrollIntoView(CastViewModel<IOutlinerExtension>(Model));
	}
	return true;
}
}
#undef LOCTEXT_NAMESPACE
