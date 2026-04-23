// Copyright Sulley. All rights reserved.

#include "MovieScene/MovieSceneComposableCameraGateInstantiator.h"

#include "ComposableCameraSystemModule.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/MovieSceneEntityManager.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/MovieSceneEntitySystemTask.h"
#include "EntitySystem/MovieSceneInstanceRegistry.h"
#include "EntitySystem/MovieSceneSequenceInstance.h"
#include "EntitySystem/TrackInstance/MovieSceneTrackInstanceSystem.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "LevelSequence/ComposableCameraLevelSequenceComponent.h"
#include "MovieSceneObjectBindingID.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "TrackInstances/MovieSceneCameraCutTrackInstance.h"
#include "Tracks/MovieSceneCameraCutTrack.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MovieSceneComposableCameraGateInstantiator)

namespace UE::ComposableCameras::GateDetail
{
	/** Walk an Actor → UComposableCameraLevelSequenceComponent. Null-safe. */
	UComposableCameraLevelSequenceComponent* FindLSComponent(UObject* BoundObject)
	{
		if (AActor* Actor = Cast<AActor>(BoundObject))
		{
			return Actor->FindComponentByClass<UComposableCameraLevelSequenceComponent>();
		}
		return nullptr;
	}

	/** Resolve a Spawnable binding GUID → the first bound actor in the given
	 *  sequence instance. Returns nullptr if the Spawnables system hasn't
	 *  materialised the actor yet. */
	UObject* ResolveFirstBoundObject(const UE::MovieScene::FSequenceInstance& Instance, const FGuid& BindingGuid)
	{
		if (!BindingGuid.IsValid())
		{
			return nullptr;
		}
		// Brace init, not paren-init: "T X(Y(z));" is parsed as a function
		// declaration of X returning T (most vexing parse) — bracing forces a
		// value initialiser.
		const FMovieSceneObjectBindingID BindingID{UE::MovieScene::FRelativeObjectBindingID(BindingGuid)};
		const TArrayView<TWeakObjectPtr<>> Objects = BindingID.ResolveBoundObjects(Instance);
		return Objects.Num() > 0 ? Objects[0].Get() : nullptr;
	}
}

UMovieSceneComposableCameraGateInstantiator::UMovieSceneComposableCameraGateInstantiator(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();

	// Attach to any imported Spawn section's ECS entity. Every
	// UMovieSceneSpawnSection::ImportEntityImpl adds BuiltInComponents->
	// SpawnableBinding (see Engine source), regardless of whether the
	// Spawnable is a legacy FMovieSceneSpawnable or a UE 5.5+ custom
	// UMovieSceneSpawnableBindingBase binding. By keying off this built-in
	// component we cover all spawnable types without any editor-time hook
	// or saved-asset migration. Non-Composable-Camera Spawnables flow
	// through too but are filtered in the lazy-resolve pass and dropped
	// from the tracked map once their bound actor is resolved.
	RelevantComponent = BuiltInComponents->SpawnableBinding;

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		DefineComponentConsumer(GetClass(), BuiltInComponents->SpawnableBinding);
		DefineComponentConsumer(GetClass(), BuiltInComponents->RootInstanceHandle);
	}
}

void UMovieSceneComposableCameraGateInstantiator::OnLink()
{
	TrackedComponents.Reset();
	UE_LOG(LogComposableCameraSystem, Log,
		TEXT("Gate instantiator linked — will monitor Spawnable entities for cut/blend gating."));
}

void UMovieSceneComposableCameraGateInstantiator::OnUnlink()
{
	// Dropped from the linker — turn off every gate we still own so the
	// components don't stay stuck in the "active" state when the ECS pipeline
	// tears down (e.g. Sequencer closed in the editor).
	for (TPair<FGateKey, FGateEntry>& Pair : TrackedComponents)
	{
		if (UComposableCameraLevelSequenceComponent* Comp = Pair.Value.Component.Get())
		{
			if (IsValid(Comp))
			{
				Comp->SetEvaluationEnabled(false);
			}
		}
	}
	TrackedComponents.Reset();
}

namespace UE::ComposableCameras::GateDetail
{
	/** Per-entity task body for NeedsLink: register a new gate entry keyed on
	 *  (root instance handle, spawnable binding GUID). Component pointer is
	 *  resolved in OnRun once the Spawnables system has had a chance to
	 *  materialise the actor. */
	struct FLinkTask
	{
		TMap<UMovieSceneComposableCameraGateInstantiator::FGateKey,
			UMovieSceneComposableCameraGateInstantiator::FGateEntry>* TrackedComponents;

		void ForEachEntity(UE::MovieScene::FRootInstanceHandle RootInstanceHandle, const FGuid& SpawnableBinding) const
		{
			if (!SpawnableBinding.IsValid())
			{
				return;
			}
			const UMovieSceneComposableCameraGateInstantiator::FGateKey Key(RootInstanceHandle, SpawnableBinding);
			UMovieSceneComposableCameraGateInstantiator::FGateEntry& Entry = TrackedComponents->FindOrAdd(Key);
			Entry.RootInstanceHandle = RootInstanceHandle;
			Entry.BindingGuid = SpawnableBinding;
			// Entry.Component is resolved lazily in OnRun; a re-link on the same key
			// keeps any already-resolved weak pointer.
		}
	};

	/** Per-entity task body for NeedsUnlink: flip gate off and erase. */
	struct FUnlinkTask
	{
		TMap<UMovieSceneComposableCameraGateInstantiator::FGateKey,
			UMovieSceneComposableCameraGateInstantiator::FGateEntry>* TrackedComponents;

		void ForEachEntity(UE::MovieScene::FRootInstanceHandle RootInstanceHandle, const FGuid& SpawnableBinding) const
		{
			if (!SpawnableBinding.IsValid())
			{
				return;
			}
			const UMovieSceneComposableCameraGateInstantiator::FGateKey Key(RootInstanceHandle, SpawnableBinding);
			if (UMovieSceneComposableCameraGateInstantiator::FGateEntry* Entry = TrackedComponents->Find(Key))
			{
				if (UComposableCameraLevelSequenceComponent* Comp = Entry->Component.Get())
				{
					if (IsValid(Comp))
					{
						Comp->SetEvaluationEnabled(false);
					}
				}
				TrackedComponents->Remove(Key);
			}
		}
	};
}

void UMovieSceneComposableCameraGateInstantiator::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;
	using namespace UE::ComposableCameras;

	FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();

	// ── Phase 1: reconcile Tracked registry with NeedsLink / NeedsUnlink ──
	//
	// Iterate every Spawn-section entity (identified by the built-in
	// SpawnableBinding component). Non-Composable-Camera spawnables flow
	// through here too; Phase 2 filters them out by actor class and discards
	// their entries, so the steady-state set only contains our actors.

	FEntityTaskBuilder()
		.Read(BuiltInComponents->RootInstanceHandle)
		.Read(BuiltInComponents->SpawnableBinding)
		.FilterAny({ BuiltInComponents->Tags.NeedsLink })
		.FilterNone({ BuiltInComponents->Tags.NeedsUnlink, BuiltInComponents->Tags.Ignored })
		.RunInline_PerEntity(&Linker->EntityManager, GateDetail::FLinkTask{ &TrackedComponents });

	FEntityTaskBuilder()
		.Read(BuiltInComponents->RootInstanceHandle)
		.Read(BuiltInComponents->SpawnableBinding)
		.FilterAny({ BuiltInComponents->Tags.NeedsUnlink })
		.RunInline_PerEntity(&Linker->EntityManager, GateDetail::FUnlinkTask{ &TrackedComponents });

	if (TrackedComponents.Num() == 0)
	{
		return;
	}

	const FInstanceRegistry* InstanceRegistry = Linker->GetInstanceRegistry();

	// ── Phase 2: lazy-resolve component pointers; discard non-CCS entries ──
	//
	// The Spawnables system may or may not have materialised the actor by the
	// time our OnRun fires on a given entity. Re-resolve each frame until the
	// pointer is valid; misses are cheap (a single binding lookup) and stop
	// happening once every actor is live. When we DO resolve an actor that is
	// not a Composable Camera actor, drop the entry from the tracked map —
	// no point revisiting it next frame, it's a plain Spawnable we don't care
	// about (e.g. any other Spawnable coexisting in the same sequence).

	TArray<FGateKey, TInlineAllocator<8>> ToDrop;

	for (TPair<FGateKey, FGateEntry>& Pair : TrackedComponents)
	{
		FGateEntry& Entry = Pair.Value;
		if (Entry.Component.IsValid())
		{
			continue;
		}
		if (!InstanceRegistry->IsHandleValid(Entry.RootInstanceHandle))
		{
			continue;
		}
		const FSequenceInstance& Instance = InstanceRegistry->GetInstance(Entry.RootInstanceHandle);
		UObject* BoundObject = GateDetail::ResolveFirstBoundObject(Instance, Entry.BindingGuid);
		if (!BoundObject)
		{
			// Actor not spawned yet. Retry next frame.
			continue;
		}
		if (UComposableCameraLevelSequenceComponent* Found = GateDetail::FindLSComponent(BoundObject))
		{
			Entry.Component = Found;
		}
		else
		{
			// Resolved to an actor, but it isn't a Composable Camera actor —
			// this Spawnable isn't ours to manage. Drop it so we don't waste
			// resolution work on it every subsequent frame.
			ToDrop.Add(Pair.Key);
		}
	}

	for (const FGateKey& Key : ToDrop)
	{
		TrackedComponents.Remove(Key);
	}

	if (TrackedComponents.Num() == 0)
	{
		return;
	}

	// ── Phase 3: build the active set from Camera Cut Track state ──
	//
	// Read the current frame's cut-target + blend participants by walking
	// every UMovieSceneCameraCutTrackInstance living on the linker. We rely
	// on UMovieSceneTrackInstanceInstantiator::GetTrackInstances() — the same
	// registry the engine uses for camera cuts. OnEndUpdateInputs has already
	// refreshed the inputs for this frame, so each cut section's
	// SectionRange vs. Context time comparison is authoritative.

	TSet<AActor*> ActiveActors;

	int32 CutTrackInstanceCount = 0;
	int32 CutSectionInputCount = 0;
	int32 BlendPartnerAddedCount = 0;

	// Resolve a cut section's bound camera actor(s) into ActiveActors. Tagged
	// with `Source` for the diagnostic log so you can tell which code path
	// added a given actor (input vs. blend-partner via ease window).
	auto AddActiveFromCutSection = [&](
		const UMovieSceneCameraCutSection* Section,
		const FSequenceInstance& Instance,
		const TCHAR* Source) -> int32
	{
		if (!Section)
		{
			return 0;
		}
		int32 Added = 0;
		const TArrayView<TWeakObjectPtr<>> Objects =
			Section->GetCameraBindingID().ResolveBoundObjects(Instance);
		for (const TWeakObjectPtr<>& WeakObj : Objects)
		{
			if (AActor* Actor = Cast<AActor>(WeakObj.Get()))
			{
				bool bAlreadyIn = false;
				ActiveActors.Add(Actor, &bAlreadyIn);
				if (!bAlreadyIn)
				{
					++Added;
					UE_LOG(LogComposableCameraSystem, Verbose,
						TEXT("Gate active: [%s] via %s (section [%s])"),
						*Actor->GetName(), Source, *Section->GetName());
				}
			}
		}
		return Added;
	};

	if (UMovieSceneTrackInstanceInstantiator* TrackInstanceSystem = Linker->FindSystem<UMovieSceneTrackInstanceInstantiator>())
	{
		for (const FMovieSceneTrackInstanceEntry& InstanceEntry : TrackInstanceSystem->GetTrackInstances())
		{
			UMovieSceneCameraCutTrackInstance* CameraCutInstance = Cast<UMovieSceneCameraCutTrackInstance>(InstanceEntry.TrackInstance.Get());
			if (!CameraCutInstance)
			{
				continue;
			}
			++CutTrackInstanceCount;

			for (const FMovieSceneTrackInstanceInput& Input : CameraCutInstance->GetInputs())
			{
				++CutSectionInputCount;

				const UMovieSceneCameraCutSection* InputCutSection = Cast<const UMovieSceneCameraCutSection>(Input.Section);
				if (!InputCutSection)
				{
					continue;
				}
				if (!InstanceRegistry->IsHandleValid(Input.InstanceHandle))
				{
					continue;
				}

				const FSequenceInstance& CutInstance = InstanceRegistry->GetInstance(Input.InstanceHandle);
				const FMovieSceneContext& Context = CutInstance.GetContext();
				const FFrameNumber CurrentTime = Context.GetTime().FloorToFrame();
				const TRange<FFrameNumber> InputRange = InputCutSection->GetTrueRange();

				UE_LOG(LogComposableCameraSystem, Verbose,
					TEXT("Gate cut-section [%s] range=[%d..%d) time=%d contains=%s"),
					*InputCutSection->GetName(),
					InputRange.HasLowerBound() ? InputRange.GetLowerBoundValue().Value : MIN_int32,
					InputRange.HasUpperBound() ? InputRange.GetUpperBoundValue().Value : MAX_int32,
					CurrentTime.Value,
					InputRange.Contains(CurrentTime) ? TEXT("YES") : TEXT("no"));

				// (1) The input section itself: its bound actor is active
				//     whenever its range contains the current time.
				if (InputRange.Contains(CurrentTime))
				{
					AddActiveFromCutSection(InputCutSection, CutInstance, TEXT("input"));
				}

				// (2) Blend-partner detection. PCM's SetViewTargetWithBlend
				//     keeps ticking BOTH the outgoing ViewTarget and the
				//     incoming PendingViewTarget during a blend window. For
				//     same-row overlapping sections, the engine truncates the
				//     earlier section out of GetInputs before its ease-out /
				//     the successor's ease-in ends — the PCM still needs the
				//     truncated camera's LS component live to produce fresh
				//     poses for the blend. We detect "we are inside a blend
				//     window" by checking the input section's ease-in /
				//     ease-out windows, and if so walk Track->GetAllSections()
				//     to pick up any section whose authored (un-truncated)
				//     range still contains the current time — those are the
				//     blend partners PCM is ticking.
				//
				//     For dual-row overlap authoring, both sections appear in
				//     GetInputs separately so this path also hits them (no
				//     harm, dedup via TSet<AActor*>).
				const int32 EaseInDuration  = InputCutSection->Easing.GetEaseInDuration();
				const int32 EaseOutDuration = InputCutSection->Easing.GetEaseOutDuration();

				bool bInEaseInWindow = false;
				if (EaseInDuration > 0 && InputRange.HasLowerBound())
				{
					const FFrameNumber EaseInStart = InputRange.GetLowerBoundValue();
					const FFrameNumber EaseInEnd   = EaseInStart + EaseInDuration;
					bInEaseInWindow = (CurrentTime >= EaseInStart && CurrentTime < EaseInEnd);
				}

				bool bInEaseOutWindow = false;
				if (EaseOutDuration > 0 && InputRange.HasUpperBound())
				{
					const FFrameNumber EaseOutEnd   = InputRange.GetUpperBoundValue();
					const FFrameNumber EaseOutStart = EaseOutEnd - EaseOutDuration;
					bInEaseOutWindow = (CurrentTime >= EaseOutStart && CurrentTime < EaseOutEnd);
				}

				if (bInEaseInWindow || bInEaseOutWindow)
				{
					const UMovieSceneCameraCutTrack* OwnerTrack =
						InputCutSection->GetTypedOuter<UMovieSceneCameraCutTrack>();
					if (OwnerTrack)
					{
						const TCHAR* Source = bInEaseInWindow ? TEXT("ease-in partner") : TEXT("ease-out partner");
						for (const UMovieSceneSection* TrackSection : OwnerTrack->GetAllSections())
						{
							const UMovieSceneCameraCutSection* OtherCutSection =
								Cast<const UMovieSceneCameraCutSection>(TrackSection);
							if (!OtherCutSection || OtherCutSection == InputCutSection)
							{
								continue;
							}
							const TRange<FFrameNumber> OtherRange = OtherCutSection->GetTrueRange();
							if (OtherRange.Contains(CurrentTime))
							{
								BlendPartnerAddedCount += AddActiveFromCutSection(OtherCutSection, CutInstance, Source);
							}
						}
					}
				}
			}
		}
	}

	// Summary log — read this to diagnose pipeline state in one line:
	//   tracked=N          : Spawnables the gate is managing
	//   cutTrackInstances  : # Camera Cut Track instances in the linker (typically 1)
	//   cutSections        : # inputs currently in GetInputs() (engine's post-truncation set)
	//   blendPartners      : # additional actors added via ease-window walk (same-row blends)
	//   active=M           : final ActiveActors size → tick budget for this frame
	UE_LOG(LogComposableCameraSystem, Verbose,
		TEXT("Gate instantiator: tracked=%d cutTrackInstances=%d cutSections=%d blendPartners=%d active=%d"),
		TrackedComponents.Num(), CutTrackInstanceCount, CutSectionInputCount, BlendPartnerAddedCount, ActiveActors.Num());

	// ── Phase 4: flip gates ──
	//
	// For each tracked component, enable iff its owning actor is in ActiveActors.
	// SetEvaluationEnabled(false) tears down the internal camera so idle
	// Spawnables genuinely drop to zero runtime cost.
	//
	// Defensive guard: if we couldn't see ANY UMovieSceneCameraCutTrackInstance
	// in the linker this frame, we have no ground truth for the active set and
	// must NOT flip any gates. Two concrete scenarios this protects:
	//
	// 1) First-frame race on Spawnable creation. The gate system links as soon
	//    as a Spawn section imports its SpawnableBinding entity, which can
	//    happen a frame BEFORE UMovieSceneCameraCutTrackInstance registers via
	//    the TrackInstance system. Without this guard, OnRun runs with
	//    CutTrackInstanceCount=0, produces an empty ActiveActors, and closes
	//    the freshly-OnRegistered Spawnable. If the cut track then links on a
	//    later frame but the section's binding resolution is delayed a tick
	//    (common with Spawnable re-targeting), the gate misses its window to
	//    re-open and the component stays stuck OFF for the rest of the section.
	//
	// 2) Level Sequences with no Camera Cut Track at all. A CCS LS Spawnable
	//    can still be meaningful (its pose-projection drives a CineCamera that
	//    an actor-bound UE camera cut on a DIFFERENT track or a manual
	//    SetViewTarget picks up). Under the original logic, these entries
	//    would sit permanently OFF because ActiveActors was always empty.
	//    Skipping the flip entirely lets them run at OnRegister's default-ON.
	//
	// The design document's "close-only" invariant is preserved: the gate still
	// only closes entries it observes to be inactive via real cut track state.
	// What's new is that "no observable cut track state" now means "leave alone"
	// instead of "treat as inactive".
	if (CutTrackInstanceCount == 0)
	{
		return;
	}

	for (TPair<FGateKey, FGateEntry>& Pair : TrackedComponents)
	{
		UComposableCameraLevelSequenceComponent* Comp = Pair.Value.Component.Get();
		if (!Comp || !IsValid(Comp))
		{
			continue;
		}

		AActor* Owner = Comp->GetOwner();

		// Editor-world bypass: in the Sequencer editor preview path, the gate
		// must not close the component. The gate's close-on-inactive semantic
		// is a performance optimization for PIE / runtime (N Spawnables alive,
		// only 1–2 actually driving the viewport at any given time). In the
		// editor, the user is actively authoring / scrubbing, and two concrete
		// quirks of editor evaluation make gate-close catastrophic there:
		//
		//   (i)  When the scrubber leaves the Camera Cut section, Sequencer
		//        evaluates the cut track's instance at a pre-animate time
		//        (typically one display frame before the section start, e.g.
		//        tick -800 at 30 fps / 24000 tick-res). The gate sees
		//        `contains=no` at this time and closes the component — which
		//        is fine on its own, mirroring the runtime behavior.
		//
		//   (ii) After the scrubber moves back INTO the section, the cut
		//        track's context time can remain stuck at that pre-animate
		//        sample across idle frames (the user is hovering the scrubber,
		//        not actively dragging). The gate keeps seeing `contains=no`,
		//        component stays OFF, scrubbing shows no camera motion even
		//        though Sequencer IS writing to the property-bag (verified via
		//        Details-panel inspection: bag leaves update live).
		//
		// The combination makes the gate's close-in-editor behavior actively
		// destructive to scrubbing preview. Bypass entirely for editor worlds
		// (Editor, EditorPreview). PIE / Game / GamePreview still flow through
		// the full flip logic.
		if (Owner)
		{
			if (UWorld* World = Owner->GetWorld())
			{
				const EWorldType::Type WorldType = World->WorldType;
				if (WorldType == EWorldType::Editor || WorldType == EWorldType::EditorPreview)
				{
					continue;
				}
			}
		}

		const bool bShouldBeActive = Owner && ActiveActors.Contains(Owner);
		if (bShouldBeActive != Comp->IsEvaluationEnabled())
		{
			Comp->SetEvaluationEnabled(bShouldBeActive);
		}
	}
}
