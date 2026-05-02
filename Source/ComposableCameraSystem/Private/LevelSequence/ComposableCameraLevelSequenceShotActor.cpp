// Copyright Sulley. All rights reserved.

#include "LevelSequence/ComposableCameraLevelSequenceShotActor.h"

#include "ComposableCameraSystemModule.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "LevelSequence/ComposableCameraLevelSequenceComponent.h"
#include "Nodes/ComposableCameraCompositionFramingNode.h"
#include "Nodes/ComposableCameraNodePinTypes.h"

AComposableCameraLevelSequenceShotActor::AComposableCameraLevelSequenceShotActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Real init happens in PostInitProperties — the inherited
	// LevelSequenceComponent must already exist before we can assign its
	// TypeAssetReference.TypeAsset, and component creation is finalized by
	// the time PostInitProperties fires.
}

void AComposableCameraLevelSequenceShotActor::PostInitProperties()
{
	Super::PostInitProperties();

	// Skip CDO — the class default object should not own a runtime-only
	// TypeAsset instance. Real instances run this path normally.
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}

	EnsureDefaultShotTypeAsset();
}

void AComposableCameraLevelSequenceShotActor::EnsureDefaultShotTypeAsset()
{
	if (!LevelSequenceComponent)
	{
		// LevelSequenceComponent was created by the base class's constructor
		// via CreateDefaultSubobject. If it's missing here, something has
		// gone very wrong with the actor's construction — bail rather than
		// crash. Logged at Warning so a misconfigured subclass shows up.
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("AComposableCameraLevelSequenceShotActor::EnsureDefaultShotTypeAsset: "
			     "LevelSequenceComponent is null on '%s' — cannot wire default TypeAsset."),
			*GetName());
		return;
	}

	// Designer-supplied custom TypeAsset wins — only seed when the slot is
	// null (or when our owned default has been GC'd / detached). This makes
	// the override path "set TypeAsset in Details panel" lossless: once a
	// designer assigns a custom TypeAsset, subsequent PostInitProperties
	// calls (duplicate spawning, hot reload) won't stomp it.
	if (LevelSequenceComponent->TypeAssetReference.TypeAsset)
	{
		return;
	}

	// Use existing owned asset if it survived an actor duplicate; otherwise
	// build a fresh one. NewObject with `this` as the outer makes the asset
	// part of the actor's serialization graph, so save/load / duplication
	// carry it along automatically.
	if (!DefaultShotTypeAsset)
	{
		UComposableCameraTypeAsset* Asset = NewObject<UComposableCameraTypeAsset>(
			this, UComposableCameraTypeAsset::StaticClass(),
			TEXT("DefaultShotTypeAsset"), RF_Public);

		// Single CompositionFramingNode — outered to the asset (matches the
		// editor-graph path's invariant that NodeTemplates entries are
		// outered to the type asset).
		UComposableCameraCompositionFramingNode* FramingNode =
			NewObject<UComposableCameraCompositionFramingNode>(
				Asset, UComposableCameraCompositionFramingNode::StaticClass(),
				NAME_None, RF_Public);

		Asset->NodeTemplates.Add(FramingNode);

		// NodePinOverrides invariant: NodePinOverrides.Num() ==
		// NodeTemplates.Num(). Pin defaults come from the C++ declaration on
		// CompositionFramingNode (no per-instance overrides in the default).
		Asset->NodePinOverrides.AddDefaulted();

		// Execution chain — single camera-node entry. The runtime
		// instantiator (ConstructCameraFromTypeAsset) prefers FullExecChain
		// when non-empty; ExecutionOrder is the camera-node-only projection
		// kept in lockstep for fast iteration paths.
		FComposableCameraExecEntry Entry;
		Entry.EntryType = EComposableCameraExecEntryType::CameraNode;
		Entry.CameraNodeIndex = 0;
		Asset->FullExecChain.Add(Entry);
		Asset->ExecutionOrder.Add(0);

		DefaultShotTypeAsset = Asset;
	}

	LevelSequenceComponent->TypeAssetReference.TypeAsset = DefaultShotTypeAsset;
	LevelSequenceComponent->TypeAssetReference.RebuildBagsFromTypeAsset();
}
