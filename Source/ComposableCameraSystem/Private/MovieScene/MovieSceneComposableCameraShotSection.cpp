// Copyright Sulley. All rights reserved.

#include "MovieScene/MovieSceneComposableCameraShotSection.h"

#include "ComposableCameraSystemModule.h"
#include "DataAssets/ComposableCameraShotAsset.h"
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
	// → CompositionFramingNode keeps the last-written Shot. Project-default
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

	// Per-section TrackInstance dispatch — same pattern as the Patch section.
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
			// LoadSynchronous is acceptable here — Sequencer evaluation already
			// loads bound objects synchronously, and Shot data is small. If
			// the asset is null or fails to load, return nullptr so the caller
			// can no-op (TrackInstance won't push an override; LS Component
			// holds last-written Shot).
			if (UComposableCameraShotAsset* Asset = ShotAssetRef.LoadSynchronous())
			{
				return &Asset->Shot;
			}
			return nullptr;
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
			if (UComposableCameraShotAsset* Asset = ShotAssetRef.LoadSynchronous())
			{
				return &Asset->Shot;
			}
			return nullptr;
		}
	}
	return nullptr;
}

UObject* UMovieSceneComposableCameraShotSection::ResolveShotEditorHost() const
{
	// The Shot Editor uses the host UObject for transaction context + dirty
	// flag + FNotifyHook routing. Inline edits go to the Section itself;
	// AssetReference edits go to the underlying asset (so changes propagate
	// to every Section referencing it).
	switch (Source)
	{
		case EComposableCameraShotSource::Inline:
			return const_cast<UMovieSceneComposableCameraShotSection*>(this);

		case EComposableCameraShotSource::AssetReference:
			return ShotAssetRef.LoadSynchronous();
	}
	return nullptr;
}

bool UMovieSceneComposableCameraShotSection::BuildEffectiveShot(
	const UE::MovieScene::FSequenceInstance& Instance,
	FComposableCameraShot& OutShot) const
{
	const FComposableCameraShot* SourceShot = ResolveActiveShot();
	if (!SourceShot)
	{
		return false;
	}

	// Value copy first — the LS Component's per-frame override map already
	// expects a fresh Shot per push, so working off a copy is the right
	// shape; mutating the source (Inline data on the Section, or the
	// ShotAsset's Shot field) would silently propagate per-section bindings
	// back into the asset for the AssetReference flow.
	OutShot = *SourceShot;

	for (const FComposableCameraShotTargetActorOverride& Override : TargetActorOverrides)
	{
		if (!OutShot.Targets.IsValidIndex(Override.TargetIndex))
		{
			// Stale overrides (e.g. ShotAsset edited to remove a target) —
			// silent drop. Logging here would spam every frame the section
			// is in range; the user-visible signal is "the override didn't
			// take effect", which is the correct outcome.
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
			// section range, or pre-warm of the sequence) — leave this
			// target's actor as-is for this frame. Next frame's resolve
			// retries.
			continue;
		}
		if (AActor* Actor = Cast<AActor>(Bound[0].Get()))
		{
			// TSoftObjectPtr<AActor> assignment from a raw AActor* captures
			// the actor's path. For Spawnables the path includes the unique
			// spawn-cycle suffix, so the LS Component's downstream `.Get()`
			// resolves to the same currently-spawned instance this frame.
			// Re-resolved next frame in case the Spawnable's identity churns.
			OutShot.Targets[Override.TargetIndex].Target.Actor = Actor;
		}
	}

	return true;
}
