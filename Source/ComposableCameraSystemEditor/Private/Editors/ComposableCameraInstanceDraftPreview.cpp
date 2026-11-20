// Copyright 2026 Sulley. All Rights Reserved.
#include "Editors/ComposableCameraInstanceDraftPreview.h"

#include "DataAssets/ComposableCameraTypeAsset.h"
#include "CineCameraComponent.h"
#include "Engine/World.h"
#include "ISequencer.h"
#include "LevelSequence/ComposableCameraLevelSequenceActor.h"
#include "LevelSequence/ComposableCameraLevelSequenceComponent.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "ComposableCameraInstanceDraftPreview"

FComposableCameraInstanceDraftPreview::~FComposableCameraInstanceDraftPreview() { Release(); }

void FComposableCameraInstanceDraftPreview::Release()
{
	if (IsValid(PreviewActor)) PreviewActor->Destroy();
	PreviewActor = nullptr;
	Values = nullptr;
	Fields.Reset(); Subjects.Reset(); Clock.Reset();
	bValidResult = false;
	MissingInput = NAME_None;
}

UCineCameraComponent* FComposableCameraInstanceDraftPreview::GetCamera(bool bRequireValidResult) const
{
	const auto* Component = IsValid(PreviewActor) ? PreviewActor->FindComponentByClass<UComposableCameraLevelSequenceComponent>() : nullptr;
	return Component && (!bRequireValidResult || bValidResult) ? Component->OutputCineCameraComponent.Get() : nullptr;
}

bool FComposableCameraInstanceDraftPreview::IsSubjectObject(const UObject* Object) const
{
	return Object && Subjects.ContainsByPredicate([Object](const auto& Subject)
		{ return Subject.Actor.IsValid() && (Object == Subject.Actor.Get() || Object->IsIn(Subject.Actor.Get())); });
}

void FComposableCameraInstanceDraftPreview::Update(const UComposableCameraInstanceData& Source, ISequencer& Sequencer, bool bInvalidated)
{
	UWorld* World = Cast<UWorld>(Sequencer.GetPlaybackContext());
	if (!World || !Source.IsTypeInterfaceCurrent() || !Source.IsTypeInterfaceValid())
	{
		Release();
		Status = FText::FromString(Source.GetTypeInterfaceError().IsEmpty() ? TEXT("Open the draft's Level Sequence in a valid world.") : Source.GetTypeInterfaceError());
		return;
	}
	if (PreviewActor && (!IsValid(PreviewActor) || PreviewActor->GetWorld() != World)) Release();
	if (!PreviewActor)
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags = RF_Transient | RF_DuplicateTransient | RF_TextExportTransient;
		Params.bHideFromSceneOutliner = true;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		PreviewActor = World->SpawnActor<AComposableCameraLevelSequenceActor>(Params);
		if (!PreviewActor)
		{
			Status = LOCTEXT("SpawnFailed", "Could not create the temporary camera preview.");
			return;
		}
		PreviewActor->SetIsTemporarilyHiddenInEditor(true);
		auto* Component = PreviewActor->FindComponentByClass<UComposableCameraLevelSequenceComponent>();
		Component->bCameraInstanceDriven = true;
		Component->SetComponentTickEnabled(false);
		Values = NewObject<UComposableCameraInstanceData>(GetTransientPackage(), NAME_None, RF_Transient);
		bInvalidated = true;
	}
	if (bInvalidated)
	{
		// Source/gesture invalidation only; continuous playback retains bags and field addresses.
		Values->CopyConfiguration(Source);
		Values->GatherFields(Fields);
		Subjects.Reset();
		for (const auto& Slot : Values->Subjects)
		{
			const int32 Index = Fields.IndexOfByPredicate([&](const auto& Field) { return Field.Path == Slot.Input; });
			if (Index == INDEX_NONE) continue;
			const auto* Property = CastField<FObjectPropertyBase>(Fields[Index].Property);
			if (!Property || !Property->PropertyClass->IsChildOf(AActor::StaticClass())) continue;
			auto& Sample = Subjects.AddDefaulted_GetRef();
			Sample.FieldIndex = Index; Sample.Binding = Slot.Binding;
			Sample.bRequired = Fields[Index].bRequiredActor;
		}
		CompatibilityWarning = FText::GetEmpty();
		bool bRequiresPCM = false;
		for (const UComposableCameraCameraNodeBase* Node : Source.Camera.TypeAsset->NodeTemplates)
			if (Node && Node->GetLevelSequenceCompatibility() == EComposableCameraNodeLevelSequenceCompatibility::RequiresPCM) bRequiresPCM = true;
		if (bRequiresPCM || !Source.Camera.TypeAsset->ComputeNodeTemplates.IsEmpty())
			CompatibilityWarning = LOCTEXT("Compatibility", "Level Sequence preview: PCM-dependent nodes and BeginPlay compute logic are unavailable.");
	}

	const FFrameTime Time = Sequencer.GetLocalTime().Time;
	bool bActorChanged = false;
	bool bTransformChanged = false;
	bool bRequiredMissing = false;
	FName NewMissingInput;
	for (auto& Subject : Subjects)
	{
		const auto& Field = Fields[Subject.FieldIndex];
		const auto* Property = CastFieldChecked<FObjectPropertyBase>(Field.Property);
		AActor* Resolved = nullptr;
		if (Subject.Binding.IsValid())
			for (const auto Object : Subject.Binding.ResolveBoundObjects(Sequencer.GetFocusedTemplateID(), Sequencer))
				if (Object.IsValid() && Object->IsA(Property->PropertyClass)) { Resolved = Cast<AActor>(Object.Get()); break; }
		bActorChanged |= Subject.Actor.Get() != Resolved;
		const FTransform Transform = Resolved ? Resolved->GetActorTransform() : FTransform::Identity;
		bTransformChanged |= !Subject.Transform.Equals(Transform);
		Subject.Actor = Resolved; Subject.Transform = Transform;
		Field.WriteObject(Resolved);
		if (!Resolved && (Subject.bRequired || Subject.Binding.IsValid()))
		{
			if (NewMissingInput.IsNone()) NewMissingInput = Field.Path;
			bRequiredMissing |= Subject.bRequired;
		}
	}
	if (bInvalidated || MissingInput != NewMissingInput)
	{
		MissingInput = NewMissingInput;
		Status = MissingInput.IsNone() ? CompatibilityWarning : FText::Format(
			LOCTEXT("Missing", "{0}: no actor resolves at this time. Check the binding or its spawn interval."), FText::FromName(MissingInput));
	}
	if (bRequiredMissing)
	{
		bValidResult = false; Clock.Reset();
		return;
	}
	const auto Sample = Clock.Sample(Time, Sequencer.GetFocusedTickResolution(),
		Sequencer.GetPlaybackStatus() != EMovieScenePlayerStatus::Playing,
		bInvalidated || bActorChanged || (bTransformChanged && Time == LastTime));
	LastTime = Time;
	if (Sample.bChanged)
	{
		auto* Component = PreviewActor->FindComponentByClass<UComposableCameraLevelSequenceComponent>();
		Component->EvaluateCameraInstance(*Values, Sample.DeltaSeconds, Sample.bReset);
		bValidResult = Component->HasCameraInstanceResult();
		if (!bValidResult) Status = LOCTEXT("EvaluationFailed", "Camera evaluation produced no valid output. The previous view is retained.");
		else if (MissingInput.IsNone()) Status = CompatibilityWarning;
	}
}

void FComposableCameraInstanceDraftPreview::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(PreviewActor); Collector.AddReferencedObject(Values);
}

#undef LOCTEXT_NAMESPACE
