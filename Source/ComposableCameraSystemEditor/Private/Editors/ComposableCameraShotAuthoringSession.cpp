// Copyright 2026 Sulley. All Rights Reserved.
#include "Editors/ComposableCameraShotAuthoringSession.h"
#include "Editors/ComposableCameraInstanceEditorSession.h"
#include "Editors/ComposableCameraInstanceDraftPreview.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "DataAssets/ComposableCameraPatchTypeAsset.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "MovieScene/MovieSceneComposableCameraInstanceTrack.h"
#include "MovieScene/MovieSceneComposableCameraInstanceSection.h"
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
#include "MovieSceneCommonHelpers.h"
#include "MovieScene/MovieSceneComposableCameraShotSection.h"
#include "MovieScene/MovieSceneComposableCameraShotTrack.h"
#include "MovieSceneSequence.h"
#include "LevelSequence.h"
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
	DraftPreview.Reset();
	bDraftModified = false;
	if (InHost != SequenceDraft.Get()) { SequenceDraft = nullptr; DraftSequence = nullptr; }
	Host = InHost;
	DraftName = FText::GetEmpty();
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
	InstanceEditor.Reset();
	if (auto* Track = GetInstanceTrack())
	{
		InstanceEditor = MakeUnique<FComposableCameraInstanceEditorSession>();
		InstanceEditor->Bind(Track, GetSequencer());
	}
	bPreviewPending = true;
	bUseLevelWorld = GetSequence() != nullptr || InHost == Scratch.Get();
}

bool FComposableCameraShotAuthoringSession::StartSequenceDraft(TSharedRef<ISequencer> Sequencer, FString& Reason)
{
	return StartTemplateDraft(FComposableCameraShot(), LOCTEXT("EmptyDraft", "Shot Draft"), Sequencer, Reason);
}

bool FComposableCameraShotAuthoringSession::StartTemplateDraft(const FComposableCameraShot& InShot, const FText& Name,
	TSharedPtr<ISequencer> Sequencer, FString& Reason)
{
	Reason.Reset();
	auto* Sequence = Sequencer ? Cast<ULevelSequence>(Sequencer->GetFocusedMovieSceneSequence()) : nullptr;
	if (Sequencer && (!Sequence || !Sequence->GetMovieScene() || Sequence->GetMovieScene()->IsReadOnly() || Sequencer->IsReadOnly()
		|| !Sequence->AllowsSpawnableObjects() || !Sequencer->GetPlaybackContext()))
	{ Reason = TEXT("Open and focus an editable Level Sequence with Spawnable support."); return false; }
	if (IsEditing() || (GEditor && GEditor->IsTransactionActive()))
	{ Reason = TEXT("Finish the current edit gesture before creating a draft."); return false; }
	const FText NewDraftName = Name.IsEmpty() ? LOCTEXT("EmptyDraft", "Shot Draft") : Name;
	FComposableCameraShot Shot = InShot;
	for (auto& Target : Shot.Targets)
	{
		// Templates provide roles/pivots, not actors or animation identities from another instance.
		Target.SubjectId = FGuid::NewGuid();
		Target.Target.Actor.Reset();
		Target.CachedAutoBoundsExtent = FVector::ZeroVector;
		Target.CachedBoundsMeshComponent.Reset();
		Target.CachedBoundsSourceComponentName = NAME_None;
	}
	DraftSequence = Sequence;
	if (Sequence)
	{
		SequenceDraft = NewObject<UComposableCameraInstanceData>(GetTransientPackage(), NAME_None, RF_Transactional | RF_Transient);
		SequenceDraft->InitializeFromShot(Shot);
		Bind(&SequenceDraft->Shot, SequenceDraft);
	}
	else
	{
		SequenceDraft = nullptr;
		Scratch = NewObject<UComposableCameraShotAsset>(GetTransientPackage(), NAME_None, RF_Transactional | RF_Transient);
		Scratch->Shot = MoveTemp(Shot);
		Bind(&Scratch->Shot, Scratch);
	}
	DraftName = NewDraftName;
	CachedSequencer = Sequencer;
	if (OnChanged) OnChanged(true);
	return true;
}

bool FComposableCameraShotAuthoringSession::StartTypeDraft(UComposableCameraTypeAsset* Type, TSharedPtr<ISequencer> Sequencer, FString& Reason)
{
	Reason.Reset();
	auto* Sequence = Sequencer ? Cast<ULevelSequence>(Sequencer->GetFocusedMovieSceneSequence()) : nullptr;
	if (!Type || Type->IsA<UComposableCameraPatchTypeAsset>())
	{ Reason = TEXT("Choose a Camera Type asset. Camera Patches use their own workflow."); return false; }
	if (!Sequence || !Sequence->GetMovieScene() || Sequence->GetMovieScene()->IsReadOnly() || Sequencer->IsReadOnly()
		|| !Sequence->AllowsSpawnableObjects() || !Sequencer->GetPlaybackContext())
	{ Reason = TEXT("Open and focus an editable Level Sequence before creating a Camera Type draft."); return false; }
	if (IsEditing() || (GEditor && GEditor->IsTransactionActive()))
	{ Reason = TEXT("Finish the current edit gesture before creating a draft."); return false; }
	DraftSequence = Sequence;
	SequenceDraft = NewObject<UComposableCameraInstanceData>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional);
	SequenceDraft->InitializeFromType(Type);
	Bind(nullptr, SequenceDraft);
	DraftName = FText::Format(LOCTEXT("TypeDraftName", "{0} Draft"), FText::FromString(Type->GetName()));
	CachedSequencer = Sequencer;
	DraftPreview = MakeUnique<FComposableCameraInstanceDraftPreview>();
	if (OnChanged) OnChanged(true);
	return true;
}

bool FComposableCameraShotAuthoringSession::IsSequenceDraft() const
{
	return SequenceDraft && Host.Get() == SequenceDraft.Get();
}

FComposableCameraShot* FComposableCameraShotAuthoringSession::GetShot() const
{
	UObject* Object = Host.Get();
	if (!Object) return nullptr;
	if (InstanceEditor)
	{
		auto* Values = InstanceEditor->GetValues();
		return Values && Values->bComposition && GetInstanceTrack() && GetInstanceTrack()->IsInMovieScene() ? &Values->Shot : nullptr;
	}
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

UMovieSceneComposableCameraInstanceTrack* FComposableCameraShotAuthoringSession::GetInstanceTrack() const
{
	const auto* Data = Cast<UComposableCameraInstanceData>(Host.Get());
	return Data ? Data->GetTypedOuter<UMovieSceneComposableCameraInstanceTrack>() : nullptr;
}
UComposableCameraInstanceData* FComposableCameraShotAuthoringSession::GetInstanceValues() const
{
	return InstanceEditor ? InstanceEditor->GetValues() : IsSequenceDraft() ? SequenceDraft.Get() : nullptr;
}
bool FComposableCameraShotAuthoringSession::IsGenericInstance() const
{
	const auto* Values = GetInstanceValues();
	return Values && !Values->bComposition;
}

FText FComposableCameraShotAuthoringSession::GetGenericStatus() const
{
	const auto* Values = GetInstanceValues();
	if (!Values || Values->bComposition) return FText::GetEmpty();
	if (LastGenericError != Values->GetTypeInterfaceError())
	{
		LastGenericError = Values->GetTypeInterfaceError();
		CachedGenericError = FText::FromString(LastGenericError);
	}
	if (!CachedGenericError.IsEmpty()) return CachedGenericError;
	const auto Seq = GetSequencer();
	if (!Seq || Seq->GetFocusedMovieSceneSequence() != GetSequence())
		return LOCTEXT("GenericSequenceUnavailable", "Open and focus this camera's Level Sequence to edit and preview it.");
	if (!Values->IsTypeInterfaceCurrent()) return LOCTEXT("GenericRefresh", "Camera Type changed. Finish the current edit to refresh its interface.");
	if (IsSequenceDraft()) return DraftPreview ? DraftPreview->GetStatus() : LOCTEXT("GenericStarting", "Preparing camera preview.");
	if (const auto* Track = GetInstanceTrack(); !Track || !Track->IsInMovieScene())
		return LOCTEXT("GenericRemoved", "Camera instance was removed. Undo or open another instance.");
	if (!GetOutputCamera()) return LOCTEXT("GenericInactive", "No valid output at this time. Check Activation, Actor inputs and the output binding.");
	if (Values->Camera.TypeAsset)
	{
		bool bRequiresPCM = false;
		for (const UComposableCameraCameraNodeBase* Node : Values->Camera.TypeAsset->NodeTemplates)
			if (Node && Node->GetLevelSequenceCompatibility() == EComposableCameraNodeLevelSequenceCompatibility::RequiresPCM) bRequiresPCM = true;
		if (bRequiresPCM || !Values->Camera.TypeAsset->ComputeNodeTemplates.IsEmpty())
			return LOCTEXT("GenericCompatibility", "Level Sequence preview: PCM-dependent nodes and BeginPlay compute logic are unavailable.");
	}
	return FText::GetEmpty();
}

const UClass* FComposableCameraShotAuthoringSession::GetInputActorClass(FName Input) const
{
	auto* Values = GetInstanceValues();
	if (!Values || !Values->IsTypeInterfaceCurrent() || !Values->IsTypeInterfaceValid()) return nullptr;
	TArray<FComposableCameraInstanceField> Fields;
	Values->GatherFields(Fields);
	const auto* Field = Fields.FindByPredicate([Input](const auto& Item) { return Item.Path == Input; });
	const auto* Property = Field ? CastField<FObjectPropertyBase>(Field->Property) : nullptr;
	return Property && Property->PropertyClass->IsChildOf(AActor::StaticClass()) ? Property->PropertyClass.Get() : nullptr;
}

FMovieSceneObjectBindingID FComposableCameraShotAuthoringSession::GetInputBinding(FName Input) const
{
	if (const auto* Values = GetInstanceValues())
		for (const auto& Subject : Values->Subjects) if (Subject.Input == Input) return Subject.Binding;
	return FMovieSceneObjectBindingID();
}

AActor* FComposableCameraShotAuthoringSession::ResolveInputActor(FName Input) const
{
	const auto Binding = GetInputBinding(Input);
	const auto Seq = GetSequencer();
	if (Seq && Binding.IsValid() && Seq->GetFocusedMovieSceneSequence() == GetSequence())
		for (const auto Object : Binding.ResolveBoundObjects(Seq->GetFocusedTemplateID(), *Seq))
			if (auto* Actor = Cast<AActor>(Object.Get())) return Actor;
	return nullptr;
}

void FComposableCameraShotAuthoringSession::WriteInputBinding(FName Input, const FMovieSceneObjectBindingID& Binding)
{
	auto* Values = GetInstanceValues();
	Values->RefreshSubjectSlots();
	for (auto& Subject : Values->Subjects) if (Subject.Input == Input) Subject.Binding = Binding;
	TArray<FComposableCameraInstanceField> Fields; Values->GatherFields(Fields);
	for (const auto& Field : Fields) if (Field.Path == Input) Field.WriteObject(nullptr);
	Changed();
}

bool FComposableCameraShotAuthoringSession::SetInputBinding(FName Input, const FGuid& BindingGuid, FString& Reason)
{
	Reason.Reset();
	const auto Seq = GetSequencer();
	const UClass* RequiredClass = GetInputActorClass(Input);
	if (!CanEdit() || !Seq || Seq->GetFocusedMovieSceneSequence() != GetSequence() || !RequiredClass)
	{ Reason = TEXT("Choose an editable Actor input in the focused Level Sequence."); return false; }
	if (IsEditing() || !GEditor || GEditor->IsTransactionActive())
	{ Reason = TEXT("Finish the current gesture before assigning an input."); return false; }
	const UClass* BoundClass = BindingGuid.IsValid() ? MovieSceneHelpers::GetBoundObjectClass(GetSequence(), BindingGuid) : nullptr;
	if (BindingGuid.IsValid() && (!BoundClass || !BoundClass->IsChildOf(RequiredClass)))
	{ Reason = TEXT("The binding does not match this Actor input's class."); return false; }
	const FMovieSceneObjectBindingID Binding = BindingGuid.IsValid()
		? FMovieSceneObjectBindingID(UE::MovieScene::FRelativeObjectBindingID(BindingGuid)) : FMovieSceneObjectBindingID();
	if (GetInputBinding(Input) == Binding) return true;
	BeginEdit(LOCTEXT("BindInput", "Bind Camera Actor Input"));
	WriteInputBinding(Input, Binding);
	EndEdit();
	return true;
}

bool FComposableCameraShotAuthoringSession::SetInputActor(FName Input, AActor* Actor, FString& Reason)
{
	if (!Actor) return SetInputBinding(Input, FGuid(), Reason);
	Reason.Reset();
	const auto Seq = GetSequencer();
	const UClass* RequiredClass = GetInputActorClass(Input);
	if (!CanEdit() || !RequiredClass || !Actor->IsA(RequiredClass) || !Seq
		|| Seq->GetFocusedMovieSceneSequence() != GetSequence() || IsEditing() || !GEditor || GEditor->IsTransactionActive())
	{ Reason = TEXT("Choose a compatible Actor in the focused editable Level Sequence."); return false; }
	FMovieSceneObjectBindingID Binding;
	if (!ResolveSubjectBinding(*Seq, Actor, false, Binding, Reason)) return false;
	BeginEdit(LOCTEXT("BindInputActor", "Bind Camera Actor Input"));
	if (!ResolveSubjectBinding(*Seq, Actor, true, Binding, Reason)) { AbortBindingEdit(); return false; }
	WriteInputBinding(Input, Binding);
	EndEdit();
	return true;
}

bool FComposableCameraShotAuthoringSession::CanEdit() const
{
	if (InstanceEditor) return InstanceEditor->CanEdit();
	if (IsSequenceDraft())
	{
		if (!SequenceDraft->IsTypeInterfaceCurrent() || !SequenceDraft->IsTypeInterfaceValid()) return false;
		const auto Seq = GetSequencer();
		return DraftSequence && DraftSequence->GetMovieScene() && !DraftSequence->GetMovieScene()->IsReadOnly()
			&& Seq && !Seq->IsReadOnly() && Seq->GetFocusedMovieSceneSequence() == DraftSequence;
	}
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
	if (IsSequenceDraft()) return DraftSequence;
	if (auto* Track = GetInstanceTrack()) return Track->GetTypedOuter<UMovieSceneSequence>();
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
	if (GetInstanceValues())
	{
		const auto Binding = GetTargetBinding(Index);
		if (const auto Seq = GetSequencer(); Seq && Binding.IsValid())
			for (const auto Object : Binding.ResolveBoundObjects(Seq->GetFocusedMovieSceneSequence() == GetSequence()
				? Seq->GetFocusedTemplateID() : MovieSceneSequenceID::Root, *Seq))
				if (auto* Actor = Cast<AActor>(Object.Get())) return Actor;
		return nullptr;
	}
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
	if (IsGenericInstance())
		if (const auto* Values = GetInstanceValues(); !Values || !Values->IsTypeInterfaceCurrent() || !Values->IsTypeInterfaceValid()) return nullptr;
	if (IsSequenceDraft() && IsGenericInstance()) return DraftPreview ? DraftPreview->GetCamera() : nullptr;
	if (auto* Track = GetInstanceTrack())
	{
		const auto Seq = GetSequencer();
		return Track->IsInMovieScene() && Seq && UMovieSceneComposableCameraInstanceTrack::FindActiveSection(*Track, Seq->GetLocalTime().Time) ? GetConfiguredCamera(false) : nullptr;
	}
	UMovieSceneComposableCameraShotSection* Section = GetSection();
	TSharedPtr<ISequencer> Sequencer = GetSequencer();
	if (!Section || !Sequencer || Sequencer->GetFocusedMovieSceneSequence() != GetSequence()
		|| !Section->IsActive() || !Section->GetRange().Contains(Sequencer->GetLocalTime().Time.FloorToFrame())) return nullptr;
	return GetConfiguredCamera(false);
}

UCineCameraComponent* FComposableCameraShotAuthoringSession::GetConfiguredCamera(bool bAllowTemplate) const
{
	if (IsSequenceDraft() && IsGenericInstance()) return DraftPreview ? DraftPreview->GetCamera(!bAllowTemplate) : nullptr;
	UMovieSceneComposableCameraShotSection* Section = GetSection();
	TSharedPtr<ISequencer> Sequencer = GetSequencer();
	if ((!Section && !GetInstanceTrack()) || !Sequencer || Sequencer->GetFocusedMovieSceneSequence() != GetSequence()) return nullptr;
	const UMovieSceneTrack* Track = GetInstanceTrack() ? GetInstanceTrack() : Section->GetTypedOuter<UMovieSceneTrack>();
	if (!Track) return nullptr;
	for (TWeakObjectPtr<UObject> Object : Sequencer->FindBoundObjects(Track->FindObjectBindingGuid(), Sequencer->GetFocusedTemplateID()))
	{
		if (AActor* Actor = Cast<AActor>(Object.Get()))
		{
			if (UComposableCameraLevelSequenceComponent* Component = Actor->FindComponentByClass<UComposableCameraLevelSequenceComponent>())
			{
				if (!bAllowTemplate && IsGenericInstance() && !Component->HasCameraInstanceResult()) return nullptr;
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
	if (IsSequenceDraft()) bDraftModified = true;
	if (IsSequenceDraft() && bStructural) { SequenceDraft->EnsureSubjectIdentities(); SequenceDraft->RefreshSubjectSlots(); }
	if (InstanceEditor) InstanceEditor->Commit(bStructural);
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
	if (InstanceEditor) return;
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
	if (InstanceEditor) { InstanceEditor->Commit(); return; }
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
	if (DraftPreview && DraftPreview->IsSubjectObject(Object)) bPreviewPending = true;
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
	if (IsSequenceDraft() && IsGenericInstance())
	{
		const auto Seq = GetSequencer();
		if (!Seq || Seq->GetFocusedMovieSceneSequence() != DraftSequence)
		{
			DraftPreview.Reset(); bPreviewPending = true;
			return;
		}
		// Invalidate property handles only between gestures. Keep old bags on a
		// destructive interface change, including changes discovered after load.
		if (!SequenceDraft->IsTypeInterfaceCurrent())
		{
			if (IsEditing() || (GEditor && GEditor->IsTransactionActive())) return;
			SequenceDraft->RefreshTypeInterface(); bPreviewPending = true;
			if (OnChanged) OnChanged(true);
		}
		if (!DraftPreview) DraftPreview = MakeUnique<FComposableCameraInstanceDraftPreview>();
		DraftPreview->Update(*SequenceDraft, *Seq, bPreviewPending);
		bPreviewPending = false;
		return;
	}
	if (InstanceEditor)
	{
		if (!IsEditing() && (!GEditor || !GEditor->IsTransactionActive()))
		{
			// Preview/Subjects already rediscover the live Sequencer. The sampled
			// parameter view must follow the same connection after an LS reopen.
			InstanceEditor->UpdateSequencer(GetSequencer());
			const auto Change = InstanceEditor->Refresh();
			if (Change != FComposableCameraInstanceEditorSession::ERefreshResult::None && OnChanged)
				OnChanged(Change == FComposableCameraInstanceEditorSession::ERefreshResult::Source);
		}
		return;
	}
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

bool FComposableCameraShotAuthoringSession::CanAddTargets() const
{
	return !IsGenericInstance() && (!GetHost() || CanEdit()) && !IsEditing() && GEditor && !GEditor->IsTransactionActive();
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
	const TSharedPtr<ISequencer> Sequencer = GetSequence() ? GetSequencer() : nullptr;
	// Validate every binding before creating any. Empty slots need no open Sequencer.
	for (AActor* Actor : Actors)
	{
		if (!Actor) continue;
		if (!IsValid(Actor)) { Reason = TEXT("Choose valid subject actors."); return false; }
		if (GetSequence())
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
	TArray<FMovieSceneObjectBindingID> InstanceBindings;
	if (GetInstanceValues())
	{
		for (int32 Offset = 0; Offset < Actors.Num(); ++Offset)
		{
			FMovieSceneObjectBindingID Binding;
			if (Actors[Offset] && !ResolveSubjectBinding(*Sequencer, Actors[Offset], true, Binding, Reason)) { AbortBindingEdit(); return false; }
			InstanceBindings.Add(Binding);
			Targets[Offset].Target.Actor.Reset();
		}
	}
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
	if (auto* Values = GetInstanceValues())
	{
		Values->EnsureSubjectIdentities(); Values->RefreshSubjectSlots();
		for (int32 Offset = 0; Offset < InstanceBindings.Num(); ++Offset)
		{
			const FName Path(*(TEXT("Shot.Targets.") + Values->Shot.Targets[FirstIndex + Offset].SubjectId.ToString(EGuidFormats::Digits) + TEXT(".Target.Actor")));
			for (auto& Slot : Values->Subjects) if (Slot.Input == Path) Slot.Binding = InstanceBindings[Offset];
		}
	}
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

FMovieSceneObjectBindingID FComposableCameraShotAuthoringSession::GetTargetBinding(int32 Index) const
{
	if (const auto* Values = GetInstanceValues(); Values && Values->Shot.Targets.IsValidIndex(Index))
	{
		const FName Path(*FString::Printf(TEXT("Shot.Targets.%s.Target.Actor"), *Values->Shot.Targets[Index].SubjectId.ToString(EGuidFormats::Digits)));
		for (const auto& Subject : Values->Subjects) if (Subject.Input == Path) return Subject.Binding;
	}
	if (const auto* Section = GetSection())
		for (const auto& Override : Section->TargetActorOverrides)
			if (Override.TargetIndex == Index) return Override.Binding;
	return FMovieSceneObjectBindingID();
}

bool FComposableCameraShotAuthoringSession::SetTargetBinding(int32 Index, const FGuid& BindingGuid, FString& Reason)
{
	Reason.Reset();
	if (!CanEdit()) { Reason = TEXT("The current shot or sequence is locked."); return false; }
	UMovieSceneComposableCameraShotSection* Section = GetSection();
	FComposableCameraShot* Shot = GetShot();
	if ((!Section && !GetInstanceValues()) || !Shot || !Shot->Targets.IsValidIndex(Index))
	{ Reason = TEXT("Choose a valid Subject in a Shot Section."); return false; }
	if (!GEditor || IsEditing() || GEditor->IsTransactionActive())
	{ Reason = TEXT("Finish the current edit gesture before assigning subjects."); return false; }
	// Inspect authored metadata, including UE5.6 custom Spawnable bindings.
	// Never spawn an actor or create a new Possessable just to choose a binding.
	if (BindingGuid.IsValid())
	{
		const UClass* Class = MovieSceneHelpers::GetBoundObjectClass(GetSequence(), BindingGuid);
		if (!Class || !Class->IsChildOf(AActor::StaticClass()))
		{ Reason = TEXT("Choose an actor binding from this Section's sequence."); return false; }
	}
	const FMovieSceneObjectBindingID Binding = UE::MovieScene::FRelativeObjectBindingID(BindingGuid);
	if (GetTargetBinding(Index) == Binding && Shot->Targets[Index].Target.Actor.IsNull()) return true;
	BeginEdit(LOCTEXT("SubjectBinding", "Set Shot Subject Binding"));
	if (auto* Values = GetInstanceValues())
	{
		Values->EnsureSubjectIdentities(); Values->RefreshSubjectSlots();
		const FName Path(*FString::Printf(TEXT("Shot.Targets.%s.Target.Actor"), *Shot->Targets[Index].SubjectId.ToString(EGuidFormats::Digits)));
		for (auto& Subject : Values->Subjects) if (Subject.Input == Path) Subject.Binding = Binding;
		Shot->Targets[Index].Target.Actor.Reset();
		Shot->Targets[Index].CachedBoundsMeshComponent.Reset();
		Shot->Targets[Index].CachedBoundsSourceComponentName = NAME_None;
		Shot->Targets[Index].CachedAutoBoundsExtent = FVector::ZeroVector;
		Changed(true); EndEdit();
		return true;
	}
	Section->TargetActorOverrides.RemoveAll([Index](const auto& Override) { return Override.TargetIndex == Index; });
	if (BindingGuid.IsValid())
	{
		auto& Override = Section->TargetActorOverrides.AddDefaulted_GetRef();
		Override.TargetIndex = Index;
		Override.Binding = Binding;
	}
	// Preserve authored pivot, bounds and preview settings, as the Section's
	// existing Bind Target Actors menu does. Only object identity/cache changes.
	auto& Target = Shot->Targets[Index];
	Target.Target.Actor.Reset();
	Target.CachedBoundsMeshComponent.Reset();
	Target.CachedBoundsSourceComponentName = NAME_None;
	Target.CachedAutoBoundsExtent = FVector::ZeroVector;
	Changed(true);
	EndEdit();
	return true;
}

bool FComposableCameraShotAuthoringSession::SetTargetActor(int32 Index, AActor* Actor, FString& Reason)
{
	if (!CanEdit()) { Reason = TEXT("The current shot or sequence is locked."); return false; }
	FComposableCameraShot* Shot = GetShot();
	if (!Shot || !Shot->Targets.IsValidIndex(Index)) return false;
	if (GetInstanceValues())
	{
		const auto Seq = GetSequencer();
		if (!Seq || Seq->GetFocusedMovieSceneSequence() != GetSequence()) return false;
		if (!GEditor || IsEditing() || GEditor->IsTransactionActive()) return false;
		FMovieSceneObjectBindingID Binding;
		if (Actor && !ResolveSubjectBinding(*Seq, Actor, false, Binding, Reason)) return false;
		BeginEdit(LOCTEXT("InstanceSubject", "Set Camera Instance Subject"));
		if (Actor && !ResolveSubjectBinding(*Seq, Actor, true, Binding, Reason)) { EndEdit(); return false; }
		auto* Values = GetInstanceValues();
		Values->EnsureSubjectIdentities(); Values->RefreshSubjectSlots();
		const FName Path(*(TEXT("Shot.Targets.") + Shot->Targets[Index].SubjectId.ToString(EGuidFormats::Digits) + TEXT(".Target.Actor")));
		for (auto& Slot : Values->Subjects) if (Slot.Input == Path) Slot.Binding = Binding;
		Shot->Targets[Index].Target.Actor.Reset();
		Shot->Targets[Index].CachedBoundsMeshComponent.Reset();
		Shot->Targets[Index].CachedBoundsSourceComponentName = NAME_None;
		Shot->Targets[Index].CachedAutoBoundsExtent = FVector::ZeroVector;
		Changed(true); EndEdit();
		return true;
	}
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
	{
		Shot->Targets[Index].Target = ExistingTargets[Index].Target;
		Shot->Targets[Index].SubjectId = ExistingTargets[Index].SubjectId;
	}
	if (UMovieSceneComposableCameraShotSection* Section = GetSection())
	{
		Section->TargetActorOverrides.RemoveAll([Shot](const auto& Override) { return !Shot->Targets.IsValidIndex(Override.TargetIndex); });
		for (auto& Target : Shot->Targets) Target.Target.Actor.Reset();
	}
	if (ExistingTargets.IsEmpty()) bUseLevelWorld = false;
	if (GetInstanceValues()) for (auto& Target : Shot->Targets) Target.Target.Actor.Reset();
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
	Collector.AddReferencedObject(SequenceDraft);
	Collector.AddReferencedObject(DraftSequence);
}
#undef LOCTEXT_NAMESPACE
