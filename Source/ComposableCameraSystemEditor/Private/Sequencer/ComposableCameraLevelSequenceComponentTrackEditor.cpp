// Copyright Sulley. All rights reserved.

#include "Sequencer/ComposableCameraLevelSequenceComponentTrackEditor.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ComposableCameraSystemEditorModule.h"
#include "ContentBrowserModule.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameFramework/Actor.h"
#include "IContentBrowserSingleton.h"
#include "ISequencer.h"
#include "KeyPropertyParams.h"
#include "LevelSequence/ComposableCameraLevelSequenceActor.h"
#include "LevelSequence/ComposableCameraLevelSequenceComponent.h"
#include "LevelSequence/ComposableCameraTypeAssetReference.h"
#include "MovieSceneSequence.h"
#include "ScopedTransaction.h"
#include "StructUtils/PropertyBag.h"
#include "Widgets/Layout/SBox.h"

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

void FComposableCameraLevelSequenceComponentTrackEditor::BuildObjectBindingContextMenu(
	FMenuBuilder& MenuBuilder, const TArray<FGuid>& ObjectBindings, const UClass* ObjectClass)
{
	// Same gating as ExtendObjectBindingTrackMenu: only attach when the
	// binding resolves to our component or to AComposableCameraLevelSequenceActor.
	if (!ObjectClass)
	{
		return;
	}
	const bool bIsComponentBinding = ObjectClass->IsChildOf<UComposableCameraLevelSequenceComponent>();
	const bool bIsActorBinding     = ObjectClass->IsChildOf<AComposableCameraLevelSequenceActor>();
	if (!bIsComponentBinding && !bIsActorBinding)
	{
		return;
	}

	// Multi-selection not supported (mirrors Epic's editor and our + Track menu).
	if (ObjectBindings.Num() != 1)
	{
		return;
	}

	if (!GetComponentForBinding(ObjectBindings[0]))
	{
		return;
	}

	// Right-click menu: only the Type Asset picker. Parameter / variable
	// channels stay under "+ Track" (their natural home — they create new
	// tracks on the binding, which is what the + button is for).
	BuildCameraTypeAssetSection(MenuBuilder, ObjectBindings);
}

void FComposableCameraLevelSequenceComponentTrackEditor::BuildCameraTypeAssetSection(
	FMenuBuilder& MenuBuilder, TArray<FGuid> ObjectBindings)
{
	UComposableCameraLevelSequenceComponent* Component = GetComponentForBinding(ObjectBindings[0]);
	if (!Component)
	{
		return;
	}

	const FString CurrentAssetName = Component->TypeAssetReference.TypeAsset
		? Component->TypeAssetReference.TypeAsset->GetName()
		: TEXT("(none)");
	MenuBuilder.BeginSection(TEXT("CameraTypeAsset"),
		FText::Format(LOCTEXT("CameraTypeAssetSection",
			"Camera Type Asset: {0}"), FText::FromString(CurrentAssetName)));
	MenuBuilder.AddSubMenu(
		LOCTEXT("CameraTypeAssetSubMenu", "Choose Camera Type Asset…"),
		LOCTEXT("CameraTypeAssetSubMenuTooltip",
			"Pick a UComposableCameraTypeAsset for this LS Actor's camera. "
			"Replaces editing TypeAssetReference.TypeAsset in the Details panel. "
			"Renames the binding to the asset name when the binding still has "
			"its default class label."),
		FNewMenuDelegate::CreateSP(
			this,
			&FComposableCameraLevelSequenceComponentTrackEditor::AddCameraTypeAssetSubMenu,
			ObjectBindings));
	MenuBuilder.EndSection();
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
		// No TypeAsset → no bag entries to expose. The Type Asset picker now
		// lives on the right-click context menu (BuildObjectBindingContextMenu)
		// — the "+ Track" menu only surfaces parameter / variable channels.
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

void FComposableCameraLevelSequenceComponentTrackEditor::AddCameraTypeAssetSubMenu(
	FMenuBuilder& MenuBuilder, TArray<FGuid> ObjectBindings)
{
	TSharedPtr<ISequencer> SequencerPtr = GetSequencer();
	UMovieSceneSequence* Sequence = SequencerPtr ? SequencerPtr->GetFocusedMovieSceneSequence() : nullptr;

	// Asset picker — filter to UComposableCameraTypeAsset and subclasses
	// (UComposableCameraPatchTypeAsset is technically a subclass; surfacing it
	// here lets advanced users opt to use a Patch asset as the base camera if
	// they really want, though the UX is unusual). Same FAssetPickerConfig
	// shape FCameraShakeTrackEditor uses.
	FAssetPickerConfig AssetPickerConfig;
	{
		AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateRaw(
			this,
			&FComposableCameraLevelSequenceComponentTrackEditor::OnCameraTypeAssetSelected,
			ObjectBindings);
		AssetPickerConfig.OnAssetEnterPressed = FOnAssetEnterPressed::CreateRaw(
			this,
			&FComposableCameraLevelSequenceComponentTrackEditor::OnCameraTypeAssetEnterPressed,
			ObjectBindings);
		AssetPickerConfig.bAllowNullSelection      = false;
		AssetPickerConfig.bAddFilterUI             = true;
		AssetPickerConfig.bShowTypeInColumnView    = false;
		AssetPickerConfig.InitialAssetViewType     = EAssetViewType::List;
		AssetPickerConfig.SaveSettingsName         = TEXT("ComposableCameraSequencerTypeAssetPicker");
		AssetPickerConfig.AdditionalReferencingAssets.Add(FAssetData(Sequence));

		// Filter to UComposableCameraTypeAsset (and subclasses, including
		// UComposableCameraPatchTypeAsset). Asset Registry doesn't auto-resolve
		// subclass relationships for non-Blueprint UCLASSes via class path
		// alone; we add the base class path and let the picker show all assets
		// of that class — UComposableCameraPatchTypeAsset assets will appear
		// because they're saved with their full class identity in the registry.
		AssetPickerConfig.Filter.ClassPaths.Add(UComposableCameraTypeAsset::StaticClass()->GetClassPathName());
		AssetPickerConfig.Filter.bRecursiveClasses = true;
	}

	FContentBrowserModule& ContentBrowserModule =
		FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	// Hardcode dims to avoid pulling in USequencerSettings — the engine's
	// CameraShakeTrackEditor uses Sequencer's saved settings for these but
	// the dependency isn't worth dragging in for two floats. 500x400 matches
	// the engine's documented defaults for FAssetPickerConfig.
	const float WidthOverride  = 500.f;
	const float HeightOverride = 400.f;

	TSharedPtr<SBox> MenuEntry = SNew(SBox)
		.WidthOverride(WidthOverride)
		.HeightOverride(HeightOverride)
		[
			ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
		];

	MenuBuilder.AddWidget(MenuEntry.ToSharedRef(), FText::GetEmpty(), true);
}

void FComposableCameraLevelSequenceComponentTrackEditor::OnCameraTypeAssetSelected(
	const FAssetData& AssetData, TArray<FGuid> ObjectBindings)
{
	FSlateApplication::Get().DismissAllMenus();

	UComposableCameraTypeAsset* SelectedAsset = Cast<UComposableCameraTypeAsset>(AssetData.GetAsset());
	if (!SelectedAsset || ObjectBindings.Num() == 0)
	{
		return;
	}

	// Resolve the inner FProperty for TypeAssetReference.TypeAsset so we can
	// route through the standard PreEditChange / PostEditChangeProperty pair.
	// This mirrors what the Details-panel asset picker does — the Component's
	// PostEditChangeProperty override sees PropertyName == "TypeAsset" and
	// runs RebuildBagsFromTypeAsset + RebuildInternalCamera. Going through
	// the formal property pipeline (instead of calling those two methods
	// directly from the Slate menu callback) avoids a crash where component
	// destruction fires while Slate / Sequencer are still mid-paint with
	// references to the soon-to-be-replaced InternalCamera.
	FProperty* RefProp = UComposableCameraLevelSequenceComponent::StaticClass()->FindPropertyByName(
		GET_MEMBER_NAME_CHECKED(UComposableCameraLevelSequenceComponent, TypeAssetReference));
	FStructProperty* RefStructProp = CastField<FStructProperty>(RefProp);
	FProperty* TypeAssetProp = (RefStructProp && RefStructProp->Struct)
		? RefStructProp->Struct->FindPropertyByName(
			GET_MEMBER_NAME_CHECKED(FComposableCameraTypeAssetReference, TypeAsset))
		: nullptr;

	const FScopedTransaction Transaction(LOCTEXT("SetCameraTypeAsset", "Set Camera Type Asset"));

	for (const FGuid& Binding : ObjectBindings)
	{
		UComposableCameraLevelSequenceComponent* Component = GetComponentForBinding(Binding);
		if (!Component)
		{
			continue;
		}

		if (TypeAssetProp)
		{
			Component->PreEditChange(TypeAssetProp);
			Component->TypeAssetReference.TypeAsset = SelectedAsset;
			FPropertyChangedEvent ChangeEvent(TypeAssetProp, EPropertyChangeType::ValueSet);
			ChangeEvent.MemberProperty = RefProp;
			Component->PostEditChangeProperty(ChangeEvent);
		}
		else
		{
			// Reflection lookup failed (shouldn't happen). Fall back to direct
			// assignment + manual rebuild so the picker still functions.
			Component->Modify();
			Component->TypeAssetReference.TypeAsset = SelectedAsset;
			Component->NotifyTypeAssetExternallyChanged();
		}
	}
}

void FComposableCameraLevelSequenceComponentTrackEditor::OnCameraTypeAssetEnterPressed(
	const TArray<FAssetData>& AssetData, TArray<FGuid> ObjectBindings)
{
	if (AssetData.Num() > 0)
	{
		OnCameraTypeAssetSelected(AssetData[0], ObjectBindings);
	}
}

#undef LOCTEXT_NAMESPACE
