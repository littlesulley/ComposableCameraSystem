// Copyright 2026 Sulley. All Rights Reserved.
#include "MovieScene/MovieSceneComposableCameraInstanceParameterTrack.h"
#include "MovieScene/MovieSceneComposableCameraInstanceSection.h"

void UMovieSceneComposableCameraInstanceParameterTrack::AddSection(UMovieSceneSection& Section)
{
	auto* ParameterSection = Cast<UMovieSceneComposableCameraInstanceSection>(&Section);
	if (!ParameterSection || ParameterSection->Channels.Num() > 1) return;
	// Native Section paste targets the destination property lane, not the copied path.
	if (ParameterSection->Channels.Num() == 1 && ParameterSection->Channels[0].Input != Input)
	{
		ParameterSection->Modify();
		ParameterSection->Channels[0].Input = Input;
		ParameterSection->RebuildChannelProxy();
	}
	Sections.AddUnique(&Section);
}
bool UMovieSceneComposableCameraInstanceParameterTrack::SupportsType(TSubclassOf<UMovieSceneSection> Type) const
{
	return Type == UMovieSceneComposableCameraInstanceSection::StaticClass();
}
UMovieSceneSection* UMovieSceneComposableCameraInstanceParameterTrack::CreateNewSection()
{
	auto* Section = NewObject<UMovieSceneComposableCameraInstanceSection>(this, NAME_None, RF_Transactional);
	Section->SetRange(TRange<FFrameNumber>::All());
	return Section;
}
