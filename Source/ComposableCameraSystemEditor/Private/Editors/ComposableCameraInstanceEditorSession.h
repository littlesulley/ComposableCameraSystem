// Copyright 2026 Sulley. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "MovieScene/ComposableCameraInstanceData.h"

class ISequencer;
class UMovieSceneTrack;
class UMovieSceneComposableCameraInstanceTrack;
class UMovieSceneComposableCameraInstanceSection;
class UMovieSceneComposableCameraInstanceParameterTrack;

namespace ComposableCameraSystem::CameraInstanceAuthoring
{
	FText ParameterLabel(const UComposableCameraInstanceData& Values, FName Input);
	UMovieSceneComposableCameraInstanceParameterTrack* FindParameterTrack(UMovieSceneComposableCameraInstanceTrack& Owner, FName Input);
	UMovieSceneComposableCameraInstanceParameterTrack* AddParameterTrack(UMovieSceneComposableCameraInstanceTrack& Owner, FName Input, FFrameTime Time);
	UMovieSceneComposableCameraInstanceSection* AddInterval(UMovieSceneTrack& Track, FFrameTime Time);
	/** Undoable opt-in conversion for existing bounded parameter sections. Keeps every key. */
	bool MakeParameterSectionInfinite(UMovieSceneComposableCameraInstanceSection& Section, ISequencer& Sequencer);
}

/** One transient, sampled editing buffer shared by quick pages and Details. Never saved as a second configuration. */
class FComposableCameraInstanceEditorSession : public FGCObject
{
public:
	enum class ERefreshResult { None, Values, Source };
	FComposableCameraInstanceEditorSession();
	~FComposableCameraInstanceEditorSession();
	static TOptional<double> ReadEditorValue(const UMovieSceneComposableCameraInstanceSection* Section, FName Input);
	void Bind(UMovieSceneComposableCameraInstanceTrack* InTrack, TSharedPtr<ISequencer> InSequencer);
	/** Reconnect a retained view after its Level Sequence editor is reopened. */
	void UpdateSequencer(TSharedPtr<ISequencer> InSequencer);
	bool CanEdit() const;
	/** Time changes sample in place; only Source invalidates property handles. */
	ERefreshResult Refresh();
	void Commit(bool bStructural = false);
	UComposableCameraInstanceData* GetValues() const { return Values; }
	UMovieSceneComposableCameraInstanceTrack* GetTrack() const { return Track.Get(); }
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FComposableCameraInstanceEditorSession"); }
private:
	TWeakObjectPtr<UMovieSceneComposableCameraInstanceTrack> Track;
	TWeakPtr<ISequencer> Sequencer;
	TObjectPtr<UComposableCameraInstanceData> Values;
	TObjectPtr<UComposableCameraInstanceData> Baseline;
	// Compiled only after source changes/commits. Time sampling preserves backing storage.
	TArray<FComposableCameraInstanceField> CachedFields;
	TArray<FComposableCameraInstanceField> CachedBaselineFields;
	TArray<double> CachedDefaults;
	FGuid Signature;
	FFrameTime SampleTime;
	TSet<FName> PendingInputs;
	bool bCommitting = false;
	void CacheFields();
	void Preview();
};
