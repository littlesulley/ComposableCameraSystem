// Copyright Sulley. All rights reserved.

#include "LevelSequence/ComposableCameraLevelSequenceActor.h"

#include "CineCameraComponent.h"
#include "ComposableCameraSystemModule.h"
#include "LevelSequence/ComposableCameraLevelSequenceComponent.h"

AComposableCameraLevelSequenceActor::AComposableCameraLevelSequenceActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// CineCamera IS the RootComponent. This mirrors ACineCameraActor's shape
	// and lets every native UE path ("find a UCameraComponent on this actor",
	// Camera Cut Track, viewport Pilot, FindComponentByClass<UCameraComponent>)
	// resolve to the root immediately. PCM::SetViewTarget's implicit-activation
	// filter hits its root-is-camera fast path for us, identical to how it
	// handles CineCameraActor. The dedicated `OutputCineCameraComponent`
	// UPROPERTY (declared on the actor) is what surfaces the component in
	// the Details panel — without that, native default subobjects exist at
	// runtime but aren't picked up by the component-tree walk and their
	// internals render uneditable.
	OutputCineCameraComponent = CreateDefaultSubobject<UCineCameraComponent>(TEXT("OutputCineCameraComponent"));
	RootComponent = OutputCineCameraComponent;

	// LevelSequenceComponent is a pure UActorComponent (no transform) — just
	// a logic/data driver. Hand it the CineCamera reference so it knows where
	// to project poses.
	LevelSequenceComponent = CreateDefaultSubobject<UComposableCameraLevelSequenceComponent>(TEXT("LevelSequenceComponent"));
	LevelSequenceComponent->OutputCineCameraComponent = OutputCineCameraComponent;

	// Actor-level tick is driven by the component itself; the actor has no
	// per-frame work of its own. Keeping actor tick off avoids a redundant
	// function-call chain per frame for components that already tick.
	PrimaryActorTick.bCanEverTick = false;
}

void AComposableCameraLevelSequenceActor::BeginPlay()
{
	Super::BeginPlay();

	// Logged at Verbose so the Spawnable lifecycle is observable without
	// spamming the default Output Log. Enable with:
	//   Log LogComposableCameraSystem Verbose
	// in the editor console to verify Sequencer is creating / destroying this
	// actor at the correct section boundaries.
	UE_LOG(LogComposableCameraSystem, Verbose,
		TEXT("AComposableCameraLevelSequenceActor::BeginPlay %s"), *GetName());
}

void AComposableCameraLevelSequenceActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UE_LOG(LogComposableCameraSystem, Verbose,
		TEXT("AComposableCameraLevelSequenceActor::EndPlay %s (reason=%d)"),
		*GetName(), static_cast<int32>(EndPlayReason));

	Super::EndPlay(EndPlayReason);
}
