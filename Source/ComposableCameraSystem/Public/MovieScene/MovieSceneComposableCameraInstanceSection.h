// Copyright 2026 Sulley. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Channels/MovieSceneBoolChannel.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneIntegerChannel.h"
#include "EntitySystem/IMovieSceneEntityProvider.h"
#include "MovieScene/ComposableCameraInstanceData.h"
#include "MovieSceneSection.h"
#include "KeyParams.h"
#include "MovieSceneComposableCameraInstanceSection.generated.h"

class UMovieSceneComposableCameraInstanceSection;
#if WITH_EDITOR
DECLARE_DELEGATE_RetVal_TwoParams(TOptional<double>, FComposableCameraInstanceEditorValue, const UMovieSceneComposableCameraInstanceSection*, FName);
#endif

USTRUCT()
struct COMPOSABLECAMERASYSTEM_API FComposableCameraInstanceChannel
{
	GENERATED_BODY()
	UPROPERTY()
	FName Input;
	UPROPERTY()
	EComposableCameraInstanceFieldType Type = EComposableCameraInstanceFieldType::Number;
	UPROPERTY()
	FMovieSceneDoubleChannel Number;
	UPROPERTY()
	FMovieSceneIntegerChannel Integer;
	UPROPERTY()
	FMovieSceneBoolChannel Boolean;
};

/** An activation/animation interval of its owning instance, not another camera instance. */
UCLASS()
class COMPOSABLECAMERASYSTEM_API UMovieSceneComposableCameraInstanceSection : public UMovieSceneSection, public IMovieSceneEntityProvider
{
	GENERATED_BODY()
public:
	UMovieSceneComposableCameraInstanceSection(const FObjectInitializer& Initializer);
	UPROPERTY()
	TArray<FComposableCameraInstanceChannel> Channels;
	bool AddKey(const FComposableCameraInstanceField& Field, FFrameNumber Time, EMovieSceneKeyInterpolation Interpolation = EMovieSceneKeyInterpolation::Auto);
	void Evaluate(FFrameTime Time, TArrayView<const FComposableCameraInstanceField> Fields) const;
	void RebuildChannelProxy();
	virtual EMovieSceneChannelProxyType CacheChannelProxy() override;
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditUndo() override;
	/** Native key buttons can capture a pending Shot Editor value without depending on the editor module. */
	static FComposableCameraInstanceEditorValue OnGetEditorValue;
#endif
	virtual void ImportEntityImpl(UMovieSceneEntitySystemLinker* Linker,
		const UE::MovieScene::FEntityImportParams& Params, UE::MovieScene::FImportedEntity* Entity) override;
};
