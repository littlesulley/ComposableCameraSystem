// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LevelSequence/ComposableCameraLevelSequenceActor.h"
#include "ComposableCameraLevelSequenceShotActor.generated.h"

class UComposableCameraTypeAsset;

/**
 * Specialized `AComposableCameraLevelSequenceActor` whose
 * `LevelSequenceComponent` comes pre-wired with a system-managed default
 * `UComposableCameraTypeAsset`: a one-node TypeAsset whose only camera node is
 * `UComposableCameraCompositionFramingNode`.
 *
 * Shot Track authoring flow:
 *
 *     1. Designer drops an AComposableCameraLevelSequenceShotActor into the LS
 *        as Spawnable or Possessable.
 *     2. The Shot Actor's TypeAssetReference auto-populates with a built-in
 *        DefaultShotTypeAsset; designer never sees TypeAsset / graph editor /
 *        evaluation tree.
 *     3. Designer adds a Composable Camera Shot Track under the actor's
 *        binding row. Shot Sections push framing data into the
 *        CompositionFramingNode each frame.
 *
 * The default TypeAsset is created **in-memory per actor instance** (not as
 * an on-disk asset). This avoids:
 *   - bootstrap timing races against AssetRegistry availability,
 *   - ConstructorHelpers::FObjectFinder failures on first install,
 *   - the user accidentally deleting the system asset.
 *
 * The owning actor's lifetime carries the TypeAsset (the TypeAsset is
 * outered to this actor and serialized inline as part of the actor's
 * package), so duplication for Spawnable spawning auto-clones the
 * TypeAsset along with the actor.
 *
 * Power-user override path: the inherited `TypeAssetReference.TypeAsset`
 * field on the LevelSequenceComponent is still settable from the Details
 * panel. Setting a different TypeAsset there bypasses the default
 * (PostInitProperties only seeds when the field is null), so a designer
 * who wants a multi-node camera (e.g. CompositionFramingNode + a downstream
 * shake or noise modifier) can still do so by authoring a custom TypeAsset
 * and assigning it.
 */
UCLASS(BlueprintType, ClassGroup = ComposableCameraSystem,
	meta = (DisplayName = "Composable Camera Level Sequence Shot Actor",
	        ShortTooltip = "An LS Actor pre-wired for the Shot Track flow. Auto-includes a CompositionFramingNode."))
class COMPOSABLECAMERASYSTEM_API AComposableCameraLevelSequenceShotActor
	: public AComposableCameraLevelSequenceActor
{
	GENERATED_BODY()

public:
	AComposableCameraLevelSequenceShotActor(const FObjectInitializer& ObjectInitializer);

	/** Construct (or refresh) the default TypeAsset and assign it to the
	 *  inherited LevelSequenceComponent's TypeAssetReference. Idempotent - skips when a TypeAsset is already set, so designer-supplied custom
	 *  TypeAssets aren't stomped. Called from PostInitProperties so it runs
	 *  for every spawned instance, including Sequencer Spawnable duplication. */
	void EnsureDefaultShotTypeAsset();

protected:
	virtual void PostInitProperties() override;

private:
	/** Owned in-memory TypeAsset. Outered to this actor, lifetime bound by it.
	 *  Visible in the Details panel for inspection (the field shown on the
	 *  ShotActor itself, NOT the same field on LevelSequenceComponent), but
	 *  read-only. Designers don't author it. */
	UPROPERTY(VisibleInstanceOnly, Category = "Composable Camera|System",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UComposableCameraTypeAsset> DefaultShotTypeAsset;
};
