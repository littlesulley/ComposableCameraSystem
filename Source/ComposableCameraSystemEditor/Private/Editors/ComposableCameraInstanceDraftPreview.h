// Copyright 2026 Sulley. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MovieScene/ComposableCameraInstanceData.h"
#include "MovieScene/ComposableCameraInstanceTime.h"
#include "UObject/GCObject.h"

class ISequencer;
class AComposableCameraLevelSequenceActor;
class UCineCameraComponent;

/** Session-owned LS evaluator. No binding, Track, Cut or persistent level actor. */
class FComposableCameraInstanceDraftPreview : public FGCObject
{
public:
	~FComposableCameraInstanceDraftPreview();
	void Update(const UComposableCameraInstanceData& Source, ISequencer& Sequencer, bool bInvalidated);
	void Release();
	UCineCameraComponent* GetCamera(bool bRequireValidResult = true) const;
	const FText& GetStatus() const { return Status; }
	bool IsSubjectObject(const UObject* Object) const;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FComposableCameraInstanceDraftPreview"); }
private:
	struct FSubjectSample
	{
		int32 FieldIndex = INDEX_NONE;
		FMovieSceneObjectBindingID Binding;
		TWeakObjectPtr<AActor> Actor;
		FTransform Transform = FTransform::Identity;
		bool bRequired = false;
	};
	TObjectPtr<AComposableCameraLevelSequenceActor> PreviewActor;
	TObjectPtr<UComposableCameraInstanceData> Values;
	TArray<FComposableCameraInstanceField> Fields;
	TArray<FSubjectSample> Subjects;
	FComposableCameraInstanceTime Clock;
	FFrameTime LastTime;
	FText Status;
	FText CompatibilityWarning;
	FName MissingInput;
	bool bValidResult = false;
};
