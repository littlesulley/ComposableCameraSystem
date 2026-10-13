// Copyright 2026 Sulley. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "EntitySystem/TrackInstance/MovieSceneTrackInstance.h"
#include "MovieScene/ComposableCameraInstanceData.h"
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
	bool bReset = true;
};

UCLASS()
class UMovieSceneComposableCameraInstanceTrackInstance : public UMovieSceneTrackInstance
{
	GENERATED_BODY()
private:
	virtual void OnAnimate() override;
	virtual void OnInputAdded(const FMovieSceneTrackInstanceInput& Input) override;
	virtual void OnInputRemoved(const FMovieSceneTrackInstanceInput& Input) override;
	virtual void OnDestroyed() override;
	UPROPERTY()
	TArray<FComposableCameraInstanceEvaluation> Evaluations;
};
