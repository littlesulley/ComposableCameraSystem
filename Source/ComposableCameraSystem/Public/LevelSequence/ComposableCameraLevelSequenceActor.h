// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ComposableCameraLevelSequenceActor.generated.h"

class UComposableCameraLevelSequenceComponent;
class UCineCameraComponent;

/**
 * Actor dedicated to binding composable cameras into a Level Sequence.
 *
 * Structure
 * ---------
 *   RootComponent = UCineCameraComponent (OutputCineCameraComponent)   ->viewport terminal
 *   Sibling        = UComposableCameraLevelSequenceComponent->logic/data driver
 *
 * Mirrors ACineCameraActor's shape: the CineCamera is the Actor's root
 * component, so every native UE path ("find a UCameraComponent on this
 * actor", Camera Cut Track, viewport Pilot, Sequencer's Camera Cut target
 * resolution) lands on it immediately. PCM::SetViewTarget's implicit-
 * activation filter hits its root-is-camera fast path identical to how it
 * handles CineCameraActor.
 *
 * The LevelSequenceComponent is a plain UActorComponent (no transform) - it holds the TypeAssetReference bag and drives the internal CCS camera,
 * projecting each tick's pose onto the CineCamera.
 *
 * Placement
 * ---------
 * Placeable in the level (Place Actors panel) AND usable as a Sequencer
 * Spawnable / Possessable. The latter two paths remain the most common - the Sequencer Spawnable binding spawns the actor on section entry and
 * destroys it on exit; a Possessable binds an existing in-level instance.
 * Direct level placement is supported for designers who want to author a
 * camera against a Sequencer that always exists in the level (e.g. an
 * in-level cinematic trigger that drives the same actor every play).
 *
 * Without an active Sequencer driving the LevelSequenceComponent, a
 * free-standing instance is a no-op (see
 * UComposableCameraLevelSequenceComponent::SetEvaluationEnabled. The
 * component still ticks, but with no Shot / Patch overrides to apply it
 * just projects the InternalCamera's default pose). This is by design;
 * the actor doesn't error on a "lonely" instance.
 *
 * Runtime driver
 * --------------
 * All real work lives on the LevelSequenceComponent. The actor has no
 * per-frame responsibilities of its own.
 */
UCLASS(BlueprintType, ClassGroup = ComposableCameraSystem,
	meta = (DisplayName = "Composable Camera Level Sequence Actor",
	        ShortTooltip = "A composable camera bound to a Level Sequence."))
class COMPOSABLECAMERASYSTEM_API AComposableCameraLevelSequenceActor : public AActor
{
	GENERATED_BODY()

public:
	AComposableCameraLevelSequenceActor(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Output viewport-terminal CineCamera. Same component the actor's
	 *  inherited `RootComponent` field points at. The dedicated
	 *  `UPROPERTY` here surfaces it in the Details panel's Components tree
	 *  so designers can author per-instance lens / filmback / aperture
	 *  / post-process / focus settings (all the standard
	 *  `UCineCameraComponent` properties). Without an explicit `UPROPERTY`,
	 *  the native default subobject still exists at runtime but isn't
	 *  picked up by the Details panel's component-tree walk, so its
	 *  internals would render uneditable.
	 *
	 *  `VisibleAnywhere` on the pointer (component identity is fixed - can't reassign to a different component) plus the standard
	 *  `EditAnywhere` flags on `UCineCameraComponent`'s own UPROPERTYs
	 *  give the desired surface: pointer is read-only (don't replace the
	 *  component), but the component's properties are designer-editable. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Composable Camera",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCineCameraComponent> OutputCineCameraComponent;

	/** Logic-and-data driver ActorComponent. Holds the TypeAssetReference
	 *  (TypeAsset + Parameters / Variables bags), spawns the transient
	 *  internal CCS camera each tick, and projects the resulting pose onto
	 *  the CineCamera root component. Not the Actor's root. The CineCamera
	 *  is, so native UCameraComponent lookups resolve immediately. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Composable Camera",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UComposableCameraLevelSequenceComponent> LevelSequenceComponent;
};
