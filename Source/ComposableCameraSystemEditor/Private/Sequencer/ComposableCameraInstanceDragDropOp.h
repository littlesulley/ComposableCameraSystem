// Copyright 2026 Sulley. All Rights Reserved.
#pragma once

#include "DragAndDrop/AssetDragDropOp.h"
#include "UObject/GCObject.h"

class ISequencer;
class FComposableCameraShotAuthoringSession;
class UComposableCameraInstanceData;
class UMovieSceneSequence;
class UMovieSceneComposableCameraInstanceTrack;

/** Native asset transport reaches empty Sequencer space; payload is a GC-owned transient snapshot. */
class FComposableCameraInstanceDragDropOp : public FAssetDragDropOp, public FGCObject
{
public:
	DRAG_DROP_OPERATOR_TYPE(FComposableCameraInstanceDragDropOp, FAssetDragDropOp)
	static TSharedPtr<FComposableCameraInstanceDragDropOp> New(TSharedRef<FComposableCameraShotAuthoringSession> Session,
		double Duration, FString& Error);
	static TSharedPtr<FComposableCameraInstanceDragDropOp> FindForSnapshot(const UObject* Object);
	UComposableCameraInstanceData* GetSnapshot() const { return Snapshot.Get(); }
	bool CanDrop(const ISequencer& Sequencer, FString& Error) const;
	void UpdateFeedback(const ISequencer& Sequencer, FFrameNumber Frame, TRange<FFrameNumber>& PreviewRange);
	FFrameNumber DropFrame(const ISequencer& Sequencer) const;
	UMovieSceneComposableCameraInstanceTrack* Drop(TSharedRef<ISequencer> Sequencer, FFrameNumber Frame);
	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("CameraInstanceDragSnapshot"); }
private:
	TObjectPtr<UComposableCameraInstanceData> Snapshot;
	TObjectPtr<UMovieSceneSequence> SourceSequence;
	TWeakObjectPtr<UObject> SourceHost;
	TWeakPtr<FComposableCameraShotAuthoringSession> SourceSession;
	double DurationSeconds = 5.;
	FText Label;
	bool bDropped = false;
};
