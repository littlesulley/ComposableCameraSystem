// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ISequencerTrackEditor.h"
#include "KeyPropertyParams.h"   // FPropertyPath (via PropertyPath.h transitively)
#include "MovieSceneTrackEditor.h"
#include "Templates/SharedPointerFwd.h"

class ISequencer;
class UComposableCameraLevelSequenceComponent;
class AComposableCameraLevelSequenceActor;

/**
 * Sequencer track editor that extends the right-click "Add Track" menu on
 * bindings of (or containing) a UComposableCameraLevelSequenceComponent.
 *
 * This class does NOT introduce a new UMovieSceneTrack type — it's a **menu
 * extender**. SupportsType() returns false. When the user right-clicks a
 * binding whose class matches our component (or an actor that owns one), we
 * add "Camera Parameters" and "Camera Variables" submenus listing the
 * TypeAsset's exposed entries, and each click emits a Sequencer->KeyProperty
 * call that materializes a stock property track (UMovieSceneFloatTrack /
 * UMovieSceneDoubleVectorTrack / UMovieSceneObjectPropertyTrack / …) on the
 * property path:
 *
 *     TypeAssetReference.Parameters.Value.{ParamName}
 *     TypeAssetReference.Variables.Value.{VarName}
 *
 * Once the track exists, Sequencer's normal evaluation writes the animated
 * value into the bag's backing FProperty each frame, and Phase D's per-tick
 * ApplyParameterBlock propagates that into the camera's runtime data block.
 *
 * Mirrors Epic's FGameplayCameraComponentTrackEditor
 * (Engine/Plugins/Cameras/GameplayCameras/Source/GameplayCamerasEditor/Private/Sequencer/
 *  GameplayCameraComponentTrackEditor.{h,cpp}).
 */
class FComposableCameraLevelSequenceComponentTrackEditor : public FMovieSceneTrackEditor
{
public:
	static TSharedRef<ISequencerTrackEditor> CreateTrackEditor(TSharedRef<ISequencer> OwningSequencer);

	explicit FComposableCameraLevelSequenceComponentTrackEditor(TSharedRef<ISequencer> InSequencer);

	// ISequencerTrackEditor.
	virtual void ExtendObjectBindingTrackMenu(
		TSharedRef<FExtender> Extender,
		const TArray<FGuid>& ObjectBindings,
		const UClass* ObjectClass) override;
	virtual bool SupportsType(TSubclassOf<UMovieSceneTrack> Type) const override;

private:
	/** Resolve a binding GUID to the actual LevelSequence component (handling
	 *  both the "binding class is the component" and "binding class is an
	 *  AComposableCameraLevelSequenceActor that owns the component" cases). */
	UComposableCameraLevelSequenceComponent* GetComponentForBinding(const FGuid& ObjectBinding) const;

	/** Resolve the object we should pass to Sequencer->KeyProperty as the
	 *  KeyableBoundObject. This is always the object the binding GUID refers
	 *  to — Sequencer uses its class to resolve the property path. */
	UObject* GetBoundObjectForBinding(const FGuid& ObjectBinding) const;

	/** Menu extension callback attached by ExtendObjectBindingTrackMenu. */
	void OnExtendObjectBindingTrackMenu(FMenuBuilder& MenuBuilder, TArray<FGuid> ObjectBindings);

	/** One keyable exposed entry (parameter or variable) collected from the
	 *  bound component's TypeAssetReference bags. */
	struct FParameterMenuEntry
	{
		FName Name;
		FText DisplayName;
		FPropertyPath PropertyPath;
		bool bIsFloat = false;
	};

	/** Walk the Parameters or Variables bag and collect one entry per keyable
	 *  leaf property. The property path starts at BoundObject's class and
	 *  drills into TypeAssetReference.[Parameters|Variables].Value.Leaf. */
	void GatherParameterEntries(
		UObject* BoundObject,
		UComposableCameraLevelSequenceComponent* Component,
		bool bIsVariables,
		TArray<FParameterMenuEntry>& OutEntries) const;

	/** Entry click handler: call Sequencer->KeyProperty with the property path. */
	void AddParameterTrack(FParameterMenuEntry Entry, FGuid ObjectBinding);
	bool CanAddParameterTrack(FParameterMenuEntry Entry, FGuid ObjectBinding) const;
};
