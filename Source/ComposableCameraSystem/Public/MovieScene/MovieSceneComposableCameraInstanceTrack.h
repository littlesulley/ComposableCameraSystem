// Copyright 2026 Sulley. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MovieSceneNameableTrack.h"
#include "MovieSceneObjectBindingID.h"
#include "MovieSceneComposableCameraInstanceTrack.generated.h"

class UComposableCameraInstanceData;
class UMovieSceneCameraCutSection;
class UMovieSceneComposableCameraInstanceSection;
class UMovieScene;
struct FComposableCameraInstanceField;

/** One instance per object binding. This track owns configuration and activation; sibling tracks own parameters. */
UCLASS()
class COMPOSABLECAMERASYSTEM_API UMovieSceneComposableCameraInstanceTrack : public UMovieSceneNameableTrack
{
	GENERATED_BODY()
public:
	UMovieSceneComposableCameraInstanceTrack(const FObjectInitializer& Initializer);
	UPROPERTY(EditAnywhere, Instanced, Category = "Camera Instance")
	TObjectPtr<UComposableCameraInstanceData> Instance;
	UPROPERTY()
	FMovieSceneObjectBindingID OutputBinding;
	/** Only this generated Cut may be maintained by the instance editor. */
	UPROPERTY()
	TObjectPtr<UMovieSceneCameraCutSection> ManagedCut;
	/** Also accepts prototype root tracks, for non-destructive editor migration. */
	bool IsInMovieScene() const;
	static UMovieSceneComposableCameraInstanceTrack* Find(UMovieScene* Scene, FGuid Binding);
	static UMovieSceneComposableCameraInstanceTrack* FindForSection(const UMovieSceneSection& Section);
	static UMovieSceneComposableCameraInstanceSection* FindActiveSection(const UMovieSceneTrack& Track, FFrameTime Time);
	void EvaluateParameters(FFrameTime Time, TArrayView<const FComposableCameraInstanceField> Fields) const;
	virtual void AddSection(UMovieSceneSection& Section) override;
	virtual bool SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const override;
	virtual UMovieSceneSection* CreateNewSection() override;
	virtual bool SupportsMultipleRows() const override { return false; }
	virtual const TArray<UMovieSceneSection*>& GetAllSections() const override;
	virtual bool HasSection(const UMovieSceneSection& Section) const override;
	virtual bool IsEmpty() const override;
	virtual void RemoveSection(UMovieSceneSection& Section) override;
	virtual void RemoveSectionAt(int32 Index) override;
	virtual void RemoveAllAnimationData() override;
#if WITH_EDITORONLY_DATA
	virtual FText GetDefaultDisplayName() const override;
#endif
private:
	UPROPERTY()
	TArray<TObjectPtr<UMovieSceneSection>> Sections;
};
