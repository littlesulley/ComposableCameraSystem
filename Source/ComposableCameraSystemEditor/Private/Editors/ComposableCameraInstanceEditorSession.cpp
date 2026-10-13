// Copyright 2026 Sulley. All Rights Reserved.
#include "Editors/ComposableCameraInstanceEditorSession.h"
#include "Editors/ComposableCameraShotSubjectLabel.h"
#include "ISequencer.h"
#include "MovieScene.h"
#include "MovieSceneSequence.h"
#include "MovieScene/MovieSceneComposableCameraInstanceTrack.h"
#include "MovieScene/MovieSceneComposableCameraInstanceParameterTrack.h"
#include "MovieScene/MovieSceneComposableCameraInstanceSection.h"
#include "UObject/UnrealType.h"
#include "GameFramework/Actor.h"
#include "Sequencer/ComposableCameraShotAuthoring.h"
#include "LevelSequence/ComposableCameraLevelSequenceComponent.h"
#include "UObject/StrongObjectPtr.h"
#include "ScopedTransaction.h"

namespace
{
TArray<FComposableCameraInstanceEditorSession*> LiveInstanceEditors;
}
FComposableCameraInstanceEditorSession::FComposableCameraInstanceEditorSession() { LiveInstanceEditors.Add(this); }
FComposableCameraInstanceEditorSession::~FComposableCameraInstanceEditorSession()
{
	LiveInstanceEditors.Remove(this);
	// Closing/switching the authoring view discards unkeyed preview edits.
	if (const auto Seq = Sequencer.Pin(); Seq && Track.IsValid() && Seq->GetFocusedMovieSceneSequence() == Track->GetTypedOuter<UMovieSceneSequence>()) Seq->ForceEvaluate();
}
TOptional<double> FComposableCameraInstanceEditorSession::ReadEditorValue(const UMovieSceneComposableCameraInstanceSection* Section, FName Input)
{
	const auto* Owner = Section ? UMovieSceneComposableCameraInstanceTrack::FindForSection(*Section) : nullptr;
	for (int32 Index = LiveInstanceEditors.Num() - 1; Index >= 0; --Index)
	{
		const auto* Editor = LiveInstanceEditors[Index];
		const auto Seq = Editor->Sequencer.Pin();
		// Native AddKey calls Section.Modify before asking for external values; its
		// signature has already changed here. Pending values are gated by identity/time.
		if (!Owner || Editor->Track.Get() != Owner || !Seq || !Editor->Values || !Editor->PendingInputs.Contains(Input)
			|| Seq->GetFocusedMovieSceneSequence() != Owner->GetTypedOuter<UMovieSceneSequence>()
			|| Seq->GetLocalTime().Time != Editor->SampleTime) continue;
		TArray<FComposableCameraInstanceField> Fields;
		Editor->Values->GatherFields(Fields);
		if (const auto* Field = Fields.FindByPredicate([&](const auto& Value) { return Value.Path == Input && Value.Type != EComposableCameraInstanceFieldType::Object; })) return Field->ReadNumber();
	}
	return TOptional<double>();
}

namespace ComposableCameraSystem::CameraInstanceAuthoring
{
FText ParameterLabel(const UComposableCameraInstanceData& Values, FName Input)
{
	FString Label = Input.ToString();
	for (int32 Index = 0; Index < Values.Shot.Targets.Num(); ++Index)
	{
		const FString Prefix = TEXT("Shot.Targets.") + Values.Shot.Targets[Index].SubjectId.ToString(EGuidFormats::Digits);
		if (Label.StartsWith(Prefix)) { Label = ShotSubjectLabels::ToText(Index).ToString() + Label.Mid(Prefix.Len()); break; }
	}
	Label.RemoveFromStart(TEXT("Shot."));
	if (Label.StartsWith(TEXT("Placement."))) Label = TEXT("Follow.") + Label.Mid(10);
	Label.ReplaceInline(TEXT(".Target."), TEXT("."));
	TArray<FString> Parts; Label.ParseIntoArray(Parts, TEXT("."));
	for (auto& Part : Parts) Part = FName::NameToDisplayString(Part, false);
	return FText::FromString(FString::Join(Parts, TEXT(" / ")));
}
UMovieSceneComposableCameraInstanceParameterTrack* FindParameterTrack(UMovieSceneComposableCameraInstanceTrack& Owner, FName Input)
{
	const auto* Scene = Owner.GetTypedOuter<UMovieScene>();
	const auto* Binding = Scene ? Scene->FindBinding(Owner.FindObjectBindingGuid()) : nullptr;
	if (Binding) for (UMovieSceneTrack* Track : Binding->GetTracks())
		if (auto* Parameter = Cast<UMovieSceneComposableCameraInstanceParameterTrack>(Track); Parameter && Parameter->Input == Input) return Parameter;
	return nullptr;
}
UMovieSceneComposableCameraInstanceSection* AddInterval(UMovieSceneTrack& Track, FFrameTime Time)
{
	auto* Scene = Track.GetTypedOuter<UMovieScene>();
	if (!Scene || Scene->IsReadOnly()) return nullptr;
	const auto* Binding = Scene->FindBinding(Track.FindObjectBindingGuid());
	if (!Scene->GetTracks().Contains(&Track) && (!Binding || !Binding->GetTracks().Contains(&Track))) return nullptr;
	Track.Modify();
	auto* Section = Cast<UMovieSceneComposableCameraInstanceSection>(Track.CreateNewSection());
	if (!Section) return nullptr;
	// The first property section is unbounded, like a native numeric property track.
	// Explicit additional sections and activation intervals retain finite ranges.
	if (!Track.IsA<UMovieSceneComposableCameraInstanceParameterTrack>() || !Track.IsEmpty())
	{
		const FFrameNumber Start = Time.FloorToFrame();
		const int64 Length = FMath::Max<int64>(1, FMath::RoundToInt64(Scene->GetTickResolution().AsDecimal() * 5.0));
		if (Length > MAX_int32 || static_cast<int64>(Start.Value) + Length > MAX_int32) return nullptr;
		Section->SetRange(TRange<FFrameNumber>(Start, Start + static_cast<int32>(Length)));
	}
	if (const auto* Parameter = Cast<UMovieSceneComposableCameraInstanceParameterTrack>(&Track))
		if (auto* Owner = UMovieSceneComposableCameraInstanceTrack::Find(Scene, Track.FindObjectBindingGuid()); Owner && Owner->Instance)
		{
			TArray<FComposableCameraInstanceField> Fields;
			Owner->Instance->GatherFields(Fields);
			if (const auto* Field = Fields.FindByPredicate([&](const auto& Item) { return Item.Path == Parameter->Input; }))
			{
				auto& Channel = Section->Channels.AddDefaulted_GetRef();
				Channel.Input = Field->Path; Channel.Type = Field->Type;
				const double Value = Field->ReadNumber();
				if (Field->Type == EComposableCameraInstanceFieldType::Number) Channel.Number.SetDefault(Value);
				else if (Field->Type == EComposableCameraInstanceFieldType::Integer) Channel.Integer.SetDefault(static_cast<int32>(Value));
				else if (Field->Type == EComposableCameraInstanceFieldType::Boolean) Channel.Boolean.SetDefault(Value != 0.0);
				Section->RebuildChannelProxy();
			}
		}
	Track.AddSection(*Section);
	return Section;
}
UMovieSceneComposableCameraInstanceParameterTrack* AddParameterTrack(UMovieSceneComposableCameraInstanceTrack& Owner, FName Input, FFrameTime Time)
{
	if (auto* Existing = FindParameterTrack(Owner, Input)) return Existing;
	auto* Scene = Owner.GetTypedOuter<UMovieScene>();
	const FGuid Binding = Owner.FindObjectBindingGuid();
	if (!Scene || Scene->IsReadOnly() || !Binding.IsValid() || !Owner.Instance || !Owner.IsInMovieScene()) return nullptr;
	TArray<FComposableCameraInstanceField> Fields;
	Owner.Instance->GatherFields(Fields);
	const auto* Field = Fields.FindByPredicate([&](const auto& Item) { return Item.Path == Input; });
	if (!Field || Field->Type == EComposableCameraInstanceFieldType::Object) return nullptr;
	Scene->Modify();
	auto* Parameter = Scene->AddTrack<UMovieSceneComposableCameraInstanceParameterTrack>(Binding);
	if (!Parameter) return nullptr;
	Parameter->Input = Input;
	Parameter->SetDisplayName(ParameterLabel(*Owner.Instance, Input));
	// Upgrade prototype animation by value: preserve ranges, interpolation and keys.
	for (auto* ExistingSection : Owner.GetAllSections())
	{
		auto* Legacy = Cast<UMovieSceneComposableCameraInstanceSection>(ExistingSection);
		if (!Legacy) continue;
		const int32 Index = Legacy->Channels.IndexOfByPredicate([&](const auto& Channel) { return Channel.Input == Input; });
		if (Index == INDEX_NONE) continue;
		auto* Migrated = CastChecked<UMovieSceneComposableCameraInstanceSection>(Parameter->CreateNewSection());
		Migrated->SetRange(Legacy->GetRange()); Migrated->SetIsActive(Legacy->IsActive());
		Migrated->SetIsLocked(Legacy->IsLocked());
		Migrated->Channels.Add(Legacy->Channels[Index]); Migrated->RebuildChannelProxy();
		Parameter->AddSection(*Migrated);
		Legacy->Modify(); Legacy->Channels.RemoveAt(Index); Legacy->RebuildChannelProxy(); Legacy->MarkAsChanged();
	}
	if (Parameter->IsEmpty()) AddInterval(*Parameter, Time);
	return Parameter;
}

bool MakeParameterSectionInfinite(UMovieSceneComposableCameraInstanceSection& Section, ISequencer& Sequencer)
{
	auto* Parameter = Section.GetTypedOuter<UMovieSceneComposableCameraInstanceParameterTrack>();
	const auto* Owner = UMovieSceneComposableCameraInstanceTrack::FindForSection(Section);
	const auto* Scene = Section.GetTypedOuter<UMovieScene>();
	if (!Parameter || !Parameter->HasSection(Section) || !Owner || !Owner->IsInMovieScene()
		|| !Scene || Scene->IsReadOnly() || Section.IsLocked() || Sequencer.IsReadOnly()
		|| Sequencer.GetFocusedMovieSceneSequence() != Section.GetTypedOuter<UMovieSceneSequence>()
		|| Section.GetRange() == TRange<FFrameNumber>::All()) return false;
	const FScopedTransaction Transaction(NSLOCTEXT("CameraInstanceAuthoring", "InfiniteParameter", "Make Parameter Section Infinite"));
	Section.Modify();
	Section.SetRange(TRange<FFrameNumber>::All());
	Sequencer.NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
	Sequencer.ForceEvaluate();
	return true;
}
}

void FComposableCameraInstanceEditorSession::Bind(UMovieSceneComposableCameraInstanceTrack* InTrack, TSharedPtr<ISequencer> InSequencer)
{
	Track = InTrack; Sequencer = InSequencer;
	Values = NewObject<UComposableCameraInstanceData>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional);
	Baseline = NewObject<UComposableCameraInstanceData>(GetTransientPackage(), NAME_None, RF_Transient);
	Signature.Invalidate();
	Refresh();
}
void FComposableCameraInstanceEditorSession::UpdateSequencer(TSharedPtr<ISequencer> InSequencer)
{
	if (Sequencer.Pin() == InSequencer) return;
	Sequencer = InSequencer;
	// The new editor can have the same time/signature as the old one. Its first
	// sample must still discard pending preview edits and refresh source handles.
	Signature.Invalidate();
	PendingInputs.Reset();
}
bool FComposableCameraInstanceEditorSession::CanEdit() const
{
	const auto Seq = Sequencer.Pin();
	const auto* Owner = Track.Get();
	const auto* Scene = Owner ? Owner->GetTypedOuter<UMovieScene>() : nullptr;
	if (!Owner || !Owner->Instance || !Owner->IsInMovieScene() || !Scene || Scene->IsReadOnly() || !Seq || Seq->IsReadOnly()
		|| Seq->GetFocusedMovieSceneSequence() != Owner->GetTypedOuter<UMovieSceneSequence>()) return false;
	const auto* Active = UMovieSceneComposableCameraInstanceTrack::FindActiveSection(*Owner, Seq->GetLocalTime().Time);
	return Active && !Active->IsLocked();
}
void FComposableCameraInstanceEditorSession::CacheFields()
{
	Values->GatherFields(CachedFields);
	Baseline->GatherFields(CachedBaselineFields);
	// Baseline is copied from Values before compiling, so field order/layout match.
	check(CachedFields.Num() == CachedBaselineFields.Num());
	TArray<FComposableCameraInstanceField> SourceFields;
	Track->Instance->GatherFields(SourceFields);
	CachedDefaults.Reset(CachedFields.Num());
	for (const auto& Field : CachedFields)
	{
		const auto* Source = SourceFields.FindByPredicate([&](const auto& Item) { return Item.Path == Field.Path && Item.Type == Field.Type; });
		CachedDefaults.Add(Source ? Source->ReadNumber() : Field.ReadNumber());
	}
}

FComposableCameraInstanceEditorSession::ERefreshResult FComposableCameraInstanceEditorSession::Refresh()
{
	const auto Seq = Sequencer.Pin();
	auto* Owner = Track.Get();
	if (bCommitting || !Values || !Owner || !Owner->Instance || !Owner->IsInMovieScene() || !Seq
		|| Seq->GetFocusedMovieSceneSequence() != Owner->GetTypedOuter<UMovieSceneSequence>()) return ERefreshResult::None;
	const auto* Scene = Owner->GetTypedOuter<UMovieScene>();
	const FFrameTime Time = Seq->GetLocalTime().Time;
	const bool bSourceChanged = Signature != Scene->GetSignature();
	if (!bSourceChanged && SampleTime == Time) return ERefreshResult::None;
	if (bSourceChanged)
	{
		Values->CopyConfiguration(*Owner->Instance);
		Baseline->CopyConfiguration(*Values);
		CacheFields();
	}
	// Native widgets read these addresses. Seeking must not replace bags/arrays or
	// rebuild the property tree. Restore defaults first for gaps and muted lanes.
	for (int32 Index = 0; Index < CachedFields.Num(); ++Index)
		if (CachedFields[Index].Type != EComposableCameraInstanceFieldType::Object) CachedFields[Index].WriteNumber(CachedDefaults[Index]);
	Owner->EvaluateParameters(Time, CachedFields);
	for (int32 Index = 0; Index < CachedFields.Num(); ++Index)
		if (CachedFields[Index].Type != EComposableCameraInstanceFieldType::Object) CachedBaselineFields[Index].WriteNumber(CachedFields[Index].ReadNumber());
	PendingInputs.Reset();
	Signature = Scene->GetSignature(); SampleTime = Time;
	return bSourceChanged ? ERefreshResult::Source : ERefreshResult::Values;
}
void FComposableCameraInstanceEditorSession::Commit(bool bStructural)
{
	using namespace ComposableCameraSystem::CameraInstanceAuthoring;
	if (bCommitting || !CanEdit()) return;
	TGuardValue<bool> Guard(bCommitting, true);
	auto* Owner = Track.Get();
	const auto Seq = Sequencer.Pin();
	const FFrameTime Time = Seq->GetLocalTime().Time;
	// A gesture owns its sample time. Never apply stale values to another frame.
	if (Time != SampleTime) { Signature.Invalidate(); return; }
	Values->EnsureSubjectIdentities(); Values->RefreshSubjectSlots();
	// Structural actions (selected actors, templates, appended subjects) share the same binding conversion.
	// Only the editing buffer may temporarily carry actor pointers.
	if (Values->bComposition)
		for (auto& Target : Values->Shot.Targets)
			if (AActor* Actor = Target.Target.Actor.Get())
			{
				FString Reason;
				FMovieSceneObjectBindingID Binding;
				if (!ComposableCameraSystem::ShotAuthoring::ResolveSubjectBinding(*Seq, Actor, true, Binding, Reason)) return;
				const FName Path(*(TEXT("Shot.Targets.") + Target.SubjectId.ToString(EGuidFormats::Digits) + TEXT(".Target.Actor")));
				for (auto& Slot : Values->Subjects) if (Slot.Input == Path) Slot.Binding = Binding;
				Target.Target.Actor.Reset();
			}
	TArray<FComposableCameraInstanceField> Edited, Before, Defaults;
	Values->GatherFields(Edited); Baseline->GatherFields(Before); Owner->Instance->GatherFields(Defaults);
	TMap<FName, double> RestoreDefaults;
	const auto Mode = Seq->GetAutoChangeMode();
	const bool bAutoKey = Mode == EAutoChangeMode::AutoKey || Mode == EAutoChangeMode::All;
	for (const auto& Field : Edited)
	{
		if (Field.Type == EComposableCameraInstanceFieldType::Object) continue;
		const auto* Old = Before.FindByPredicate([&](const auto& Item) { return Item.Path == Field.Path && Item.Type == Field.Type; });
		const auto* Default = Defaults.FindByPredicate([&](const auto& Item) { return Item.Path == Field.Path && Item.Type == Field.Type; });
		auto* Parameter = FindParameterTrack(*Owner, Field.Path);
		auto* Section = Parameter ? UMovieSceneComposableCameraInstanceTrack::FindActiveSection(*Parameter, Time) : nullptr;
		// Prototype animation remains editable before explicit migration to individual lanes.
		auto* Activation = UMovieSceneComposableCameraInstanceTrack::FindActiveSection(*Owner, Time);
		if (!Parameter && Activation && Activation->Channels.ContainsByPredicate([&](const auto& Item) { return Item.Input == Field.Path; })) Section = Activation;
		const bool bChanged = !Old || Field.ReadNumber() != Old->ReadNumber();
		if ((Parameter || Section || (bAutoKey && bChanged)) && Default) RestoreDefaults.Add(Field.Path, Default->ReadNumber());
		if (!bChanged) continue;
		if (bAutoKey && !Parameter && !Section) { Parameter = AddParameterTrack(*Owner, Field.Path, Time); bStructural |= Parameter != nullptr; }
		if (Parameter && !Section) Section = UMovieSceneComposableCameraInstanceTrack::FindActiveSection(*Parameter, Time);
		if (bAutoKey && Parameter && !Section) { Section = AddInterval(*Parameter, Time); bStructural |= Section != nullptr; }
		if (Section && Section->IsLocked()) { if (Old) Field.WriteNumber(Old->ReadNumber()); continue; }
		// With Auto Key off, animation edits remain in this view until the native key button
		// asks OnGetEditorValue for them. Seeking/undo/closing discards these pending values.
		if (bAutoKey && Section) { Section->AddKey(Field, Time.FloorToFrame(), Seq->GetKeyInterpolation()); PendingInputs.Remove(Field.Path); }
		else if (Parameter || Section) PendingInputs.Add(Field.Path);
	}
	Owner->Modify(); Owner->Instance->Modify();
	Owner->Instance->CopyConfiguration(*Values);
	Owner->Instance->GatherFields(Defaults);
	for (const auto& Field : Defaults)
	{
		if (const double* Value = RestoreDefaults.Find(Field.Path)) Field.WriteNumber(*Value);
		// Authored Subjects contain binding IDs, never live actor references.
		if (const auto* Object = CastField<FObjectPropertyBase>(Field.Property); Object && Object->PropertyClass->IsChildOf(AActor::StaticClass())) Field.WriteObject(nullptr);
	}
	Owner->MarkAsChanged();
	Baseline->CopyConfiguration(*Values);
	// An edit may have changed a Subject array or property bag; no cached address
	// may survive it. Capture authored defaults, not pending/keyed sample values.
	CacheFields();
	Signature = Owner->GetTypedOuter<UMovieScene>()->GetSignature();
	Seq->NotifyMovieSceneDataChanged(bStructural ? EMovieSceneDataChangeType::MovieSceneStructureItemsChanged : EMovieSceneDataChangeType::TrackValueChanged);
	Seq->ForceEvaluate();
	Preview();
}
void FComposableCameraInstanceEditorSession::Preview()
{
	const auto Seq = Sequencer.Pin();
	auto* Owner = Track.Get();
	if (!Seq || !Owner || !Values) return;
	// Editor-only gesture update. Runtime evaluation keeps its preallocated per-sequence buffers.
	TStrongObjectPtr<UComposableCameraInstanceData> PreviewValues(NewObject<UComposableCameraInstanceData>());
	PreviewValues->CopyConfiguration(*Values);
	TArray<FComposableCameraInstanceField> Fields; PreviewValues->GatherFields(Fields);
	for (const auto& Binding : PreviewValues->Subjects)
	{
		const auto* Field = Fields.FindByPredicate([&](const auto& Item) { return Item.Path == Binding.Input; });
		if (!Field) continue;
		UObject* Actor = nullptr;
		if (Binding.Binding.IsValid()) for (const auto Object : Binding.Binding.ResolveBoundObjects(Seq->GetFocusedTemplateID(), *Seq))
			if (Object.IsValid()) { Actor = Object.Get(); break; }
		Field->WriteObject(Actor);
	}
	for (const auto Object : Owner->OutputBinding.ResolveBoundObjects(Seq->GetFocusedTemplateID(), *Seq))
		if (auto* Actor = Cast<AActor>(Object.Get()))
			if (auto* Output = Actor->FindComponentByClass<UComposableCameraLevelSequenceComponent>(); Output && Output->CameraInstanceOwner == Owner)
				Output->EvaluateCameraInstance(*PreviewValues.Get(), 0.f, true);
}
void FComposableCameraInstanceEditorSession::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Values); Collector.AddReferencedObject(Baseline);
}
