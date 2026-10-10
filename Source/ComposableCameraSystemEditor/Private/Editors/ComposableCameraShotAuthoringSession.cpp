// Copyright 2026 Sulley. All Rights Reserved.
#include "Editors/ComposableCameraShotAuthoringSession.h"
#include "Widgets/ComposableCameraShotViewportDisplayUtils.h"

#include "AssetToolsModule.h"
#include "CineCameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "ComposableCameraSystemEditorModule.h"
#include "DataAssets/ComposableCameraShotAsset.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/SkeletalMesh.h"
#include "Factories/ComposableCameraShotAssetFactory.h"
#include "ISequencer.h"
#include "LevelSequence/ComposableCameraLevelSequenceComponent.h"
#include "MovieScene.h"
#include "MovieScene/MovieSceneComposableCameraShotSection.h"
#include "MovieScene/MovieSceneComposableCameraShotTrack.h"
#include "MovieSceneSequence.h"
#include "Modules/ModuleManager.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"
#include "Sequencer/ComposableCameraShotAuthoring.h"
#include "Misc/TransactionObjectEvent.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "ComposableCameraShotAuthoringSession"
using namespace ComposableCameraSystem::ShotAuthoring;

FComposableCameraShotAuthoringSession::FComposableCameraShotAuthoringSession()
{
	PropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &FComposableCameraShotAuthoringSession::OnPropertyChanged);
	ObjectTransactedHandle = FCoreUObjectDelegates::OnObjectTransacted.AddRaw(this, &FComposableCameraShotAuthoringSession::OnObjectTransacted);
}

FComposableCameraShotAuthoringSession::~FComposableCameraShotAuthoringSession()
{
	EndEdit();
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyChangedHandle);
	FCoreUObjectDelegates::OnObjectTransacted.Remove(ObjectTransactedHandle);
}

void FComposableCameraShotAuthoringSession::Bind(FComposableCameraShot* Shot, UObject* InHost)
{
	EndEdit();
	Host = InHost;
	CachedSequencer.Reset();
	LegacyShot = Shot;
	ShotProperty = NAME_None;
	if (InHost && Shot)
	{
		for (TFieldIterator<FStructProperty> It(InHost->GetClass()); It; ++It)
		{
			if (It->Struct == FComposableCameraShot::StaticStruct()
				&& It->ContainerPtrToValuePtr<FComposableCameraShot>(InHost) == Shot)
			{
				ShotProperty = It->GetFName();
				LegacyShot = nullptr;
				break;
			}
		}
	}
	bPreviewPending = true;
	bUseLevelWorld = GetSection() != nullptr || InHost == Scratch.Get();
}

FComposableCameraShot* FComposableCameraShotAuthoringSession::GetShot() const
{
	UObject* Object = Host.Get();
	if (!Object) return nullptr;
	if (UMovieSceneComposableCameraShotSection* Section = Cast<UMovieSceneComposableCameraShotSection>(Object))
	{
		return Section->ResolveShotEditorShot();
	}
	FStructProperty* Property = FindFProperty<FStructProperty>(Object->GetClass(), ShotProperty);
	return Property && Property->Struct == FComposableCameraShot::StaticStruct()
		? Property->ContainerPtrToValuePtr<FComposableCameraShot>(Object) : LegacyShot;
}

UMovieSceneComposableCameraShotSection* FComposableCameraShotAuthoringSession::GetSection() const
{
	return Cast<UMovieSceneComposableCameraShotSection>(Host.Get());
}

bool FComposableCameraShotAuthoringSession::CanEdit() const
{
	if (!GetShot()) return false;
	if (const auto* Section = GetSection())
	{
		const UMovieScene* Scene = Section->GetTypedOuter<UMovieScene>();
		return Scene && !Scene->IsReadOnly() && !Section->IsLocked();
	}
	return GetShot() != nullptr;
}

UMovieSceneSequence* FComposableCameraShotAuthoringSession::GetSequence() const
{
	UMovieSceneComposableCameraShotSection* Section = GetSection();
	return Section ? Section->GetTypedOuter<UMovieSceneSequence>() : nullptr;
}

TSharedPtr<ISequencer> FComposableCameraShotAuthoringSession::GetSequencer() const
{
	if (TSharedPtr<ISequencer> Cached = CachedSequencer.Pin())
	{
		if (Cached->GetFocusedMovieSceneSequence() == GetSequence() || Cached->GetRootMovieSceneSequence() == GetSequence()) return Cached;
	}
	if (UMovieSceneSequence* Sequence = GetSequence())
	{
		TSharedPtr<ISequencer> Result = FModuleManager::LoadModuleChecked<FComposableCameraSystemEditorModule>("ComposableCameraSystemEditor")
			.FindOpenSequencerForSequence(Sequence);
		CachedSequencer = Result;
		return Result;
	}
	return nullptr;
}

AActor* FComposableCameraShotAuthoringSession::ResolveTarget(int32 Index) const
{
	const FComposableCameraShot* Shot = GetShot();
	if (!Shot || !Shot->Targets.IsValidIndex(Index)) return nullptr;
	if (UMovieSceneComposableCameraShotSection* Section = GetSection())
	{
		for (const FComposableCameraShotTargetActorOverride& Override : Section->TargetActorOverrides)
		{
			if (Override.TargetIndex != Index || !Override.Binding.IsValid()) continue;
			if (TSharedPtr<ISequencer> Sequencer = GetSequencer())
			{
				const FMovieSceneSequenceID SourceID = Sequencer->GetFocusedMovieSceneSequence() == GetSequence() ? Sequencer->GetFocusedTemplateID() : MovieSceneSequenceID::Root;
				for (TWeakObjectPtr<UObject> Object : Override.Binding.ResolveBoundObjects(SourceID, *Sequencer))
				{
					if (AActor* Actor = Cast<AActor>(Object.Get())) return Actor;
				}
			}
			return nullptr; // An unresolved explicit binding never silently previews an unrelated placeholder.
		}
	}
	return Shot->Targets[Index].Target.Actor.Get();
}

UCineCameraComponent* FComposableCameraShotAuthoringSession::GetOutputCamera() const
{
	UMovieSceneComposableCameraShotSection* Section = GetSection();
	TSharedPtr<ISequencer> Sequencer = GetSequencer();
	if (!Section || !Sequencer || Sequencer->GetFocusedMovieSceneSequence() != GetSequence()
		|| !Section->IsActive() || !Section->GetRange().Contains(Sequencer->GetLocalTime().Time.FloorToFrame())) return nullptr;
	return GetConfiguredCamera(false);
}

UCineCameraComponent* FComposableCameraShotAuthoringSession::GetConfiguredCamera(bool bAllowTemplate) const
{
	UMovieSceneComposableCameraShotSection* Section = GetSection();
	TSharedPtr<ISequencer> Sequencer = GetSequencer();
	if (!Section || !Sequencer || Sequencer->GetFocusedMovieSceneSequence() != GetSequence()) return nullptr;
	const UMovieSceneTrack* Track = Section->GetTypedOuter<UMovieSceneTrack>();
	if (!Track) return nullptr;
	for (TWeakObjectPtr<UObject> Object : Sequencer->FindBoundObjects(Track->FindObjectBindingGuid(), Sequencer->GetFocusedTemplateID()))
	{
		if (AActor* Actor = Cast<AActor>(Object.Get()))
		{
			if (UComposableCameraLevelSequenceComponent* Component = Actor->FindComponentByClass<UComposableCameraLevelSequenceComponent>())
			{
				return Component->OutputCineCameraComponent;
			}
		}
	}
	// A spawnable outside its spawn range has no live instance. Read its template
	// configuration for aspect only; it must never become the active output view.
	if (bAllowTemplate)
		if (AActor* Actor = Cast<AActor>(Sequencer->FindSpawnedObjectOrTemplate(Track->FindObjectBindingGuid())))
			if (auto* Component = Actor->FindComponentByClass<UComposableCameraLevelSequenceComponent>())
				return Component->OutputCineCameraComponent;
	return nullptr;
}

float FComposableCameraShotAuthoringSession::GetPreviewAspectRatio() const
{
	return ComposableCameraSystem::ShotViewportDisplay::CameraAspectRatio(GetConfiguredCamera());
}

void FComposableCameraShotAuthoringSession::NotifyViewportValueCommit()
{
	NotifyNativePropertyChange(FPropertyChangedEvent(nullptr, EPropertyChangeType::ValueSet));
}

void FComposableCameraShotAuthoringSession::BeginEdit(const FText& Description)
{
	if (!Transaction && CanEdit())
	{
		Transaction = MakeUnique<FScopedTransaction>(Description);
		EditingProperty = ShotProperty;
		if (const auto* Section = GetSection()) EditingProperty = Section->Source == EComposableCameraShotSource::Inline
			? GET_MEMBER_NAME_CHECKED(UMovieSceneComposableCameraShotSection, InlineShot) : GET_MEMBER_NAME_CHECKED(UMovieSceneComposableCameraShotSection, ShotOverrides);
		SaveToTransactionBuffer(Host.Get(), false);
		bGestureChanged = false;
		bStructuralChange = false;
	}
}

void FComposableCameraShotAuthoringSession::Changed(bool bStructural)
{
	bPreviewPending = true;
	bGestureChanged = true;
	bStructuralChange |= bStructural;
	if (!Transaction && OnChanged) OnChanged(bStructural);
}

void FComposableCameraShotAuthoringSession::NotifyNativePropertyChange(const FPropertyChangedEvent& Event)
{
	const uint32 ArrayChanges = EPropertyChangeType::ArrayAdd | EPropertyChangeType::ArrayRemove
		| EPropertyChangeType::ArrayClear | EPropertyChangeType::Duplicate | EPropertyChangeType::ArrayMove;
	Changed((Event.ChangeType & ArrayChanges) != 0);
	if ((Event.ChangeType & EPropertyChangeType::Interactive) != 0) return;

	UObject* Object = Host.Get();
	if (!Object) return;
	FName PropertyName = ShotProperty;
	if (const auto* Section = GetSection()) PropertyName = Section->Source == EComposableCameraShotSource::Inline
		? GET_MEMBER_NAME_CHECKED(UMovieSceneComposableCameraShotSection, InlineShot)
		: GET_MEMBER_NAME_CHECKED(UMovieSceneComposableCameraShotSection, ShotOverrides);
	if (FProperty* Property = Object->GetClass()->FindPropertyByName(PropertyName))
	{
		// This outer event is our own native commit, not an external source replacement.
		TGuardValue<bool> Guard(bNotifyingHost, true);
		FPropertyChangedEvent OuterEvent(Property, Event.ChangeType);
		Object->PostEditChangeProperty(OuterEvent);
	}
	Object->MarkPackageDirty();
}

void FComposableCameraShotAuthoringSession::NotifyHost()
{
	UObject* Object = Host.Get();
	if (!Object) return;
	const FName Name = EditingProperty;
	if (FProperty* Property = Object->GetClass()->FindPropertyByName(Name))
	{
		FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
		Object->PostEditChangeProperty(Event);
	}
	Object->MarkPackageDirty();
}

void FComposableCameraShotAuthoringSession::EndEdit()
{
	if (!Transaction) return;
	const bool bNotify = bGestureChanged;
	const bool bRebind = bStructuralChange;
	if (bNotify) NotifyHost();
	else Transaction->Cancel();
	Transaction.Reset();
	bGestureChanged = false;
	bStructuralChange = false;
	if (bNotify && OnChanged) OnChanged(bRebind);
}

void FComposableCameraShotAuthoringSession::OnPropertyChanged(UObject* Object, FPropertyChangedEvent& Event)
{
	if (Object == Host.Get())
	{
		bPreviewPending = true;
		if (!Transaction && !bNotifyingHost && (Event.ChangeType & EPropertyChangeType::Interactive) == 0 && OnChanged) OnChanged(true);
	}
}

void FComposableCameraShotAuthoringSession::OnObjectTransacted(UObject* Object, const FTransactionObjectEvent& Event)
{
	if (Object == Host.Get())
	{
		bPreviewPending = true;
		// Finalized and Snapshot events accompany ordinary edits; only history playback invalidates handles.
		if (!Transaction && Event.GetEventType() == ETransactionObjectEventType::UndoRedo && OnChanged) OnChanged(true);
	}
}

void FComposableCameraShotAuthoringSession::Tick()
{
	if (!bPreviewPending) return;
	UMovieSceneComposableCameraShotSection* Section = GetSection();
	TSharedPtr<ISequencer> Sequencer = GetSequencer();
	if (!Section || !Sequencer || Sequencer->GetPlaybackStatus() == EMovieScenePlayerStatus::Playing) return;
	if (Sequencer->GetFocusedMovieSceneSequence() != GetSequence()
		|| !Section->IsActive() || !Section->GetRange().Contains(Sequencer->GetLocalTime().Time.FloorToFrame())) return;
	UCineCameraComponent* Camera = GetOutputCamera();
	UComposableCameraLevelSequenceComponent* Component = Camera && Camera->GetOwner()
		? Camera->GetOwner()->FindComponentByClass<UComposableCameraLevelSequenceComponent>() : nullptr;
	if (!Component || !Section->BuildEffectiveShotWithoutBindings(PreviewScratch)) return;
	for (int32 Index = 0; Index < PreviewScratch.Targets.Num(); ++Index) PreviewScratch.Targets[Index].Target.Actor = ResolveTarget(Index);
	if (Component->RefreshShotEditorPreview(Section, PreviewScratch))
	{
		bPreviewPending = false;
		if (GEditor) GEditor->RedrawLevelEditingViewports(false);
	}
}

void FComposableCameraShotAuthoringSession::EnsureScratch()
{
	if (GetShot()) return;
	Scratch = NewObject<UComposableCameraShotAsset>(GetTransientPackage(), NAME_None, RF_Transactional | RF_Transient);
	Bind(&Scratch->Shot, Scratch);
}

void FComposableCameraShotAuthoringSession::AbortBindingEdit()
{
	// Binding actions reject an existing transaction before starting. Revert all bindings created by this action.
	Transaction.Reset();
	bGestureChanged = false;
	bStructuralChange = false;
	if (GEditor) GEditor->UndoTransaction();
	bPreviewPending = true;
	if (OnChanged) OnChanged(true);
}

bool FComposableCameraShotAuthoringSession::BindTargetsToSection(UMovieSceneComposableCameraShotSection& Section, TConstArrayView<AActor*> Actors, FString& Reason)
{
	TSharedPtr<ISequencer> Sequencer = GetSequencer();
	if (!Sequencer || Sequencer->GetFocusedMovieSceneSequence() != GetSequence()) return false;
	TArray<FComposableCameraShotTargetActorOverride> Bindings;
	for (int32 Index = 0; Index < Actors.Num(); ++Index)
	{
		FMovieSceneObjectBindingID Binding;
		if (!ResolveSubjectBinding(*Sequencer, Actors[Index], true, Binding, Reason)) return false;
		FComposableCameraShotTargetActorOverride& Override = Bindings.AddDefaulted_GetRef();
		Override.TargetIndex = Index;
		Override.Binding = Binding;
	}
	Section.TargetActorOverrides = MoveTemp(Bindings);
	return true;
}

bool FComposableCameraShotAuthoringSession::UseSelectedActors(EComposableShotTemplate Template, FString& Reason)
{
	if (GetHost() && !GetShot()) { Reason = TEXT("The current source has no editable shot. Choose its asset or switch the section to Inline."); return false; }
	if (GetShot() && !CanEdit()) { Reason = TEXT("The current shot or sequence is locked."); return false; }
	if (GetSection() && (!GetSequencer() || GetSequencer()->GetFocusedMovieSceneSequence() != GetSequence()))
	{ Reason = TEXT("Open and focus the owning sequence before assigning subjects."); return false; }
	TArray<AActor*> Actors;
	if (GEditor)
	{
		for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It)
		{
			if (AActor* Actor = Cast<AActor>(*It)) Actors.Add(Actor);
		}
	}
	if (!CanUseTemplate(Template, Actors.Num()))
	{
		Reason = TEXT("Select one actor for single shots, two for pair shots, or at least two for group shots.");
		return false;
	}
	if (GetSection())
	{
		if (!GEditor || GEditor->IsTransactionActive()) { Reason = TEXT("Finish the current edit gesture before assigning subjects."); return false; }
		for (AActor* Actor : Actors)
		{
			FMovieSceneObjectBindingID Binding;
			if (!ResolveSubjectBinding(*GetSequencer(), Actor, false, Binding, Reason)) return false;
		}
	}
	TArray<FComposableCameraShotTarget> Targets;
	for (AActor* Actor : Actors) Targets.Add(MakeTarget(Actor));
	FComposableCameraShot Result;
	if (!BuildTemplate(Template, Targets, Result)) return false;
	EnsureScratch();
	BeginEdit(LOCTEXT("Selection", "Create Shot From Selected Actors"));
	if (UMovieSceneComposableCameraShotSection* Section = GetSection())
	{
		if (!BindTargetsToSection(*Section, Actors, Reason)) { AbortBindingEdit(); return false; }
		for (auto& Target : Result.Targets) Target.Target.Actor.Reset();
	}
	*GetShot() = MoveTemp(Result);
	Changed(true);
	EndEdit();
	return true;
}

bool FComposableCameraShotAuthoringSession::ApplyTemplate(EComposableShotTemplate Template, FString& Reason)
{
	if (!CanEdit()) { Reason = TEXT("The current shot or sequence is locked."); return false; }
	FComposableCameraShot* Shot = GetShot();
	FComposableCameraShot Result;
	TArray<FComposableCameraShotTarget> Targets;
	if (Shot) { Targets = Shot->Targets; for (int32 Index = 0; Index < Targets.Num(); ++Index) Targets[Index].Target.Actor = ResolveTarget(Index); }
	if (!Shot || !BuildTemplate(Template, Targets, Result))
	{
		Reason = TEXT("This template needs a matching subject count. Pick subjects first.");
		return false;
	}
	BeginEdit(LOCTEXT("Template", "Apply Shot Template"));
	if (GetSection()) for (auto& Target : Result.Targets) Target.Target.Actor.Reset();
	*Shot = MoveTemp(Result);
	Changed(true);
	EndEdit();
	return true;
}

bool FComposableCameraShotAuthoringSession::CanAddTargets() const
{
	return (!GetHost() || CanEdit()) && !IsEditing() && GEditor && !GEditor->IsTransactionActive();
}

bool FComposableCameraShotAuthoringSession::AddTarget(AActor* Actor, FString& Reason)
{
	return AppendTargets(MakeArrayView(&Actor, 1), Reason);
}

bool FComposableCameraShotAuthoringSession::AddSelectedTargets(FString& Reason)
{
	TArray<AActor*> Actors;
	if (GEditor)
		for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It)
			if (AActor* Actor = Cast<AActor>(*It); IsValid(Actor)) Actors.Add(Actor);
	if (Actors.IsEmpty()) { Reason = TEXT("Select actors in the level, then Add Selected Actors."); return false; }
	return AppendTargets(Actors, Reason);
}

bool FComposableCameraShotAuthoringSession::AppendTargets(TConstArrayView<AActor*> Actors, FString& Reason)
{
	Reason.Reset();
	if (Actors.IsEmpty()) { Reason = TEXT("Choose subjects to append."); return false; }
	if (!GEditor || IsEditing() || GEditor->IsTransactionActive())
	{ Reason = TEXT("Finish the current edit gesture before adding subjects."); return false; }
	if (GetHost() && !GetShot())
	{ Reason = TEXT("The current source has no editable shot. Choose its asset or switch the section to Inline."); return false; }
	if (GetShot() && !CanEdit()) { Reason = TEXT("The current shot or sequence is locked."); return false; }
	UMovieSceneComposableCameraShotSection* Section = GetSection();
	const TSharedPtr<ISequencer> Sequencer = Section ? GetSequencer() : nullptr;
	// Validate every binding before creating any. Empty slots need no open Sequencer.
	for (AActor* Actor : Actors)
	{
		if (!Actor) continue;
		if (!IsValid(Actor)) { Reason = TEXT("Choose valid subject actors."); return false; }
		if (Section)
		{
			if (!Sequencer || Sequencer->GetFocusedMovieSceneSequence() != GetSequence())
			{ Reason = TEXT("Open and focus the owning sequence before assigning subjects."); return false; }
			FMovieSceneObjectBindingID Binding;
			if (!ResolveSubjectBinding(*Sequencer, Actor, false, Binding, Reason)) return false;
		}
	}
	TArray<FComposableCameraShotTarget> Targets;
	Targets.Reserve(Actors.Num());
	for (AActor* Actor : Actors) Targets.Add(Actor ? MakeTarget(Actor) : FComposableCameraShotTarget());
	EnsureScratch();
	const int32 FirstIndex = GetShot()->Targets.Num();
	BeginEdit(LOCTEXT("AddSubjects", "Add Shot Subjects"));
	TArray<FComposableCameraShotTargetActorOverride> Bindings;
	if (Section)
	{
		Bindings.Reserve(Actors.Num());
		for (int32 Offset = 0; Offset < Actors.Num(); ++Offset)
		{
			Targets[Offset].Target.Actor.Reset();
			if (!Actors[Offset]) continue;
			FMovieSceneObjectBindingID Binding;
			if (!ResolveSubjectBinding(*Sequencer, Actors[Offset], true, Binding, Reason)) { AbortBindingEdit(); return false; }
			auto& Override = Bindings.AddDefaulted_GetRef();
			Override.TargetIndex = FirstIndex + Offset;
			Override.Binding = Binding;
		}
		// An old out-of-range override must not attach itself to a newly added empty slot.
		const int32 EndIndex = FirstIndex + Actors.Num();
		Section->TargetActorOverrides.RemoveAll([FirstIndex, EndIndex](const auto& Override)
			{ return Override.TargetIndex >= FirstIndex && Override.TargetIndex < EndIndex; });
		Section->TargetActorOverrides.Append(MoveTemp(Bindings));
	}
	GetShot()->Targets.Append(MoveTemp(Targets));
	Changed(true);
	EndEdit();
	return true;
}

bool FComposableCameraShotAuthoringSession::CanRemoveTarget(int32 Index) const
{
	const auto* Shot = GetShot();
	return CanAddTargets() && Shot && Shot->Targets.IsValidIndex(Index);
}

bool FComposableCameraShotAuthoringSession::RemoveTarget(int32 Index, FString& Reason)
{
	Reason.Reset();
	if (!CanRemoveTarget(Index))
	{
		Reason = TEXT("Choose an existing subject on an unlocked shot and finish the current edit gesture.");
		return false;
	}
	const int32 OldCount = GetShot()->Targets.Num();
	BeginEdit(LOCTEXT("RemoveSubject", "Remove Shot Subject"));
	ComposableCameraSystem::ShotAuthoring::RemoveTarget(*GetShot(), Index);
	if (auto* Section = GetSection())
	{
		Section->TargetActorOverrides.RemoveAll([Index, OldCount](const auto& Override)
			{ return Override.TargetIndex == Index || Override.TargetIndex < 0 || Override.TargetIndex >= OldCount; });
		for (auto& Override : Section->TargetActorOverrides)
			if (Override.TargetIndex > Index) --Override.TargetIndex;
	}
	Changed(true);
	EndEdit();
	return true;
}

bool FComposableCameraShotAuthoringSession::SetTargetActor(int32 Index, AActor* Actor, FString& Reason)
{
	if (!CanEdit()) { Reason = TEXT("The current shot or sequence is locked."); return false; }
	FComposableCameraShot* Shot = GetShot();
	if (!Shot || !Shot->Targets.IsValidIndex(Index)) return false;
	if (GetSection() && (!GetSequencer() || GetSequencer()->GetFocusedMovieSceneSequence() != GetSequence() || !Actor))
	{
		Reason = TEXT("Open the owning sequence and pick an actor to create its subject binding.");
		return false;
	}
	if (GetSection())
	{
		if (!GEditor || GEditor->IsTransactionActive()) { Reason = TEXT("Finish the current edit gesture before assigning subjects."); return false; }
		FMovieSceneObjectBindingID Binding;
		if (!ResolveSubjectBinding(*GetSequencer(), Actor, false, Binding, Reason)) return false;
	}
	BeginEdit(LOCTEXT("Subject", "Set Shot Subject"));
	if (UMovieSceneComposableCameraShotSection* Section = GetSection())
	{
		FMovieSceneObjectBindingID Binding;
		if (!ResolveSubjectBinding(*GetSequencer(), Actor, true, Binding, Reason)) { AbortBindingEdit(); return false; }
		Section->TargetActorOverrides.RemoveAll([Index](const auto& Override) { return Override.TargetIndex == Index; });
		FComposableCameraShotTargetActorOverride& Override = Section->TargetActorOverrides.AddDefaulted_GetRef();
		Override.TargetIndex = Index;
		Override.Binding = Binding;
	}
	Shot->Targets[Index] = MakeTarget(Actor);
	if (GetSection()) Shot->Targets[Index].Target.Actor.Reset();
	Changed(true);
	EndEdit();
	return true;
}

bool FComposableCameraShotAuthoringSession::MoveTarget(int32 From, int32 To)
{
	FComposableCameraShot* Shot = GetShot();
	if (!CanEdit() || !Shot || !Shot->Targets.IsValidIndex(From) || !Shot->Targets.IsValidIndex(To) || From == To) return false;
	BeginEdit(LOCTEXT("Move", "Reorder Shot Subjects"));
	ComposableCameraSystem::ShotAuthoring::MoveTarget(*Shot, From, To);
	if (UMovieSceneComposableCameraShotSection* Section = GetSection())
	{
		for (FComposableCameraShotTargetActorOverride& Override : Section->TargetActorOverrides)
		{
			int32& Index = Override.TargetIndex;
			if (Index == From) Index = To;
			else if (From < To && Index > From && Index <= To) --Index;
			else if (To < From && Index >= To && Index < From) ++Index;
		}
	}
	Changed(true);
	EndEdit();
	return true;
}

bool FComposableCameraShotAuthoringSession::SwapSubjects()
{
	FComposableCameraShot* Shot = GetShot();
	if (!CanEdit() || !Shot || Shot->Targets.Num() != 2) return false;
	BeginEdit(LOCTEXT("Swap", "Swap Shot Subjects A and B"));
	Shot->Targets.Swap(0, 1); // Roles stay fixed; actors take the opposite role.
	if (UMovieSceneComposableCameraShotSection* Section = GetSection())
	{
		for (FComposableCameraShotTargetActorOverride& Override : Section->TargetActorOverrides)
			if (Override.TargetIndex == 0 || Override.TargetIndex == 1) Override.TargetIndex = 1 - Override.TargetIndex;
	}
	Changed(true);
	EndEdit();
	return true;
}

void FComposableCameraShotAuthoringSession::Mirror()
{
	if (!CanEdit()) return;
	if (FComposableCameraShot* Shot = GetShot())
	{
		BeginEdit(LOCTEXT("Mirror", "Mirror Shot Composition"));
		Shot->Placement.LocalCameraDirection.X = FMath::UnwindDegrees(-Shot->Placement.LocalCameraDirection.X);
		Shot->Placement.ScreenPosition.X *= -1.f;
		Shot->Aim.ScreenPosition.X *= -1.f;
		Shot->Roll *= -1.f;
		Swap(Shot->Aim.AimZones.DeadZone.Left, Shot->Aim.AimZones.DeadZone.Right);
		Swap(Shot->Aim.AimZones.SoftZone.Left, Shot->Aim.AimZones.SoftZone.Right);
		Swap(Shot->Placement.PlacementZones.DeadZone.Left, Shot->Placement.PlacementZones.DeadZone.Right);
		Swap(Shot->Placement.PlacementZones.SoftZone.Left, Shot->Placement.PlacementZones.SoftZone.Right);
		Changed();
		EndEdit();
	}
}

bool FComposableCameraShotAuthoringSession::ApplyPreset(UComposableCameraShotAsset* Preset, FString& Reason)
{
	if (!Preset) { Reason = TEXT("Choose a Shot preset first."); return false; }
	if (GetHost() && !CanEdit()) { Reason = TEXT("The current source is locked or has no editable shot."); return false; }
	if (GetShot() && !GetShot()->Targets.IsEmpty() && GetShot()->Targets.Num() != Preset->Shot.Targets.Num())
	{ Reason = TEXT("Preset subject count differs. Assign the matching subjects before applying it."); return false; }
	EnsureScratch();
	BeginEdit(LOCTEXT("Preset", "Apply Shot Preset"));
	FComposableCameraShot* Shot = GetShot();
	const TArray<FComposableCameraShotTarget> ExistingTargets = Shot->Targets;
	*Shot = Preset->Shot;
	// Role slots preserve this sequence's actor identities, bones and local offsets.
	for (int32 Index = 0; Index < FMath::Min(ExistingTargets.Num(), Shot->Targets.Num()); ++Index)
		Shot->Targets[Index].Target = ExistingTargets[Index].Target;
	if (UMovieSceneComposableCameraShotSection* Section = GetSection())
	{
		Section->TargetActorOverrides.RemoveAll([Shot](const auto& Override) { return !Shot->Targets.IsValidIndex(Override.TargetIndex); });
		for (auto& Target : Shot->Targets) Target.Target.Actor.Reset();
	}
	if (ExistingTargets.IsEmpty()) bUseLevelWorld = false;
	Changed(true);
	EndEdit();
	return true;
}

UComposableCameraShotAsset* FComposableCameraShotAuthoringSession::SavePreset()
{
	if (!GetShot()) return nullptr;
	FComposableCameraShot Copy = *GetShot();
#if WITH_EDITORONLY_DATA
	for (int32 Index = 0; Index < Copy.Targets.Num(); ++Index)
	{
		if (AActor* Actor = ResolveTarget(Index))
		{
			USkeletalMeshComponent* Mesh = nullptr;
			for (UActorComponent* Component : Actor->GetComponents())
				if (auto* Candidate = Cast<USkeletalMeshComponent>(Component); Candidate && (Copy.Targets[Index].Target.ComponentName.IsNone() || Candidate->GetFName() == Copy.Targets[Index].Target.ComponentName)) { Mesh = Candidate; break; }
			Copy.Targets[Index].Target.EditorPreviewMesh = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
			Copy.Targets[Index].Target.EditorPreviewTransform = Actor->GetActorTransform();
			Copy.Targets[Index].Target.EditorPreviewMeshRelativeTransform = Mesh ? Mesh->GetComponentTransform().GetRelativeTransform(Actor->GetActorTransform()) : FTransform::Identity;
		}
		Copy.Targets[Index].Target.Actor.Reset();
	}
#endif
	UComposableCameraShotAssetFactory* Factory = NewObject<UComposableCameraShotAssetFactory>();
	UComposableCameraShotAsset* Asset = Cast<UComposableCameraShotAsset>(FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools")
		.Get().CreateAssetWithDialog(UComposableCameraShotAsset::StaticClass(), Factory));
	if (Asset) { Asset->Shot = MoveTemp(Copy); Asset->MarkPackageDirty(); }
	return Asset;
}

void FComposableCameraShotAuthoringSession::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Scratch);
}
#undef LOCTEXT_NAMESPACE
