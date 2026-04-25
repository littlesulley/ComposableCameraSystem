// Copyright Sulley. All rights reserved.

#include "MovieScene/MovieSceneComposableCameraPatchTrackInstance.h"

#include "ComposableCameraSystemModule.h"
#include "Core/ComposableCameraParameterBlock.h"
#include "DataAssets/ComposableCameraPatchTypeAsset.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/MovieSceneInstanceRegistry.h"
#include "EntitySystem/MovieSceneSequenceInstance.h"
#include "GameFramework/Actor.h"
#include "LevelSequence/ComposableCameraLevelSequenceComponent.h"
#include "MovieScene.h"
#include "MovieScene/MovieSceneComposableCameraPatchSection.h"
#include "MovieSceneObjectBindingID.h"
#include "Patches/ComposableCameraPatchEnvelope.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MovieSceneComposableCameraPatchTrackInstance)

namespace
{
	/** Resolve `Section->TargetActorBinding` against the input's sequence
	 *  instance to its bound LS Actor's UComposableCameraLevelSequenceComponent.
	 *  Returns null on any miss (binding unset, actor not yet spawned, bound
	 *  object isn't an actor, actor has no LS Component). The TrackInstance
	 *  callbacks tolerate null — silent skip with no log spam (this fires per
	 *  Sequencer scrub frame and any noise here would flood the log). */
	UComposableCameraLevelSequenceComponent* ResolveLSComponent(
		const UMovieSceneEntitySystemLinker* Linker,
		const FMovieSceneTrackInstanceInput& Input,
		const UMovieSceneComposableCameraPatchSection& Section)
	{
		if (!Linker || !Section.TargetActorBinding.IsValid())
		{
			return nullptr;
		}
		const UE::MovieScene::FInstanceRegistry* Registry = Linker->GetInstanceRegistry();
		if (!Registry || !Registry->IsHandleValid(Input.InstanceHandle))
		{
			return nullptr;
		}
		const UE::MovieScene::FSequenceInstance& Instance = Registry->GetInstance(Input.InstanceHandle);
		const TArrayView<TWeakObjectPtr<>> Objects =
			Section.TargetActorBinding.ResolveBoundObjects(Instance);
		if (Objects.Num() == 0)
		{
			return nullptr;
		}
		AActor* Actor = Cast<AActor>(Objects[0].Get());
		return Actor ? Actor->FindComponentByClass<UComposableCameraLevelSequenceComponent>() : nullptr;
	}

	/** Resolve the input's current evaluation frame via the linker's instance
	 *  registry → FSequenceInstance::GetContext::GetTime. Same pattern the gate
	 *  instantiator uses. Falls back to the section's lower bound if the
	 *  registry lookup fails (defensive — shouldn't normally happen). */
	FFrameNumber ResolveCurrentFrame(
		const UMovieSceneEntitySystemLinker* Linker,
		const FMovieSceneTrackInstanceInput& Input)
	{
		if (Linker)
		{
			if (const UE::MovieScene::FInstanceRegistry* Registry = Linker->GetInstanceRegistry())
			{
				if (Registry->IsHandleValid(Input.InstanceHandle))
				{
					const UE::MovieScene::FSequenceInstance& Instance = Registry->GetInstance(Input.InstanceHandle);
					return Instance.GetContext().GetTime().FloorToFrame();
				}
			}
		}
		if (const UMovieSceneSection* Section = Input.Section)
		{
			const TRange<FFrameNumber> Range = Section->GetTrueRange();
			if (Range.HasLowerBound())
			{
				return Range.GetLowerBoundValue();
			}
		}
		return FFrameNumber(0);
	}

	/** Resolve the section's effective enter / exit duration in seconds for
	 *  the stateless envelope computation. Per-Params override wins; otherwise
	 *  fall back to section easing (converted via owning movie scene's tick
	 *  rate); otherwise asset's `DefaultEnterDuration` / `DefaultExitDuration`. */
	void ResolveEnvelopeDurations(
		const UMovieSceneComposableCameraPatchSection& Section,
		float& OutEnterSeconds,
		float& OutExitSeconds)
	{
		const UMovieScene* OwnerScene = Section.GetTypedOuter<UMovieScene>();
		const FFrameRate TickRate = OwnerScene ? OwnerScene->GetTickResolution() : FFrameRate(60000, 1);

		auto TicksToSeconds = [TickRate](int32 Ticks) -> float
		{
			return Ticks > 0 ? TickRate.AsSeconds(FFrameTime(Ticks)) : 0.f;
		};

		if (Section.Params.bOverrideEnterDuration)
		{
			OutEnterSeconds = Section.Params.EnterDuration;
		}
		else if (const float Eased = TicksToSeconds(Section.Easing.GetEaseInDuration()); Eased > 0.f)
		{
			OutEnterSeconds = Eased;
		}
		else
		{
			OutEnterSeconds = Section.PatchAsset ? Section.PatchAsset->DefaultEnterDuration : 0.f;
		}

		if (Section.Params.bOverrideExitDuration)
		{
			OutExitSeconds = Section.Params.ExitDuration;
		}
		else if (const float Eased = TicksToSeconds(Section.Easing.GetEaseOutDuration()); Eased > 0.f)
		{
			OutExitSeconds = Eased;
		}
		else
		{
			OutExitSeconds = Section.PatchAsset ? Section.PatchAsset->DefaultExitDuration : 0.f;
		}
	}
}

void UMovieSceneComposableCameraPatchTrackInstance::OnAnimate()
{
	UMovieSceneEntitySystemLinker* Linker = GetLinker();

	// Diagnostic: confirm OnAnimate is firing at all and how many inputs
	// the engine has handed us. Verbose so it doesn't spam in normal use,
	// but lets `Log LogComposableCameraSystem Verbose` reveal a silent
	// "TrackInstance never fires" bug. Same scope-level log pattern the
	// Gate Instantiator uses.
	UE_LOG(LogComposableCameraSystem, Verbose,
		TEXT("PatchTrackInstance::OnAnimate fired with %d input(s)."),
		GetInputs().Num());

	for (const FMovieSceneTrackInstanceInput& Input : GetInputs())
	{
		UMovieSceneComposableCameraPatchSection* Section = Cast<UMovieSceneComposableCameraPatchSection>(Input.Section);
		if (!Section || !Section->PatchAsset)
		{
			UE_LOG(LogComposableCameraSystem, Verbose,
				TEXT("  input '%s': skipped (no PatchAsset)."),
				Section ? *Section->GetName() : TEXT("<null>"));
			continue;
		}
		UComposableCameraLevelSequenceComponent* LSComp = ResolveLSComponent(Linker, Input, *Section);
		if (!LSComp)
		{
			UE_LOG(LogComposableCameraSystem, Verbose,
				TEXT("  input '%s': skipped (TargetActorBinding %s did not resolve to an LS Component this frame)."),
				*Section->GetName(),
				Section->TargetActorBinding.IsValid() ? TEXT("set but unresolved") : TEXT("not set"));
			continue;
		}

		const FFrameNumber CurrentFrame = ResolveCurrentFrame(Linker, Input);

		// Envelope alpha — stateless, recomputed each frame from playhead vs
		// section bounds. Same ease curve as runtime PatchManager via shared
		// PatchEnvelope::ApplyEase.
		float EnterSeconds = 0.f;
		float ExitSeconds = 0.f;
		ResolveEnvelopeDurations(*Section, EnterSeconds, ExitSeconds);

		const UMovieScene* OwnerScene = Section->GetTypedOuter<UMovieScene>();
		const FFrameRate TickRate = OwnerScene ? OwnerScene->GetTickResolution() : FFrameRate(60000, 1);

		const TRange<FFrameNumber> Range = Section->GetTrueRange();
		const FFrameNumber SectionStart = Range.HasLowerBound() ? Range.GetLowerBoundValue() : FFrameNumber(0);
		const FFrameNumber SectionEnd   = Range.HasUpperBound() ? Range.GetUpperBoundValue() : FFrameNumber(MAX_int32);

		const float Alpha = UE::ComposableCameras::PatchEnvelope::ComputeStatelessAlpha(
			CurrentFrame, SectionStart, SectionEnd,
			EnterSeconds, ExitSeconds,
			Section->PatchAsset->DefaultEaseType, TickRate);

		// Sample channel-driven parameters (channels override bag defaults).
		FComposableCameraParameterBlock Block;
		Section->BuildParameterBlock(CurrentFrame, Block);

		LSComp->SetSequencerPatchOverlay(Section, Block, Alpha);

		UE_LOG(LogComposableCameraSystem, Verbose,
			TEXT("  input '%s' → LSComp '%s', frame=%d, range=[%d..%d), enter=%.3fs (%d ticks), exit=%.3fs (%d ticks), alpha=%.3f"),
			*Section->GetName(),
			*LSComp->GetName(),
			CurrentFrame.Value,
			SectionStart.Value, SectionEnd.Value,
			EnterSeconds, (EnterSeconds * TickRate).FloorToFrame().Value,
			ExitSeconds, (ExitSeconds * TickRate).FloorToFrame().Value,
			Alpha);
	}
}

void UMovieSceneComposableCameraPatchTrackInstance::OnInputRemoved(const FMovieSceneTrackInstanceInput& InInput)
{
	UMovieSceneComposableCameraPatchSection* Section = Cast<UMovieSceneComposableCameraPatchSection>(InInput.Section);
	if (!Section)
	{
		return;
	}
	if (UComposableCameraLevelSequenceComponent* LSComp = ResolveLSComponent(GetLinker(), InInput, *Section))
	{
		LSComp->RemoveSequencerPatchOverlay(Section);
	}
}

void UMovieSceneComposableCameraPatchTrackInstance::OnDestroyed()
{
	// Defensive teardown: walk every input and tell its bound LS Component to
	// drop the overlay. Mirrors the per-input path of OnInputRemoved. Handles
	// Sequencer hot-reload / linker shutdown where OnInputRemoved isn't
	// guaranteed to fire for every still-active input.
	UMovieSceneEntitySystemLinker* Linker = GetLinker();
	for (const FMovieSceneTrackInstanceInput& Input : GetInputs())
	{
		UMovieSceneComposableCameraPatchSection* Section = Cast<UMovieSceneComposableCameraPatchSection>(Input.Section);
		if (!Section)
		{
			continue;
		}
		if (UComposableCameraLevelSequenceComponent* LSComp = ResolveLSComponent(Linker, Input, *Section))
		{
			LSComp->RemoveSequencerPatchOverlay(Section);
		}
	}
}
