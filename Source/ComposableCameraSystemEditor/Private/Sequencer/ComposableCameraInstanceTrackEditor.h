// Copyright 2026 Sulley. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "MovieSceneTrackEditor.h"

class UComposableCameraInstanceData;
class UMovieSceneComposableCameraInstanceTrack;
class UMovieSceneSection;
class UMovieScene;

namespace ComposableCameraSystem::CameraInstanceAuthoring
{
	/** Shared hover/creation interval. Failure leaves a bounded empty range at Start. */
	bool MakeCreationRange(FFrameRate TickResolution, FFrameNumber Start, double DurationSeconds,
		TRange<FFrameNumber>& Range, FString& Error);
	/** Shared creation for asset drops and sampled/draft configuration snapshots. */
	UMovieSceneComposableCameraInstanceTrack* Create(TSharedRef<ISequencer> Sequencer,
		UObject* Source, FFrameNumber Start, FString& Error, double DurationSeconds = 5.0, FText Label = FText::GetEmpty());
	/** Copy the instance and its parameter lanes, translating animation so Anchor starts at Start. */
	UMovieSceneComposableCameraInstanceTrack* DuplicateAtTime(TSharedRef<ISequencer> Sequencer,
		UMovieSceneComposableCameraInstanceTrack& Source, UMovieSceneSection& Anchor, FFrameNumber Start, FString& Error);
	void BuildSubjectsMenu(FMenuBuilder& Menu, UMovieSceneComposableCameraInstanceTrack* Owner, TSharedPtr<ISequencer> Sequencer);
	bool SetSubjectBinding(UMovieSceneComposableCameraInstanceTrack& Track, FName Input, FGuid LocalBinding);
}

class FComposableCameraInstanceTrackEditor : public FMovieSceneTrackEditor
{
public:
	static TSharedRef<ISequencerTrackEditor> CreateTrackEditor(TSharedRef<ISequencer> Sequencer);
	explicit FComposableCameraInstanceTrackEditor(TSharedRef<ISequencer> Sequencer);
	virtual ~FComposableCameraInstanceTrackEditor() override;
	virtual bool SupportsType(TSubclassOf<UMovieSceneTrack> Type) const override;
	virtual bool SupportsSequence(UMovieSceneSequence* Sequence) const override;
	virtual bool HandleAssetAdded(UObject* Asset, const FGuid& TargetObjectGuid) override;
	virtual bool OnAllowDrop(const FDragDropEvent& Event, FSequencerDragDropParams& Params) override;
	virtual FReply OnDrop(const FDragDropEvent& Event, const FSequencerDragDropParams& Params) override;
	virtual void BuildAddTrackMenu(FMenuBuilder& Menu) override;
	virtual TSharedRef<ISequencerSection> MakeSectionInterface(UMovieSceneSection& Section, UMovieSceneTrack& Track, FGuid Binding) override;
	virtual void BuildTrackContextMenu(FMenuBuilder& Menu, UMovieSceneTrack* Track) override;
	virtual void BuildObjectBindingTrackMenu(FMenuBuilder& Menu, const TArray<FGuid>& Bindings, const UClass* Class) override;
	virtual void BuildObjectBindingContextMenu(FMenuBuilder& Menu, const TArray<FGuid>& Bindings, const UClass* Class) override;
	virtual TSharedPtr<SWidget> BuildOutlinerEditWidget(const FGuid& Binding, UMovieSceneTrack* Track, const FBuildEditWidgetParams& Params) override;
	virtual void Tick(float DeltaTime) override;
private:
	void InitializeBindingNames();
	void OnDataChanged(EMovieSceneDataChangeType Change);
	FDelegateHandle DataChangedHandle;
	bool bSynchronizing = false;
	TArray<TWeakObjectPtr<UMovieSceneComposableCameraInstanceTrack>> KnownInstances;
	TSet<TWeakObjectPtr<UMovieSceneComposableCameraInstanceTrack>> AttemptedUpgrades;
	TWeakObjectPtr<UMovieScene> NamingScene;
};
