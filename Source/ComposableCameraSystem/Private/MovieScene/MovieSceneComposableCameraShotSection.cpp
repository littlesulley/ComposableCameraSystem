// Copyright Sulley. All rights reserved.

#include "MovieScene/MovieSceneComposableCameraShotSection.h"

#include "ComposableCameraSystemModule.h"
#include "DataAssets/ComposableCameraShotAsset.h"
#include "DataAssets/ComposableCameraTransitionDataAsset.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/MovieSceneEntityBuilder.h"
#include "EntitySystem/MovieSceneSequenceInstance.h"
#include "GameFramework/Actor.h"
#include "MovieScene/MovieSceneComposableCameraShotTrackInstance.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MovieSceneComposableCameraShotSection)

UMovieSceneComposableCameraShotSection::UMovieSceneComposableCameraShotSection(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Section IS the source of truth for "is the shot active". When the
	// playhead leaves the bounds the override is removed; gap between sections
	// -> CompositionFramingNode keeps the last-written Shot. Project-default
	// completion is the right baseline for an additive override system.
	EvalOptions.EnableAndSetCompletionMode(EMovieSceneCompletionMode::ProjectDefault);

	// Shot lifetime IS the section's authored range. Infinite ranges break
	// the top-row-winner picking (every infinite section would be "active"
	// forever).
	bSupportsInfiniteRange = false;
}

void UMovieSceneComposableCameraShotSection::ImportEntityImpl(
	UMovieSceneEntitySystemLinker* EntityLinker,
	const UE::MovieScene::FEntityImportParams& ImportParams,
	UE::MovieScene::FImportedEntity* OutImportedEntity)
{
	using namespace UE::MovieScene;

	UE_LOG(LogComposableCameraSystem, Verbose,
		TEXT("ShotSection::ImportEntityImpl fired for section '%s' (Source=%s)."),
		*GetName(),
		Source == EComposableCameraShotSource::Inline ? TEXT("Inline") : TEXT("AssetReference"));

	// Per-section TrackInstance dispatch. Same pattern as the Patch section.
	// The TrackInstance owns per-frame OnAnimate evaluation; this section is
	// purely a data carrier.
	FMovieSceneTrackInstanceComponent TrackInstance{
		decltype(FMovieSceneTrackInstanceComponent::Owner)(this),
		UMovieSceneComposableCameraShotTrackInstance::StaticClass()
	};

	OutImportedEntity->AddBuilder(
		FEntityBuilder()
		.AddTag(FBuiltInComponentTypes::Get()->Tags.Root)
		.Add(FBuiltInComponentTypes::Get()->TrackInstance, TrackInstance)
	);
}

const FComposableCameraShot* UMovieSceneComposableCameraShotSection::ResolveActiveShot() const
{
	switch (Source)
	{
		case EComposableCameraShotSource::Inline:
			return &InlineShot;

		case EComposableCameraShotSource::AssetReference:
		{
			return ShotAssetRef.IsNull() ? nullptr : &ShotOverrides;
		}
	}
	return nullptr;
}

FComposableCameraShot* UMovieSceneComposableCameraShotSection::ResolveActiveShot()
{
	switch (Source)
	{
		case EComposableCameraShotSource::Inline:
			return &InlineShot;

		case EComposableCameraShotSource::AssetReference:
		{
			return ShotAssetRef.IsNull() ? nullptr : &ShotOverrides;
		}
	}
	return nullptr;
}

FComposableCameraShot* UMovieSceneComposableCameraShotSection::ResolveShotEditorShot()
{
	switch (Source)
	{
		case EComposableCameraShotSource::Inline:
			return &InlineShot;

		case EComposableCameraShotSource::AssetReference:
			return ShotAssetRef.IsNull() ? nullptr : &ShotOverrides;
	}
	return nullptr;
}

UComposableCameraShotAsset* UMovieSceneComposableCameraShotSection::ResolveCachedShotAsset() const
{
	if (CachedShotAsset)
	{
		return CachedShotAsset;
	}
	// Free lookup -`.Get()` only returns non-null when the asset is
	// already in memory. No load triggered.
	if (UComposableCameraShotAsset* Loaded = ShotAssetRef.Get())
	{
		CachedShotAsset = Loaded;
		return Loaded;
	}
	return nullptr;
}

UComposableCameraTransitionDataAsset* UMovieSceneComposableCameraShotSection::ResolveCachedEnterTransition() const
{
	if (CachedEnterTransition)
	{
		return CachedEnterTransition;
	}
	if (UComposableCameraTransitionDataAsset* Loaded = EnterTransition.Get())
	{
		CachedEnterTransition = Loaded;
		return Loaded;
	}
	return nullptr;
}

void UMovieSceneComposableCameraShotSection::PostLoad()
{
	Super::PostLoad();
	RefreshCachedAssets();
	if (Source == EComposableCameraShotSource::AssetReference && !bShotOverridesInitialized)
	{
		RefreshShotOverridesFromSource();
	}
}

#if WITH_EDITOR
void UMovieSceneComposableCameraShotSection::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	const FName PropertyName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UMovieSceneComposableCameraShotSection, ShotAssetRef)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UMovieSceneComposableCameraShotSection, Source))
	{
		// Designer pointed the soft ref at a different asset (or flipped
		// Source). Drop the stale cache and re-resolve. LoadSynchronous
		// here is fine, we're outside of evaluation.
		CachedShotAsset = nullptr;
		CachedEnterTransition = nullptr;
		RefreshCachedAssets();
		RefreshShotOverridesFromSource();
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UMovieSceneComposableCameraShotSection, EnterTransition))
	{
		CachedEnterTransition = nullptr;
		RefreshCachedAssets();
	}
}
#endif

void UMovieSceneComposableCameraShotSection::RefreshCachedAssets()
{
	// Off-hot-path: blocking LoadSynchronous is acceptable here. PostLoad
	// runs on async-load completion (already off the game thread for cooked
	// builds), PostEditChangeProperty runs on a designer interaction. The
	// resolved hard pointer is then read via the cached members for every
	// subsequent eval-path call without ever blocking again.
	if (Source == EComposableCameraShotSource::AssetReference && !ShotAssetRef.IsNull())
	{
		CachedShotAsset = ShotAssetRef.LoadSynchronous();
	}
	if (!EnterTransition.IsNull())
	{
		CachedEnterTransition = EnterTransition.LoadSynchronous();
	}
}

UObject* UMovieSceneComposableCameraShotSection::ResolveShotEditorHost() const
{
	return const_cast<UMovieSceneComposableCameraShotSection*>(this);
}

bool UMovieSceneComposableCameraShotSection::BuildEffectiveShotWithoutBindings(FComposableCameraShot& OutShot) const
{
	switch (Source)
	{
		case EComposableCameraShotSource::Inline:
			OutShot = InlineShot;
			return true;

		case EComposableCameraShotSource::AssetReference:
			if (ShotAssetRef.IsNull())
			{
				return false;
			}
			OutShot = ShotOverrides;
			return true;
	}
	return false;
}

void UMovieSceneComposableCameraShotSection::RefreshShotOverridesFromSource()
{
	if (Source != EComposableCameraShotSource::AssetReference)
	{
		return;
	}

	UComposableCameraShotAsset* Asset = ResolveCachedShotAsset();
	if (!Asset)
	{
		return;
	}

	ShotOverrides = Asset->Shot;
	bShotOverridesInitialized = true;
}

bool UMovieSceneComposableCameraShotSection::BuildEffectiveShot(
	const UE::MovieScene::FSequenceInstance& Instance,
	FComposableCameraShot& OutShot) const
{
	if (!BuildEffectiveShotWithoutBindings(OutShot))
	{
		return false;
	}

	for (const FComposableCameraShotTargetActorOverride& Override : TargetActorOverrides)
	{
		if (Override.TargetIndex < 0)
		{
			// Negative index. Meta=(ClampMin="0") on the UPROPERTY should
			// already prevent this, but defensive skip in case of a hand-
			// edited asset / migration.
			continue;
		}
		if (!Override.Binding.IsValid())
		{
			continue;
		}

		const TArrayView<TWeakObjectPtr<>> Bound = Override.Binding.ResolveBoundObjects(Instance);
		if (Bound.Num() == 0)
		{
			// Binding hasn't spawned yet (Spawnable outside the active
			// section range, or pre-warm of the sequence). Leave this
			// target's actor as-is for this frame. Next frame's resolve
			// retries.
			continue;
		}
		AActor* Actor = Cast<AActor>(Bound[0].Get());
		if (!Actor)
		{
			continue;
		}

		// Auto-grow Targets to fit the override's TargetIndex. The original
		// design assumed the underlying Inline / ShotAsset Shot had a Targets
		// entry for every TargetIndex referenced by an override, so missing
		// indices were treated as "stale override, silent drop". In practice
		// designers configure TargetActorOverrides on a Section without first
		// adding placeholder Targets entries on the InlineShot. The UI
		// invites doing exactly that, and the silent-drop behavior produced a
		// PIE-only "Targets[] empty ->SolveShot fails -> CineCam at world
		// origin" failure that's invisible at edit time (the Shot Editor's
		// preview path takes a different code branch and shows the override
		// resolved correctly). Auto-growing makes the override slot itself
		// the source of truth for target presence; the Inline / ShotAsset
		// Shot only needs to author the *non-actor* fields of each target
		// (Bone, Offset, BoundsShape, Weight) for indices the designer cares
		// about beyond just the actor identity.
		if (!OutShot.Targets.IsValidIndex(Override.TargetIndex))
		{
			OutShot.Targets.SetNum(Override.TargetIndex + 1);
		}

		// TSoftObjectPtr<AActor> assignment from a raw AActor* captures
		// the actor's path. For Spawnables the path includes the unique
		// spawn-cycle suffix, so the LS Component's downstream `.Get()`
		// resolves to the same currently-spawned instance this frame.
		// Re-resolved next frame in case the Spawnable's identity churns.
		OutShot.Targets[Override.TargetIndex].Target.Actor = Actor;
	}

	return true;
}
