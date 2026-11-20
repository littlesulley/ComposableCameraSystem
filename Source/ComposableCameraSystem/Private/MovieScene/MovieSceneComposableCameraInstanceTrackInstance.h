// Copyright 2026 Sulley. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "EntitySystem/TrackInstance/MovieSceneTrackInstance.h"
#include "MovieScene/ComposableCameraInstanceData.h"
#include "MovieScene/ComposableCameraInstanceTime.h"
#include "MovieSceneComposableCameraInstanceTrackInstance.generated.h"

class UComposableCameraLevelSequenceComponent;

USTRUCT()
struct FComposableCameraInstanceEvaluation
{
	GENERATED_BODY()
	UPROPERTY()
	FMovieSceneTrackInstanceInput Input;
	UPROPERTY()
	TObjectPtr<UComposableCameraInstanceData> Values;
	UPROPERTY()
	TWeakObjectPtr<UComposableCameraLevelSequenceComponent> Output;
	FGuid SourceSignature;
	FGuid SectionSignature;
	TArray<FComposableCameraInstanceField> Fields;
	TArray<double> Defaults;
	TArray<FTransform> ActorTransforms;
	FFrameTime LastTime;
	bool bReset = true;
	FComposableCameraInstanceTime Clock;
};

UCLASS()
class UMovieSceneComposableCameraInstanceTrackInstance : public UMovieSceneTrackInstance
{
	GENERATED_BODY()
private:
#if WITH_EDITOR
	virtual void OnInitialize() override;
	void OnSubjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event);
#endif
	virtual void OnAnimate() override;
	virtual void OnInputAdded(const FMovieSceneTrackInstanceInput& Input) override;
	virtual void OnInputRemoved(const FMovieSceneTrackInstanceInput& Input) override;
	virtual void OnDestroyed() override;
	UPROPERTY()
	TArray<FComposableCameraInstanceEvaluation> Evaluations;
};
