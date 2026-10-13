// Copyright 2026 Sulley. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "MovieSceneNameableTrack.h"
#include "MovieSceneComposableCameraInstanceParameterTrack.generated.h"

/** A native parameter lane under the instance's object binding. Owns animation, not another instance. */
UCLASS()
class COMPOSABLECAMERASYSTEM_API UMovieSceneComposableCameraInstanceParameterTrack : public UMovieSceneNameableTrack
{
	GENERATED_BODY()
public:
	UPROPERTY()
	FName Input;
	virtual FName GetTrackName() const override { return Input; }
	virtual void AddSection(UMovieSceneSection& Section) override;
	virtual bool SupportsType(TSubclassOf<UMovieSceneSection> Type) const override;
	virtual UMovieSceneSection* CreateNewSection() override;
	virtual const TArray<UMovieSceneSection*>& GetAllSections() const override { return Sections; }
	virtual bool HasSection(const UMovieSceneSection& Section) const override { return Sections.Contains(&Section); }
	virtual bool IsEmpty() const override { return Sections.IsEmpty(); }
	virtual void RemoveSection(UMovieSceneSection& Section) override { Sections.Remove(&Section); }
	virtual void RemoveSectionAt(int32 Index) override { Sections.RemoveAt(Index); }
	virtual void RemoveAllAnimationData() override { Sections.Reset(); }
	virtual bool SupportsMultipleRows() const override { return false; }
private:
	UPROPERTY()
	TArray<TObjectPtr<UMovieSceneSection>> Sections;
};
