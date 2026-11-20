// Copyright 2026 Sulley. All Rights Reserved.
#include "MovieScene/MovieSceneComposableCameraInstanceTrackInstance.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/MovieSceneInstanceRegistry.h"
#include "EntitySystem/MovieSceneSequenceInstance.h"
#include "GameFramework/Actor.h"
#include "LevelSequence/ComposableCameraLevelSequenceComponent.h"
#include "MovieScene/MovieSceneComposableCameraInstanceSection.h"
#include "MovieScene/MovieSceneComposableCameraInstanceTrack.h"
#include "UObject/UnrealType.h"
#include "MovieScene.h"

#if WITH_EDITOR
void UMovieSceneComposableCameraInstanceTrackInstance::OnInitialize()
{
	FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject(this, &UMovieSceneComposableCameraInstanceTrackInstance::OnSubjectPropertyChanged);
}

void UMovieSceneComposableCameraInstanceTrackInstance::OnSubjectPropertyChanged(UObject* Object, FPropertyChangedEvent&)
{
	if (!Object) return;
	for (auto& Evaluation : Evaluations)
	{
		if (!Evaluation.Values || Evaluation.Values->bComposition) continue;
		for (const auto& Field : Evaluation.Fields)
		{
			const auto* Property = CastField<FObjectPropertyBase>(Field.Property);
			const auto* Actor = Property ? Cast<AActor>(Property->GetObjectPropertyValue(Field.Value)) : nullptr;
			if (IsValid(Actor) && (Object == Actor || Object->IsIn(Actor)))
			{
				// Component/socket edits can change node inputs without moving the Actor
				// or the playhead. Invalidate once; the next sample must not advance time.
				Evaluation.bReset = true;
				break;
			}
		}
	}
}
#endif

void UMovieSceneComposableCameraInstanceTrackInstance::OnInputAdded(const FMovieSceneTrackInstanceInput& Input)
{
	auto& Evaluation = Evaluations.AddDefaulted_GetRef();
	Evaluation.Input = Input;
	Evaluation.Values = NewObject<UComposableCameraInstanceData>(this, NAME_None, RF_Transient);
}

void UMovieSceneComposableCameraInstanceTrackInstance::OnAnimate()
{
	const auto* Registry = GetLinker() ? GetLinker()->GetInstanceRegistry() : nullptr;
	if (!Registry) return;
	for (auto& Evaluation : Evaluations)
	{
		auto* Section = Cast<UMovieSceneComposableCameraInstanceSection>(Evaluation.Input.Section);
		auto* Track = Section ? Section->GetTypedOuter<UMovieSceneComposableCameraInstanceTrack>() : nullptr;
		if (!Track || !Track->Instance || !Registry->IsHandleValid(Evaluation.Input.InstanceHandle)) continue;
		// One output has one writer. If future editing creates overlapping intervals of the
		// same instance, the latest-starting interval wins deterministically.
		bool bSuppressed = false;
		for (const auto& Other : Evaluations)
		{
			if (&Other == &Evaluation || Other.Input.InstanceHandle != Evaluation.Input.InstanceHandle) continue;
			const UMovieSceneSection* Peer = Other.Input.Section;
			if (!Peer || Peer->GetOuter() != Track) continue;
			const FFrameNumber Start = Section->HasStartFrame() ? Section->GetInclusiveStartFrame() : FFrameNumber(MIN_int32);
			const FFrameNumber PeerStart = Peer->HasStartFrame() ? Peer->GetInclusiveStartFrame() : FFrameNumber(MIN_int32);
			if (PeerStart > Start || (PeerStart == Start && Track->GetAllSections().IndexOfByKey(Peer) > Track->GetAllSections().IndexOfByKey(Section))) bSuppressed = true;
		}
		if (bSuppressed) { Evaluation.bReset = true; continue; }
		const auto& Instance = Registry->GetInstance(Evaluation.Input.InstanceHandle);
		UComposableCameraLevelSequenceComponent* Output = nullptr;
		for (const TWeakObjectPtr<UObject> Object : Track->OutputBinding.ResolveBoundObjects(Instance))
		{
			if (AActor* Actor = Cast<AActor>(Object.Get())) Output = Actor->FindComponentByClass<UComposableCameraLevelSequenceComponent>();
			if (Output && Output->CameraInstanceOwner != Track) Output = nullptr;
			if (Output) break;
		}
		if (Evaluation.Output.IsValid() && Evaluation.Output.Get() != Output) Evaluation.Output->ReleaseCameraInstance();
		if (!Output) { Evaluation.Output.Reset(); Evaluation.bReset = true; continue; }
		const bool bConfigurationChanged = Evaluation.SourceSignature != Track->GetTypedOuter<UMovieScene>()->GetSignature()
			|| Evaluation.SectionSignature != Section->GetSignature() || !Evaluation.Values->IsTypeInterfaceCurrent();
		if (bConfigurationChanged)
		{
			// Configuration changes/first entry allocate; steady-state evaluation reuses these
			// bags, target buffers and compiled field addresses independently per Sequence instance.
			Evaluation.Values->CopyConfiguration(*Track->Instance);
			Evaluation.Values->RefreshTypeInterface();
			Evaluation.Values->GatherFields(Evaluation.Fields);
			Evaluation.Defaults.Reset(Evaluation.Fields.Num());
			for (const auto& Field : Evaluation.Fields) Evaluation.Defaults.Add(Field.ReadNumber());
			Evaluation.ActorTransforms.Init(FTransform::Identity, Evaluation.Fields.Num());
			Evaluation.SourceSignature = Track->GetTypedOuter<UMovieScene>()->GetSignature();
			Evaluation.SectionSignature = Section->GetSignature();
			Evaluation.bReset = true;
		}
		if (!Evaluation.Values->IsTypeInterfaceValid())
		{
			Output->ReleaseCameraInstance();
			Evaluation.Output.Reset();
			Evaluation.Clock.Reset();
			continue;
		}
		for (int32 Index = 0; Index < Evaluation.Fields.Num(); ++Index)
			if (Evaluation.Fields[Index].Type != EComposableCameraInstanceFieldType::Object)
				Evaluation.Fields[Index].WriteNumber(Evaluation.Defaults[Index]);
		const auto& Context = Instance.GetContext();
		Track->EvaluateParameters(Context.GetTime(), Evaluation.Fields);
		bool bRequiredMissing = false;
		for (const FComposableCameraInstanceBinding& Binding : Evaluation.Values->Subjects)
		{
			const auto* Field = Evaluation.Fields.FindByPredicate([&](const auto& Item) { return Item.Path == Binding.Input && Item.Type == EComposableCameraInstanceFieldType::Object; });
			const FObjectPropertyBase* ObjectProperty = Field ? CastField<FObjectPropertyBase>(Field->Property) : nullptr;
			if (!ObjectProperty) continue;
			UObject* Resolved = nullptr;
			if (Binding.Binding.IsValid())
				for (const auto Object : Binding.Binding.ResolveBoundObjects(Instance))
					if (Object.IsValid() && Object->IsA(ObjectProperty->PropertyClass)) { Resolved = Object.Get(); break; }
			if (ObjectProperty->GetObjectPropertyValue(Field->Value) != Resolved) Evaluation.bReset = true;
			if (!Evaluation.Values->bComposition)
			{
				const auto* Actor = Cast<AActor>(Resolved);
				const FTransform Transform = Actor ? Actor->GetActorTransform() : FTransform::Identity;
				const int32 FieldIndex = static_cast<int32>(Field - Evaluation.Fields.GetData());
				FTransform& Previous = Evaluation.ActorTransforms[FieldIndex];
				// A moved level actor invalidates the same-time sample; it does not
				// request another simulation step at the same timestamp.
				if (Context.GetTime() == Evaluation.LastTime && !Previous.Equals(Transform)) Evaluation.bReset = true;
				Previous = Transform;
			}
			Field->WriteObject(Resolved); // Missing/despawned binding clears the previous live actor.
			bRequiredMissing |= Field->bRequiredActor && !Resolved;
		}
		if (bRequiredMissing)
		{
			Output->InvalidateCameraInstanceResult();
			Evaluation.Clock.Reset(); Evaluation.bReset = true; Evaluation.Output = Output;
			continue;
		}
		if (Evaluation.Values->bComposition)
		{
			// Preserve the established composition solver's resampling/reset contract.
			const bool bReset = Evaluation.bReset || Evaluation.Output.Get() != Output || Context.HasJumped()
				|| Context.GetStatus() != EMovieScenePlayerStatus::Playing || Context.GetDelta().AsDecimal() < 0;
			const float DeltaTime = bReset ? 0.f : static_cast<float>(Context.GetDelta().AsDecimal() / Context.GetFrameRate().AsDecimal());
			Output->EvaluateCameraInstance(*Evaluation.Values, DeltaTime, bReset);
		}
		else
		{
			const auto Sample = Evaluation.Clock.Sample(Context.GetTime(), Context.GetFrameRate(),
				Context.HasJumped() || FComposableCameraInstanceTime::IsDiscontinuousStatus(Context.GetStatus()),
				Evaluation.bReset || Evaluation.Output.Get() != Output);
			if (Sample.bChanged) Output->EvaluateCameraInstance(*Evaluation.Values, Sample.DeltaSeconds, Sample.bReset);
		}
		Evaluation.LastTime = Context.GetTime();
		Evaluation.Output = Output;
		Evaluation.bReset = false;
	}
}

void UMovieSceneComposableCameraInstanceTrackInstance::OnInputRemoved(const FMovieSceneTrackInstanceInput& Input)
{
	for (auto& Evaluation : Evaluations)
		if (Evaluation.Input.IsSameInput(Input))
			if (auto* Output = Evaluation.Output.Get())
			{
				const bool bStillUsed = Evaluations.ContainsByPredicate([&](const auto& Other)
					{ return !Other.Input.IsSameInput(Input) && Other.Output.Get() == Output; });
				if (!bStillUsed) Output->ReleaseCameraInstance();
			}
	Evaluations.RemoveAll([&](const auto& Evaluation) { return Evaluation.Input.IsSameInput(Input); });
}
void UMovieSceneComposableCameraInstanceTrackInstance::OnDestroyed()
{
#if WITH_EDITOR
	FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
#endif
	for (auto& Evaluation : Evaluations)
		if (auto* Output = Evaluation.Output.Get()) Output->ReleaseCameraInstance();
	Evaluations.Reset();
}
