// Copyright 2026 Sulley. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "MovieSceneTrackEditor.h"

class UComposableCameraInstanceData;
class UMovieSceneComposableCameraInstanceTrack;
class UMovieSceneSection;
class UMovieSceneCameraCutSection;
class UMovieScene;

namespace ComposableCameraSystem::CameraInstanceAuthoring
{
	/** Shared hover/creation interval. Failure leaves a bounded empty range at Start. */
	bool MakeCreationRange(FFrameRate TickResolution, FFrameNumber Start, double DurationSeconds,
		TRange<FFrameNumber>& Range, FString& Error);
	/** Read-only preflight shared by hover, creation and duplication. Locked active Cuts block replacement. */
	bool CanReplaceCameraCut(const UMovieScene& Scene, const TRange<FFrameNumber>& Range, FString& Error);
	/** Set this existing Activation's output as the cut over its range, without creating an instance. */
	bool CanSetCameraCut(TSharedRef<ISequencer> Sequencer, const UMovieSceneComposableCameraInstanceTrack& Owner,
		const UMovieSceneSection& Activation, FString& Error);
	bool SetCameraCut(TSharedRef<ISequencer> Sequencer, UMovieSceneComposableCameraInstanceTrack& Owner,
		UMovieSceneSection& Activation, FString& Error);
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
	struct FManagedCutState
	{
		TWeakObjectPtr<UMovieSceneSection> Activation;
		TWeakObjectPtr<UMovieSceneCameraCutSection> Cut;
		TRange<FFrameNumber> ActivationRange;
		TRange<FFrameNumber> CutRange;
	};
	void CacheManagedCuts();
	void InitializeBindingNames();
	void OnDataChanged(EMovieSceneDataChangeType Change);
	FDelegateHandle DataChangedHandle;
	FDelegateHandle SequenceActivatedHandle;
	bool bSynchronizing = false;
	TArray<TWeakObjectPtr<UMovieSceneComposableCameraInstanceTrack>> KnownInstances;
	TMap<TWeakObjectPtr<UMovieSceneComposableCameraInstanceTrack>, FManagedCutState> ManagedCutStates;
	TWeakObjectPtr<UMovieScene> CachedScene;
	TSet<TWeakObjectPtr<UMovieSceneComposableCameraInstanceTrack>> AttemptedUpgrades;
	TWeakObjectPtr<UMovieScene> NamingScene;
};
