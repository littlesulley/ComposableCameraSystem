// Copyright Sulley. All rights reserved.

#include "MovieScene/MovieSceneComposableCameraShotTrackInstance.h"

#include "ComposableCameraSystemModule.h"
#include "DataAssets/ComposableCameraTransitionDataAsset.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/MovieSceneInstanceRegistry.h"
#include "EntitySystem/MovieSceneSequenceInstance.h"
#include "GameFramework/Actor.h"
#include "LevelSequence/ComposableCameraLevelSequenceComponent.h"
#include "MovieScene.h"
#include "MovieSceneObjectBindingID.h"
#include "MovieSceneTrack.h"
#include "MovieScene/MovieSceneComposableCameraShotSection.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MovieSceneComposableCameraShotTrackInstance)

namespace
{
	/** Resolve the parent-binding's bound LS Component for `Section`.
	 *  Walks Section → parent UMovieSceneTrack → FindObjectBindingGuid →
	 *  FSequenceInstance::ResolveBoundObjects → LS Component on the first
	 *  bound actor. Returns null on any miss (track is root / not under a
	 *  binding, binding not yet spawned, bound object isn't an actor, actor
	 *  has no LS Component). */
	UComposableCameraLevelSequenceComponent* ResolveLSComponent(
		const UMovieSceneEntitySystemLinker* Linker,
		const FMovieSceneTrackInstanceInput& Input,
		const UMovieSceneComposableCameraShotSection& Section)
	{
		if (!Linker)
		{
			return nullptr;
		}
		const UMovieSceneTrack* Track = Section.GetTypedOuter<UMovieSceneTrack>();
		if (!Track)
		{
			return nullptr;
		}
		const FGuid BindingGuid = Track->FindObjectBindingGuid();
		if (!BindingGuid.IsValid())
		{
			return nullptr;
		}

		const UE::MovieScene::FInstanceRegistry* Registry = Linker->GetInstanceRegistry();
		if (!Registry || !Registry->IsHandleValid(Input.InstanceHandle))
		{
			return nullptr;
		}
		const UE::MovieScene::FSequenceInstance& Instance = Registry->GetInstance(Input.InstanceHandle);

		// Resolve the binding GUID against the input's sequence instance —
		// FMovieSceneObjectBindingID's relative-binding ctor defaults SequenceID
		// to the input's owning sequence so cross-sequence sub-sections also
		// resolve correctly.
		const FMovieSceneObjectBindingID Binding = UE::MovieScene::FRelativeObjectBindingID(BindingGuid);
		const TArrayView<TWeakObjectPtr<>> Objects = Binding.ResolveBoundObjects(Instance);
		if (Objects.Num() == 0)
		{
			return nullptr;
		}
		AActor* Actor = Cast<AActor>(Objects[0].Get());
		return Actor ? Actor->FindComponentByClass<UComposableCameraLevelSequenceComponent>() : nullptr;
	}
}

namespace
{
	/** Per-input resolved state — built in pass 1 of OnAnimate, consumed in
	 *  pass 2 for cross-section overlap analysis (Phase F BlendAlpha). */
	struct FResolvedShotInput
	{
		UMovieSceneComposableCameraShotSection*    Section            { nullptr };
		UComposableCameraLevelSequenceComponent*   LSComp             { nullptr };
		FComposableCameraShot                      EffectiveShot;
		int32                                      RowIndex           { 0 };
		TRange<FFrameNumber>                       Range;
		FFrameNumber                               CurrentFrame;
		UComposableCameraTransitionDataAsset*      EnterTransition    { nullptr };
	};

	/** Compute the Phase F BlendAlpha for `Entry` against its immediately-
	 *  below-row in-range overlapping peer (same LSComp). Returns 1.0 when
	 *  there is no such peer (entry is standalone / lower-row of a pair).
	 *
	 *  Same-LSComp scoping matters when one Sequencer drives multiple LS
	 *  Actors via separate parent bindings — each LSActor's Shot Track has
	 *  its own row layout, and we must NOT cross-blend rows that target
	 *  different cameras. */
	float ComputeBlendAlphaAgainstLowerPeer(
		const FResolvedShotInput& Entry,
		const TArray<FResolvedShotInput>& AllResolved)
	{
		// Find the immediately-below-row peer (largest RowIndex strictly
		// below Entry.RowIndex) that shares any frame-range overlap with
		// `Entry`. Same LSComp scope.
		const FResolvedShotInput* Peer = nullptr;
		for (const FResolvedShotInput& Other : AllResolved)
		{
			if (Other.LSComp != Entry.LSComp)            { continue; }
			if (Other.Section == Entry.Section)          { continue; }
			if (Other.RowIndex >= Entry.RowIndex)        { continue; }
			if (Other.Range.IsEmpty())                   { continue; }
			if (TRange<FFrameNumber>::Intersection(Other.Range, Entry.Range).IsEmpty())
			{
				continue;
			}
			if (!Peer || Other.RowIndex > Peer->RowIndex)
			{
				Peer = &Other;
			}
		}

		if (!Peer
			|| !Entry.Range.HasLowerBound() || !Entry.Range.HasUpperBound()
			|| !Peer->Range.HasLowerBound() || !Peer->Range.HasUpperBound())
		{
			return 1.0f;
		}

		const FFrameNumber OverlapStart = FMath::Max(
			Entry.Range.GetLowerBoundValue(), Peer->Range.GetLowerBoundValue());
		const FFrameNumber OverlapEnd = FMath::Min(
			Entry.Range.GetUpperBoundValue(), Peer->Range.GetUpperBoundValue());
		const int32 OverlapTicks = (OverlapEnd - OverlapStart).Value;
		if (OverlapTicks <= 0)
		{
			// Touching ranges (zero-width intersection) → treat as hard cut.
			// BlendAlpha = 1 means "incoming is fully active"; the lower-row
			// entry's range no longer contains CurrentFrame so the blender
			// will see only this entry anyway.
			return 1.0f;
		}

		const int32 Progress = (Entry.CurrentFrame - OverlapStart).Value;
		return FMath::Clamp(
			static_cast<float>(Progress) / static_cast<float>(OverlapTicks),
			0.0f, 1.0f);
	}
}

void UMovieSceneComposableCameraShotTrackInstance::OnAnimate()
{
	UMovieSceneEntitySystemLinker* Linker = GetLinker();

	UE_LOG(LogComposableCameraSystem, Verbose,
		TEXT("ShotTrackInstance::OnAnimate fired with %d input(s)."),
		GetInputs().Num());

	// ─── Pass 1: resolve every input to (Section, LSComp, EffectiveShot,
	// RowIndex, Range, CurrentFrame, EnterTransition). Two-pass instead of
	// inline-push because Phase F's BlendAlpha needs cross-section visibility
	// (each section's alpha is computed against its lower-row peer).
	const UE::MovieScene::FInstanceRegistry* Registry =
		Linker ? Linker->GetInstanceRegistry() : nullptr;

	TArray<FResolvedShotInput> Resolved;
	Resolved.Reserve(GetInputs().Num());

	for (const FMovieSceneTrackInstanceInput& Input : GetInputs())
	{
		UMovieSceneComposableCameraShotSection* Section =
			Cast<UMovieSceneComposableCameraShotSection>(Input.Section);
		if (!Section)
		{
			continue;
		}

		UComposableCameraLevelSequenceComponent* LSComp =
			ResolveLSComponent(Linker, Input, *Section);
		if (!LSComp)
		{
			UE_LOG(LogComposableCameraSystem, Verbose,
				TEXT("  shot input '%s': skipped (parent binding did not resolve to an LS Component this frame)."),
				*Section->GetName());
			continue;
		}

		if (!Registry || !Registry->IsHandleValid(Input.InstanceHandle))
		{
			continue;
		}
		const UE::MovieScene::FSequenceInstance& Instance =
			Registry->GetInstance(Input.InstanceHandle);

		// Build the effective Shot: source Shot (Inline / AssetReference)
		// overlaid with per-target binding overrides. The override path
		// resolves Sequencer bindings to live actors via the running
		// sequence instance — Spawnables, Possessables, sub-bindings.
		FComposableCameraShot EffectiveShot;
		if (!Section->BuildEffectiveShot(Instance, EffectiveShot))
		{
			// AssetReference Section with null / unresolved asset — silent
			// skip. CompositionFramingNode keeps last-written Shot, so the
			// camera holds its previous framing (gap-fill semantics).
			UE_LOG(LogComposableCameraSystem, Verbose,
				TEXT("  shot input '%s': skipped (AssetReference unresolved)."),
				*Section->GetName());
			continue;
		}

		FResolvedShotInput& E = Resolved.AddDefaulted_GetRef();
		E.Section         = Section;
		E.LSComp          = LSComp;
		E.EffectiveShot   = MoveTemp(EffectiveShot);
		E.RowIndex        = Section->GetRowIndex();
		E.Range           = Section->GetTrueRange();
		E.CurrentFrame    = Instance.GetContext().GetTime().FloorToFrame();
		// Read the cached resolved transition — populated off the eval path
		// at the section's PostLoad / PostEditChangeProperty. The eval path
		// must NOT call LoadSynchronous; an unloaded asset degrades to
		// null and the Phase F blender treats null as a hard cut (decision
		// recorded in Section.h's EnterTransition doc-comment).
		E.EnterTransition = Section->ResolveCachedEnterTransition();
	}

	// ─── Pass 2: compute BlendAlpha per entry, push to LSComp.
	for (const FResolvedShotInput& Entry : Resolved)
	{
		const float BlendAlpha = ComputeBlendAlphaAgainstLowerPeer(Entry, Resolved);

		FComposableCameraSequencerShotEntry NewEntry;
		NewEntry.Shot            = Entry.EffectiveShot;
		NewEntry.RowIndex        = Entry.RowIndex;
		NewEntry.EnterTransition = Entry.EnterTransition;
		NewEntry.BlendAlpha      = BlendAlpha;

		Entry.LSComp->SetSequencerShotOverride(Entry.Section, NewEntry);

		UE_LOG(LogComposableCameraSystem, Verbose,
			TEXT("  shot input '%s' → LSComp '%s', row=%d, alpha=%.3f, transition=%s"),
			*Entry.Section->GetName(),
			*Entry.LSComp->GetName(),
			Entry.RowIndex,
			BlendAlpha,
			Entry.EnterTransition ? *Entry.EnterTransition->GetName() : TEXT("<none>"));
	}
}

void UMovieSceneComposableCameraShotTrackInstance::OnInputRemoved(const FMovieSceneTrackInstanceInput& InInput)
{
	UMovieSceneComposableCameraShotSection* Section = Cast<UMovieSceneComposableCameraShotSection>(InInput.Section);
	if (!Section)
	{
		return;
	}
	if (UComposableCameraLevelSequenceComponent* LSComp = ResolveLSComponent(GetLinker(), InInput, *Section))
	{
		LSComp->RemoveSequencerShotOverride(Section);
	}
}

void UMovieSceneComposableCameraShotTrackInstance::OnDestroyed()
{
	UMovieSceneEntitySystemLinker* Linker = GetLinker();
	for (const FMovieSceneTrackInstanceInput& Input : GetInputs())
	{
		UMovieSceneComposableCameraShotSection* Section = Cast<UMovieSceneComposableCameraShotSection>(Input.Section);
		if (!Section)
		{
			continue;
		}
		if (UComposableCameraLevelSequenceComponent* LSComp = ResolveLSComponent(Linker, Input, *Section))
		{
			LSComp->RemoveSequencerShotOverride(Section);
		}
	}
}
