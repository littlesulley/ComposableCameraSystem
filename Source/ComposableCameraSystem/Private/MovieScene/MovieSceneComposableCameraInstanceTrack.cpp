// Copyright 2026 Sulley. All Rights Reserved.
#include "MovieScene/MovieSceneComposableCameraInstanceTrack.h"
#include "MovieScene/ComposableCameraInstanceData.h"
#include "MovieScene/MovieSceneComposableCameraInstanceSection.h"
#include "MovieScene.h"
#include "MovieScene/MovieSceneComposableCameraInstanceParameterTrack.h"

bool UMovieSceneComposableCameraInstanceTrack::IsInMovieScene() const
{
	const UMovieScene* Scene = GetTypedOuter<UMovieScene>();
	if (!Scene) return false;
	if (Scene->GetTracks().Contains(this)) return true;
	for (const FMovieSceneBinding& Binding : Scene->GetBindings())
		if (Binding.GetTracks().Contains(this)) return true;
	return false;
}
UMovieSceneComposableCameraInstanceTrack* UMovieSceneComposableCameraInstanceTrack::Find(UMovieScene* Scene, FGuid Binding)
{
	const FMovieSceneBinding* Object = Scene ? Scene->FindBinding(Binding) : nullptr;
	if (Object) for (UMovieSceneTrack* Track : Object->GetTracks())
		if (auto* InstanceTrack = Cast<UMovieSceneComposableCameraInstanceTrack>(Track)) return InstanceTrack;
	return nullptr;
}
UMovieSceneComposableCameraInstanceTrack* UMovieSceneComposableCameraInstanceTrack::FindForSection(const UMovieSceneSection& Section)
{
	if (auto* Track = Section.GetTypedOuter<UMovieSceneComposableCameraInstanceTrack>()) return Track;
	const auto* Track = Section.GetTypedOuter<UMovieSceneTrack>();
	return Track ? Find(Track->GetTypedOuter<UMovieScene>(), Track->FindObjectBindingGuid()) : nullptr;
}
UMovieSceneComposableCameraInstanceSection* UMovieSceneComposableCameraInstanceTrack::FindActiveSection(const UMovieSceneTrack& Track, FFrameTime Time)
{
	UMovieSceneComposableCameraInstanceSection* Result = nullptr;
	for (UMovieSceneSection* Section : Track.GetAllSections())
	{
		auto* Candidate = Cast<UMovieSceneComposableCameraInstanceSection>(Section);
		if (!Candidate || !Candidate->IsActive() || !Candidate->GetRange().Contains(Time.FloorToFrame())) continue;
		const FFrameNumber Start = Candidate->HasStartFrame() ? Candidate->GetInclusiveStartFrame() : FFrameNumber(MIN_int32);
		if (!Result || Start >= (Result->HasStartFrame() ? Result->GetInclusiveStartFrame() : FFrameNumber(MIN_int32))) Result = Candidate;
	}
	return Result;
}
void UMovieSceneComposableCameraInstanceTrack::EvaluateParameters(FFrameTime Time, TArrayView<const FComposableCameraInstanceField> Fields) const
{
	// Keep prototype channels readable. New parameter lanes take precedence per field.
	if (const auto* Section = FindActiveSection(*this, Time)) Section->Evaluate(Time, Fields);
	const UMovieScene* Scene = GetTypedOuter<UMovieScene>();
	const FMovieSceneBinding* Binding = Scene ? Scene->FindBinding(FindObjectBindingGuid()) : nullptr;
	if (Binding) for (const UMovieSceneTrack* Track : Binding->GetTracks())
		if (const auto* Parameter = Cast<UMovieSceneComposableCameraInstanceParameterTrack>(Track); Parameter && !Track->IsEvalDisabled())
			if (const auto* Section = FindActiveSection(*Track, Time))
				if (const auto* Field = Fields.FindByPredicate([&](const auto& Value) { return Value.Path == Parameter->Input; }))
					Section->Evaluate(Time, MakeArrayView(Field, 1));
}

UMovieSceneComposableCameraInstanceTrack::UMovieSceneComposableCameraInstanceTrack(const FObjectInitializer& Initializer)
	: Super(Initializer)
{
	Instance = CreateDefaultSubobject<UComposableCameraInstanceData>(TEXT("CameraInstance"));
	EvalOptions.bEvaluateInPreroll = false;
	EvalOptions.bEvaluateInPostroll = false;
#if WITH_EDITORONLY_DATA
	TrackTint = FColor(64, 160, 192, 65);
#endif
}

void UMovieSceneComposableCameraInstanceTrack::AddSection(UMovieSceneSection& Section)
{
	if (SupportsType(Section.GetClass())) Sections.AddUnique(&Section);
}
bool UMovieSceneComposableCameraInstanceTrack::SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const
{
	return SectionClass == UMovieSceneComposableCameraInstanceSection::StaticClass();
}
UMovieSceneSection* UMovieSceneComposableCameraInstanceTrack::CreateNewSection()
{
	auto* Section = NewObject<UMovieSceneComposableCameraInstanceSection>(this, NAME_None, RF_Transactional);
	if (const UMovieScene* Scene = GetTypedOuter<UMovieScene>())
		Section->SetRange(TRange<FFrameNumber>(0, (5.0 * Scene->GetTickResolution()).FloorToFrame()));
	return Section;
}
const TArray<UMovieSceneSection*>& UMovieSceneComposableCameraInstanceTrack::GetAllSections() const { return Sections; }
bool UMovieSceneComposableCameraInstanceTrack::HasSection(const UMovieSceneSection& Section) const { return Sections.Contains(&Section); }
bool UMovieSceneComposableCameraInstanceTrack::IsEmpty() const { return Sections.IsEmpty(); }
void UMovieSceneComposableCameraInstanceTrack::RemoveSection(UMovieSceneSection& Section) { Sections.Remove(&Section); }
void UMovieSceneComposableCameraInstanceTrack::RemoveSectionAt(int32 Index) { Sections.RemoveAt(Index); }
void UMovieSceneComposableCameraInstanceTrack::RemoveAllAnimationData() { Sections.Reset(); }
#if WITH_EDITORONLY_DATA
FText UMovieSceneComposableCameraInstanceTrack::GetDefaultDisplayName() const { return NSLOCTEXT("ComposableCameraInstance", "Track", "Camera Instance"); }
#endif
