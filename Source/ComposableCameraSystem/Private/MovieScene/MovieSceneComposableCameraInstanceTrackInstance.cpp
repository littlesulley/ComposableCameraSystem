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
			|| Evaluation.SectionSignature != Section->GetSignature();
		if (bConfigurationChanged)
		{
			// Configuration changes/first entry allocate; steady-state evaluation reuses these
			// bags, target buffers and compiled field addresses independently per Sequence instance.
			Evaluation.Values->CopyConfiguration(*Track->Instance);
			Evaluation.Values->GatherFields(Evaluation.Fields);
			Evaluation.Defaults.Reset(Evaluation.Fields.Num());
			for (const auto& Field : Evaluation.Fields) Evaluation.Defaults.Add(Field.ReadNumber());
			Evaluation.SourceSignature = Track->GetTypedOuter<UMovieScene>()->GetSignature();
			Evaluation.SectionSignature = Section->GetSignature();
			Evaluation.bReset = true;
		}
		for (int32 Index = 0; Index < Evaluation.Fields.Num(); ++Index)
			if (Evaluation.Fields[Index].Type != EComposableCameraInstanceFieldType::Object)
				Evaluation.Fields[Index].WriteNumber(Evaluation.Defaults[Index]);
		Track->EvaluateParameters(Instance.GetContext().GetTime(), Evaluation.Fields);
		for (const FComposableCameraInstanceBinding& Binding : Evaluation.Values->Subjects)
		{
			const auto* Field = Evaluation.Fields.FindByPredicate([&](const auto& Item) { return Item.Path == Binding.Input && Item.Type == EComposableCameraInstanceFieldType::Object; });
			const FObjectPropertyBase* ObjectProperty = Field ? CastField<FObjectPropertyBase>(Field->Property) : nullptr;
			if (!ObjectProperty) continue;
			UObject* Resolved = nullptr;
			if (Binding.Binding.IsValid())
				for (const auto Object : Binding.Binding.ResolveBoundObjects(Instance))
					if (Object.IsValid() && Object->IsA(ObjectProperty->PropertyClass)) { Resolved = Object.Get(); break; }
			Field->WriteObject(Resolved); // Missing/despawned binding clears the previous live actor.
		}
		const auto& Context = Instance.GetContext();
		const bool bReset = Evaluation.bReset || Evaluation.Output.Get() != Output || Context.HasJumped()
			|| Context.GetStatus() != EMovieScenePlayerStatus::Playing || Context.GetDelta().AsDecimal() < 0;
		const float DeltaTime = bReset ? 0.f : static_cast<float>(Context.GetDelta().AsDecimal() / Context.GetFrameRate().AsDecimal());
		Output->EvaluateCameraInstance(*Evaluation.Values, DeltaTime, bReset);
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
	for (auto& Evaluation : Evaluations)
		if (auto* Output = Evaluation.Output.Get()) Output->ReleaseCameraInstance();
	Evaluations.Reset();
}
