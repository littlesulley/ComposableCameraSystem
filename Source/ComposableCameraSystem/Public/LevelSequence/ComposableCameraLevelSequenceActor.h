// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ComposableCameraLevelSequenceActor.generated.h"

class UComposableCameraLevelSequenceComponent;

/**
 * Actor dedicated to binding composable cameras into a Level Sequence.
 *
 * Structure
 * ─────────
 *   RootComponent = UCineCameraComponent (OutputCineCameraComponent)   ← viewport terminal
 *   Sibling        = UComposableCameraLevelSequenceComponent             ← logic/data driver
 *
 * Mirrors ACineCameraActor's shape: the CineCamera is the Actor's root
 * component, so every native UE path ("find a UCameraComponent on this
 * actor", Camera Cut Track, viewport Pilot, Sequencer's Camera Cut target
 * resolution) lands on it immediately. PCM::SetViewTarget's implicit-
 * activation filter hits its root-is-camera fast path identical to how it
 * handles CineCameraActor.
 *
 * The LevelSequenceComponent is a plain UActorComponent (no transform) —
 * it holds the TypeAssetReference bag and drives the internal CCS camera,
 * projecting each tick's pose onto the CineCamera.
 *
 * Spawnable-only
 * ──────────────
 * Marked NotPlaceable so it cannot be dragged into a level directly — this
 * camera's lifetime is owned by Sequencer (the Spawnable binding spawns it on
 * section entry, destroys it on section exit). A free-standing actor in the
 * level has no meaning: no Sequencer means no section ⇒ no evaluation signal
 * (see UComposableCameraLevelSequenceComponent::SetEvaluationEnabled).
 *
 * NotPlaceable does NOT prevent Sequencer's spawn register from instantiating
 * the class — it only hides it from the "Place Actors" panel and editor drag
 * operations, which is exactly what we want.
 *
 * Runtime driver
 * ──────────────
 * All real work lives on the LevelSequenceComponent — the actor has no
 * per-frame responsibilities of its own.
 */
UCLASS(NotPlaceable, BlueprintType, ClassGroup = ComposableCameraSystem,
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

	/** Logic-and-data driver ActorComponent. Holds the TypeAssetReference
	 *  (TypeAsset + Parameters / Variables bags), spawns the transient
	 *  internal CCS camera each tick, and projects the resulting pose onto
	 *  the CineCamera root component. Not the Actor's root — the CineCamera
	 *  is, so native UCameraComponent lookups resolve immediately. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Composable Camera",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UComposableCameraLevelSequenceComponent> LevelSequenceComponent;
};
