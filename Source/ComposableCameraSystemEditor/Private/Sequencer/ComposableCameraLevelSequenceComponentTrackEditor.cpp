// Copyright Sulley. All rights reserved.

#include "Sequencer/ComposableCameraLevelSequenceComponentTrackEditor.h"

#include "ComposableCameraSystemEditorModule.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameFramework/Actor.h"
#include "ISequencer.h"
#include "KeyPropertyParams.h"
#include "LevelSequence/ComposableCameraLevelSequenceActor.h"
#include "LevelSequence/ComposableCameraLevelSequenceComponent.h"
#include "LevelSequence/ComposableCameraTypeAssetReference.h"
#include "StructUtils/PropertyBag.h"

#define LOCTEXT_NAMESPACE "FComposableCameraLevelSequenceComponentTrackEditor"

TSharedRef<ISequencerTrackEditor> FComposableCameraLevelSequenceComponentTrackEditor::CreateTrackEditor(
	TSharedRef<ISequencer> OwningSequencer)
{
	return MakeShareable(new FComposableCameraLevelSequenceComponentTrackEditor(OwningSequencer));
}

FComposableCameraLevelSequenceComponentTrackEditor::FComposableCameraLevelSequenceComponentTrackEditor(
	TSharedRef<ISequencer> InSequencer)
	: FMovieSceneTrackEditor(InSequencer)
{
}

bool FComposableCameraLevelSequenceComponentTrackEditor::SupportsType(TSubclassOf<UMovieSceneTrack> Type) const
{
	// Pure menu extender: we do not own any UMovieSceneTrack subclass. The
	// tracks we spawn via Sequencer->KeyProperty are stock property tracks
	// (float / vector / object / enum / struct) chosen by Sequencer itself
	// based on the leaf FProperty's type.
	return false;
}

void FComposableCameraLevelSequenceComponentTrackEditor::ExtendObjectBindingTrackMenu(
	TSharedRef<FExtender> Extender,
	const TArray<FGuid>& ObjectBindings,
	const UClass* ObjectClass)
{
	if (!ObjectClass)
	{
		return;
	}

	// Two acceptable binding shapes:
	//   (a) binding resolves to the component directly (user added a component
	//       binding under the actor binding — this is the only fully-wired
	//       path today, see GatherParameterEntries).
	//   (b) binding resolves to AComposableCameraLevelSequenceActor — we still
	//       attach the extender to leave a future-compat hook, but
	//       GatherParameterEntries currently returns empty for actor bindings.
	const bool bIsComponentBinding = ObjectClass->IsChildOf<UComposableCameraLevelSequenceComponent>();
	const bool bIsActorBinding     = ObjectClass->IsChildOf<AComposableCameraLevelSequenceActor>();

	if (!bIsComponentBinding && !bIsActorBinding)
	{
		return;
	}

	Extender->AddMenuExtension(
		TEXT("Tracks"),
		EExtensionHook::After,
		nullptr,
		FMenuExtensionDelegate::CreateSP(
			this,
			&FComposableCameraLevelSequenceComponentTrackEditor::OnExtendObjectBindingTrackMenu,
			ObjectBindings));
}

UComposableCameraLevelSequenceComponent* FComposableCameraLevelSequenceComponentTrackEditor::GetComponentForBinding(
	const FGuid& ObjectBinding) const
{
	TSharedPtr<ISequencer> SequencerPtr = GetSequencer();
	if (!SequencerPtr.IsValid())
	{
		return nullptr;
	}

	UObject* BoundObject = SequencerPtr->FindSpawnedObjectOrTemplate(ObjectBinding);
	if (!BoundObject)
	{
		return nullptr;
	}

	if (UComposableCameraLevelSequenceComponent* Direct = Cast<UComposableCameraLevelSequenceComponent>(BoundObject))
	{
		return Direct;
	}

	if (AComposableCameraLevelSequenceActor* Actor = Cast<AComposableCameraLevelSequenceActor>(BoundObject))
	{
		// The actor exposes a private component as its root — reach it via a
		// component-type search so we don't need the actor to expose a getter.
		return Actor->FindComponentByClass<UComposableCameraLevelSequenceComponent>();
	}

	return nullptr;
}

UObject* FComposableCameraLevelSequenceComponentTrackEditor::GetBoundObjectForBinding(
	const FGuid& ObjectBinding) const
{
	TSharedPtr<ISequencer> SequencerPtr = GetSequencer();
	if (!SequencerPtr.IsValid())
	{
		return nullptr;
	}
	return SequencerPtr->FindSpawnedObjectOrTemplate(ObjectBinding);
}

void FComposableCameraLevelSequenceComponentTrackEditor::OnExtendObjectBindingTrackMenu(
	FMenuBuilder& MenuBuilder, TArray<FGuid> ObjectBindings)
{
	// Multi-selection not supported in this first cut (same as Epic's editor).
	if (ObjectBindings.Num() != 1)
	{
		return;
	}

	UObject* BoundObject = GetBoundObjectForBinding(ObjectBindings[0]);
	UComposableCameraLevelSequenceComponent* Component = GetComponentForBinding(ObjectBindings[0]);
	if (!BoundObject || !Component)
	{
		return;
	}
	if (!Component->TypeAssetReference.TypeAsset)
	{
		// No TypeAsset → no bag entries to expose.
		return;
	}

	TArray<FParameterMenuEntry> ParameterEntries;
	GatherParameterEntries(BoundObject, Component, /*bIsVariables=*/false, ParameterEntries);

	TArray<FParameterMenuEntry> VariableEntries;
	GatherParameterEntries(BoundObject, Component, /*bIsVariables=*/true, VariableEntries);

	if (ParameterEntries.Num() == 0 && VariableEntries.Num() == 0)
	{
		return;
	}

	// Flat two-section layout: each exposed parameter / variable is a leaf
	// entry directly under its section header. No submenus — one click adds
	// the track. Sections act as the only separator.
	if (ParameterEntries.Num() > 0)
	{
		MenuBuilder.BeginSection(TEXT("CameraParameters"),
			LOCTEXT("CameraParameters", "Camera Parameters"));
		for (const FParameterMenuEntry& Entry : ParameterEntries)
		{
			FUIAction AddAction(
				FExecuteAction::CreateSP(
					this,
					&FComposableCameraLevelSequenceComponentTrackEditor::AddParameterTrack,
					Entry,
					ObjectBindings[0]),
				FCanExecuteAction::CreateSP(
					this,
					&FComposableCameraLevelSequenceComponentTrackEditor::CanAddParameterTrack,
					Entry,
					ObjectBindings[0]));
			MenuBuilder.AddMenuEntry(
				Entry.DisplayName.IsEmpty() ? FText::FromName(Entry.Name) : Entry.DisplayName,
				FText::GetEmpty(),
				FSlateIcon(),
				AddAction);
		}
		MenuBuilder.EndSection();
	}

	if (VariableEntries.Num() > 0)
	{
		MenuBuilder.BeginSection(TEXT("CameraVariables"),
			LOCTEXT("CameraVariables", "Camera Variables"));
		for (const FParameterMenuEntry& Entry : VariableEntries)
		{
			FUIAction AddAction(
				FExecuteAction::CreateSP(
					this,
					&FComposableCameraLevelSequenceComponentTrackEditor::AddParameterTrack,
					Entry,
					ObjectBindings[0]),
				FCanExecuteAction::CreateSP(
					this,
					&FComposableCameraLevelSequenceComponentTrackEditor::CanAddParameterTrack,
					Entry,
					ObjectBindings[0]));
			MenuBuilder.AddMenuEntry(
				Entry.DisplayName.IsEmpty() ? FText::FromName(Entry.Name) : Entry.DisplayName,
				FText::GetEmpty(),
				FSlateIcon(),
				AddAction);
		}
		MenuBuilder.EndSection();
	}
}

void FComposableCameraLevelSequenceComponentTrackEditor::GatherParameterEntries(
	UObject* BoundObject,
	UComposableCameraLevelSequenceComponent* Component,
	bool bIsVariables,
	TArray<FParameterMenuEntry>& OutEntries) const
{
	if (!BoundObject || !Component)
	{
		return;
	}

	TSharedPtr<ISequencer> SequencerPtr = GetSequencer();
	if (!SequencerPtr.IsValid())
	{
		return;
	}

	// Build the chain of prefix properties:
	//   BoundObject.class
	//     → (if Actor) RootComponent / the component subobject — not a FProperty drill;
	//       instead, Sequencer's CanKeyProperty handles component traversal via
	//       the component binding. For actor bindings we'd need a different path
	//       starting at the actor's component reference. For first cut, require
	//       the binding to resolve to a UComposableCameraLevelSequenceComponent
	//       directly (bIsComponentBinding path), which is what Epic's editor
	//       requires too. Actor-binding support can be added later by injecting
	//       a component-name prefix into the property path.
	const UClass* ComponentClass = Component->GetClass();

	FProperty* RefProp = ComponentClass->FindPropertyByName(
		GET_MEMBER_NAME_CHECKED(UComposableCameraLevelSequenceComponent, TypeAssetReference));
	if (!RefProp)
	{
		return;
	}

	FStructProperty* RefStructProp = CastField<FStructProperty>(RefProp);
	if (!RefStructProp || !RefStructProp->Struct)
	{
		return;
	}

	const FName BagFieldName = bIsVariables ? FName(TEXT("Variables")) : FName(TEXT("Parameters"));
	FProperty* BagProp = RefStructProp->Struct->FindPropertyByName(BagFieldName);
	if (!BagProp)
	{
		return;
	}

	FStructProperty* BagStructProp = CastField<FStructProperty>(BagProp);
	if (!BagStructProp || !BagStructProp->Struct)
	{
		return;
	}

	// Inside an FInstancedPropertyBag, the actual values live under a field
	// named "Value" which is the backing FInstancedStruct. Sequencer's
	// property-path resolution walks into that struct to find the concrete
	// leaf FProperty the bag instantiated (via UPropertyBag::GetOrCreateFromDescs).
	FProperty* ValueProp = BagStructProp->Struct->FindPropertyByName(FName(TEXT("Value")));
	if (!ValueProp)
	{
		return;
	}

	// Iterate the per-instance UPropertyBag to see the bag's actual fields.
	FInstancedPropertyBag& Bag = bIsVariables
		? Component->TypeAssetReference.Variables
		: Component->TypeAssetReference.Parameters;

	const UPropertyBag* BagStruct = Bag.GetPropertyBagStruct();
	if (!BagStruct)
	{
		return;
	}

	// If the bound object is the component itself, the property path starts
	// at TypeAssetReference. If it's an actor containing the component, we'd
	// need a component-traversal prefix — skipped for first cut; users bind
	// the component directly (or accept the actor-binding path won't key).
	const UClass* BoundObjectClass = BoundObject->GetClass();
	const bool bIsComponentBoundDirectly = BoundObjectClass->IsChildOf<UComposableCameraLevelSequenceComponent>();

	if (!bIsComponentBoundDirectly)
	{
		// Actor-binding path isn't wired yet — menu stays empty for that case.
		return;
	}

	for (TFieldIterator<FProperty> It(BagStruct); It; ++It)
	{
		FProperty* LeafProp = *It;
		if (!LeafProp || LeafProp->HasAnyPropertyFlags(CPF_Deprecated))
		{
			continue;
		}

		FPropertyPath Path;
		Path.AddProperty(FPropertyInfo(RefProp));
		Path.AddProperty(FPropertyInfo(BagProp));
		Path.AddProperty(FPropertyInfo(ValueProp));
		Path.AddProperty(FPropertyInfo(LeafProp));

		if (!SequencerPtr->CanKeyProperty(FCanKeyPropertyParams(BoundObjectClass, Path)))
		{
			continue;
		}

		FParameterMenuEntry Entry;
		Entry.Name = LeafProp->GetFName();
		Entry.DisplayName = LeafProp->GetDisplayNameText();
		Entry.PropertyPath = Path;
		Entry.bIsFloat = LeafProp->IsA<FFloatProperty>() || LeafProp->IsA<FDoubleProperty>();
		OutEntries.Add(Entry);
	}

	OutEntries.Sort([](const FParameterMenuEntry& A, const FParameterMenuEntry& B)
	{
		return A.Name.LexicalLess(B.Name);
	});
}

void FComposableCameraLevelSequenceComponentTrackEditor::AddParameterTrack(
	FParameterMenuEntry Entry, FGuid ObjectBinding)
{
	TSharedPtr<ISequencer> SequencerPtr = GetSequencer();
	if (!SequencerPtr.IsValid())
	{
		return;
	}

	UObject* BoundObject = GetBoundObjectForBinding(ObjectBinding);
	if (!BoundObject)
	{
		return;
	}

	TArray<UObject*> KeyableBoundObjects;
	KeyableBoundObjects.Add(BoundObject);

	// Mirror Epic's mode choice: use ManualKey when AutoSetTrackDefaults is on,
	// ManualKeyForced otherwise. This matches how stock property track menu
	// entries behave on any UObject, so our entries feel native.
	const ESequencerKeyMode KeyMode = SequencerPtr->GetAutoSetTrackDefaults()
		? ESequencerKeyMode::ManualKey
		: ESequencerKeyMode::ManualKeyForced;

	FKeyPropertyParams Params(KeyableBoundObjects, Entry.PropertyPath, KeyMode);
	SequencerPtr->KeyProperty(Params);
}

bool FComposableCameraLevelSequenceComponentTrackEditor::CanAddParameterTrack(
	FParameterMenuEntry Entry, FGuid ObjectBinding) const
{
	return GetBoundObjectForBinding(ObjectBinding) != nullptr;
}

#undef LOCTEXT_NAMESPACE
