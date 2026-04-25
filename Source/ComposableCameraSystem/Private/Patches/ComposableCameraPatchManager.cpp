// Copyright Sulley. All rights reserved.

#include "Patches/ComposableCameraPatchManager.h"

#include "ComposableCameraSystemModule.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "Core/ComposableCameraDirector.h"
#include "Core/ComposableCameraTypeAssetInstantiator.h"
#include "DataAssets/ComposableCameraPatchTypeAsset.h"
#include "Debug/ComposableCameraDebugPanelData.h"
#include "Engine/World.h"
#include "Patches/ComposableCameraPatchEnvelope.h"
#include "Patches/ComposableCameraPatchHandle.h"
#include "Patches/ComposableCameraPatchInstance.h"
#include "Stats/Stats.h"

// `stat CCS` numeric counters for the per-frame Patch hot path. Sit alongside
// the existing PCM / Director / EvaluationTree counters in STATGROUP_CCS so a
// single `stat CCS` command surfaces patch overhead next to its peers.
DECLARE_CYCLE_STAT(TEXT("PatchManager Apply"),  STAT_CCS_PatchManager_Apply,  STATGROUP_CCS);
DECLARE_CYCLE_STAT(TEXT("Patch TickEvaluator"), STAT_CCS_Patch_TickEvaluator, STATGROUP_CCS);

namespace
{
	/**
	 * Advance the envelope state machine by DeltaTime.
	 *
	 *   Entering : alpha = ease(t),         t = ElapsedInPhase / EnterDuration; transition → Active when t ≥ 1.
	 *   Active   : alpha = 1; ElapsedTimeActive accumulates (Stage 4 wires Duration channel against it).
	 *   Exiting  : alpha = ExitStartAlpha · (1 - ease(t)), t = ElapsedInPhase / ExitDuration; transition → Expired when t ≥ 1.
	 *   Expired  : no-op; Apply's end-of-pass sweep removes the entry.
	 *
	 * EnterDuration / ExitDuration ≤ 0 short-circuit to the destination phase
	 * on the same call (no wasted frame at α=0).
	 */
	void AdvancePatchEnvelope(UComposableCameraPatchInstance* Instance, float DeltaTime)
	{
		Instance->ElapsedInPhase += DeltaTime;

		switch (Instance->Phase)
		{
			case EComposableCameraPatchPhase::Entering:
			{
				if (Instance->EnterDuration <= 0.f)
				{
					Instance->Phase = EComposableCameraPatchPhase::Active;
					Instance->ElapsedInPhase = 0.f;
					Instance->CurrentAlpha = 1.f;
				}
				else
				{
					const float t = Instance->ElapsedInPhase / Instance->EnterDuration;
					Instance->CurrentAlpha = UE::ComposableCameras::PatchEnvelope::ApplyEase(Instance->EaseType, t);
					if (t >= 1.f)
					{
						Instance->Phase = EComposableCameraPatchPhase::Active;
						Instance->ElapsedInPhase = 0.f;
						Instance->CurrentAlpha = 1.f;
					}
				}
				break;
			}
			case EComposableCameraPatchPhase::Active:
			{
				Instance->ElapsedTimeActive += DeltaTime;
				Instance->CurrentAlpha = 1.f;
				break;
			}
			case EComposableCameraPatchPhase::Exiting:
			{
				if (Instance->ExitDuration <= 0.f)
				{
					Instance->Phase = EComposableCameraPatchPhase::Expired;
					Instance->CurrentAlpha = 0.f;
				}
				else
				{
					const float t = Instance->ElapsedInPhase / Instance->ExitDuration;
					Instance->CurrentAlpha =
						Instance->ExitStartAlpha * (1.f - UE::ComposableCameras::PatchEnvelope::ApplyEase(Instance->EaseType, t));
					if (t >= 1.f)
					{
						Instance->Phase = EComposableCameraPatchPhase::Expired;
						Instance->CurrentAlpha = 0.f;
					}
				}
				break;
			}
			case EComposableCameraPatchPhase::Expired:
				// Sweep handles removal; no work here.
				break;
		}
	}

	/**
	 * Shared "flip to Exiting" transition used by both manual ExpirePatch and
	 * automatic schedule expiration. Snapshots CurrentAlpha as the exit ramp's
	 * amplitude (so Entering-mid-fade → Exiting doesn't pop) and short-circuits
	 * to Expired when there's nothing to fade from (alpha ≤ 0 or ExitDuration ≤ 0).
	 */
	void TransitionPatchToExiting(UComposableCameraPatchInstance* Instance)
	{
		Instance->ExitStartAlpha = Instance->CurrentAlpha;
		if (Instance->CurrentAlpha <= 0.f || Instance->ExitDuration <= 0.f)
		{
			Instance->Phase = EComposableCameraPatchPhase::Expired;
			Instance->CurrentAlpha = 0.f;
		}
		else
		{
			Instance->Phase = EComposableCameraPatchPhase::Exiting;
			Instance->ElapsedInPhase = 0.f;
		}
	}

	/**
	 * Check the Active-phase schedule for a Patch and flip to Exiting if any
	 * enabled channel fires. No-op if the Patch is not in Active phase.
	 *
	 * Channels checked, in first-to-fire order:
	 *   - Duration: ElapsedTimeActive ≥ Duration.
	 *   - Condition: Asset's CanRemain(DeltaTime, UpstreamPose) returns false.
	 *   - bExpireOnCameraChange: Director's RunningCamera differs from the
	 *     camera seen at AddPatch time.
	 *
	 * UpstreamPose is the pose the Patch would act on this frame (output of
	 * tree + all lower-layer Patches); Condition callbacks can inspect it to
	 * decide in/out. CurrentRunningCamera is the Director's RunningCamera
	 * resolved once per Apply and passed in to avoid per-patch outer walks.
	 */
	void CheckPatchScheduleExpiration(
		UComposableCameraPatchInstance* Instance,
		float DeltaTime,
		const FComposableCameraPose& UpstreamPose,
		AComposableCameraCameraBase* CurrentRunningCamera)
	{
		if (Instance->Phase != EComposableCameraPatchPhase::Active)
		{
			return;
		}

		const uint8 ExType = Instance->ExpirationType;
		bool bShouldExit = false;

		if ((ExType & static_cast<uint8>(EComposableCameraPatchExpirationType::Duration)) != 0
			&& Instance->ElapsedTimeActive >= Instance->Duration)
		{
			bShouldExit = true;
		}

		if (!bShouldExit
			&& (ExType & static_cast<uint8>(EComposableCameraPatchExpirationType::Condition)) != 0)
		{
			if (UComposableCameraPatchTypeAsset* Asset = Instance->SourcePatchAsset.Get())
			{
				if (!Asset->CanRemain(DeltaTime, UpstreamPose))
				{
					bShouldExit = true;
				}
			}
		}

		if (!bShouldExit && Instance->bExpireOnCameraChange)
		{
			if (Instance->RunningCameraAtAdd.Get() != CurrentRunningCamera)
			{
				bShouldExit = true;
			}
		}

		if (bShouldExit)
		{
			TransitionPatchToExiting(Instance);
		}
	}
}

UComposableCameraPatchManager::UComposableCameraPatchManager()
{
	// Cold-path reservation. Per PatchSystemProposal §14: typical scene has
	// 0–4 active patches; reserving 8 keeps AddPatch alloc-free for the
	// overwhelming majority of usages.
	ActivePatches.Reserve(8);
}

UComposableCameraPatchHandle* UComposableCameraPatchManager::AddPatch(
	UComposableCameraPatchTypeAsset* PatchAsset,
	const FComposableCameraPatchActivateParams& Params,
	const FComposableCameraParameterBlock& ParameterBlock)
{
	if (!PatchAsset)
	{
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("PatchManager::AddPatch rejected: PatchAsset is null."));
		return nullptr;
	}

	// Sentinel resolution — each overridable field is gated by its own paired
	// bOverride* bool (FPostProcessSettings::bOverride_* idiom). Unchecked →
	// asset default; checked → caller value wins, even when the value is 0.
	// See FComposableCameraPatchActivateParams doc for the full design rationale.
	const uint8 EffectiveExpirationType = Params.bOverrideExpirationType
		? Params.ExpirationType
		: PatchAsset->DefaultExpirationType;

	const float EffectiveDuration = Params.bOverrideDuration
		? Params.Duration
		: PatchAsset->DefaultDuration;

	if ((EffectiveExpirationType & static_cast<uint8>(EComposableCameraPatchExpirationType::Duration)) != 0
		&& EffectiveDuration <= 0.f)
	{
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("PatchManager::AddPatch rejected: Duration channel enabled but resolved Duration <= 0 ('%s')."),
			*PatchAsset->GetName());
		return nullptr;
	}

	const float EffectiveEnter = Params.bOverrideEnterDuration
		? Params.EnterDuration
		: PatchAsset->DefaultEnterDuration;

	const float EffectiveExit = Params.bOverrideExitDuration
		? Params.ExitDuration
		: PatchAsset->DefaultExitDuration;

	const int32 EffectiveLayer = Params.bOverrideLayerIndex
		? Params.LayerIndex
		: PatchAsset->DefaultLayerIndex;

	// Spawn the Patch evaluator as a stand-alone CameraBase actor. Mirrors the
	// LS component's pattern (see UComposableCameraLevelSequenceComponent::EnsureInternalCamera):
	// transient, no autonomous tick, PCM-independent Initialize. Construction
	// happens at AddPatch (cold path) so the per-frame Apply pass is alloc-free.
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("PatchManager::AddPatch rejected: no UWorld reachable from PatchManager (typical for "
			     "test-only Director instances created without an Outer chain)."));
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = GetTypedOuter<AActor>();   // Typically the PCM; nullptr is acceptable.
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.bNoFail = true;

	AComposableCameraCameraBase* Evaluator = World->SpawnActor<AComposableCameraCameraBase>(
		AComposableCameraCameraBase::StaticClass(),
		FTransform::Identity,
		SpawnParams);

	if (!Evaluator)
	{
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("PatchManager::AddPatch rejected: SpawnActor returned null for Patch '%s'."),
			*PatchAsset->GetName());
		return nullptr;
	}

	// We drive the evaluator manually via Apply each frame; suppress its actor tick
	// to avoid a wasted scheduler visit. Same pattern as the LS component.
	Evaluator->SetActorTickEnabled(false);

	// PCM-independent init — Patch evaluators do not participate in PCM-level
	// Action dispatch (PatchSystemProposal §16.9). The CameraBase's
	// Initialize(nullptr) path skips BindCameraActionsForNewCamera, so this is
	// just the existing LS-compatible spine.
	Evaluator->Initialize(/*Manager=*/nullptr);

	UE::ComposableCameras::ConstructCameraFromTypeAsset(Evaluator, PatchAsset, ParameterBlock);

	// Build the runtime instance.
	UComposableCameraPatchInstance* Instance = NewObject<UComposableCameraPatchInstance>(this);
	Instance->SourcePatchAsset = PatchAsset;
	Instance->Evaluator = Evaluator;
	Instance->LayerIndex = EffectiveLayer;
	Instance->PushSequence = NextPushSequence++;
	Instance->ExpirationType = EffectiveExpirationType;
	Instance->Duration = EffectiveDuration;
	Instance->bExpireOnCameraChange = Params.bExpireOnCameraChange;
	Instance->EnterDuration = EffectiveEnter;
	Instance->ExitDuration = EffectiveExit;
	Instance->EaseType = PatchAsset->DefaultEaseType;
	// Stage 3: Patch starts in Entering at alpha = 0. Apply's AdvanceEnvelope
	// ramps alpha 0 → 1 over EnterDuration before the first BlendBy contribution.
	// EnterDuration <= 0 short-circuits to Active at alpha = 1 on the first
	// AdvanceEnvelope call — no wasted invisible frame.
	Instance->Phase = EComposableCameraPatchPhase::Entering;
	Instance->ElapsedInPhase = 0.f;
	Instance->ElapsedTimeActive = 0.f;
	Instance->CurrentAlpha = 0.f;
	Instance->ExitStartAlpha = 1.f;
	Instance->CachedParameters = ParameterBlock;

	// Cache the Director's RunningCamera at AddPatch time so bExpireOnCameraChange
	// can detect subsequent camera changes per-patch. Resolved via the outer chain
	// (PatchManager → Director); a null Director yields a null weak ptr which
	// matches correctly against a subsequent null RunningCamera.
	if (UComposableCameraDirector* OwningDirector = GetTypedOuter<UComposableCameraDirector>())
	{
		Instance->RunningCameraAtAdd = OwningDirector->GetRunningCamera();
	}

	// Sorted insert by (LayerIndex ascending, PushSequence ascending). Linear scan
	// is appropriate for the expected size (≤ ~8 entries); the array is reserved
	// up front so this is O(N) without reallocation in the typical case.
	int32 InsertIndex = ActivePatches.Num();
	for (int32 i = 0; i < ActivePatches.Num(); ++i)
	{
		const UComposableCameraPatchInstance* Existing = ActivePatches[i];
		if (Existing && Instance->LayerIndex < Existing->LayerIndex)
		{
			InsertIndex = i;
			break;
		}
	}
	ActivePatches.Insert(Instance, InsertIndex);

	// Hand out the user-facing handle.
	UComposableCameraPatchHandle* Handle = NewObject<UComposableCameraPatchHandle>(this);
	Handle->BindInstance(Instance);
	Instance->Handle = Handle;

	UE_LOG(LogComposableCameraSystem, Verbose,
		TEXT("PatchManager::AddPatch '%s' (layer=%d, seq=%d, expirationMask=0x%02x)."),
		*PatchAsset->GetName(), Instance->LayerIndex, Instance->PushSequence,
		Instance->ExpirationType);

	return Handle;
}

void UComposableCameraPatchManager::ExpirePatch(
	UComposableCameraPatchHandle* Handle, float ExitDurationOverride)
{
	if (!Handle)
	{
		return;
	}
	UComposableCameraPatchInstance* Instance = Handle->GetInstance();
	if (!Instance)
	{
		return;
	}

	// Idempotent: a second ExpirePatch on a Patch that's already exiting (or
	// expired) does nothing, which avoids resetting ElapsedInPhase and prolonging
	// the fade.
	if (Instance->Phase == EComposableCameraPatchPhase::Exiting ||
		Instance->Phase == EComposableCameraPatchPhase::Expired)
	{
		return;
	}

	// Caller-supplied override wins when non-negative; -1 sentinel keeps the
	// asset's authored ExitDuration. Pass 0 for a hard cut-off (no fade).
	if (ExitDurationOverride >= 0.f)
	{
		Instance->ExitDuration = ExitDurationOverride;
	}

	TransitionPatchToExiting(Instance);

	UE_LOG(LogComposableCameraSystem, Verbose,
		TEXT("PatchManager::ExpirePatch (phase=%d, alpha=%.3f, exit=%.3fs)."),
		static_cast<int32>(Instance->Phase), Instance->ExitStartAlpha, Instance->ExitDuration);
}

FComposableCameraPose UComposableCameraPatchManager::Apply(
	float DeltaTime, const FComposableCameraPose& InputPose)
{
	SCOPE_CYCLE_COUNTER(STAT_CCS_PatchManager_Apply);
	TRACE_CPUPROFILER_EVENT_SCOPE(CCS_PatchManager_Apply);

	if (ActivePatches.Num() == 0)
	{
		return InputPose;
	}

	// Resolve the owning Director's RunningCamera once per Apply — the schedule
	// check needs it for the bExpireOnCameraChange flag. A null Director (test
	// paths) yields a null camera, which matches correctly against any Patch's
	// RunningCameraAtAdd snapshot (null → null is "no change").
	UComposableCameraDirector* OwningDirector = GetTypedOuter<UComposableCameraDirector>();
	AComposableCameraCameraBase* CurrentRunningCamera =
		OwningDirector ? OwningDirector->GetRunningCamera() : nullptr;

	// Iterate sorted ActivePatches (LayerIndex asc, PushSequence asc — see AddPatch).
	// For each patch:
	//   1. Advance the envelope state machine — may flip Phase and update CurrentAlpha.
	//   2. Check schedule channels (Duration / Condition / OnCameraChange) — may flip
	//      Active → Exiting. Runs AFTER AdvanceEnvelope so Entering → Active → schedule
	//      transitions can happen in the correct order on a single frame.
	//   3. If Expired (envelope finished, schedule fired with ExitDuration≤0, or
	//      short-circuited via ExpirePatch), skip ticking.
	//   4. Otherwise, tick the evaluator with the running upstream pose and contribute
	//      its result by BlendBy at the new CurrentAlpha. The chain semantics let
	//      higher-layer patches see lower-layer patches' output as their upstream.
	FComposableCameraPose Result = InputPose;
	for (UComposableCameraPatchInstance* Instance : ActivePatches)
	{
		if (!Instance)
		{
			continue;
		}

		AdvancePatchEnvelope(Instance, DeltaTime);
		CheckPatchScheduleExpiration(Instance, DeltaTime, Result, CurrentRunningCamera);

		if (Instance->Phase == EComposableCameraPatchPhase::Expired)
		{
			continue;
		}
		if (!Instance->Evaluator || !IsValid(Instance->Evaluator))
		{
			continue;
		}

		FComposableCameraPose Evaluated;
		{
			SCOPE_CYCLE_COUNTER(STAT_CCS_Patch_TickEvaluator);
			TRACE_CPUPROFILER_EVENT_SCOPE(CCS_Patch_TickEvaluator);
			// Per-Patch-asset breakout in Insights — dynamic name keeps the
			// timeline readable when several Patches are active simultaneously.
			if (UComposableCameraPatchTypeAsset* Asset = Instance->SourcePatchAsset.Get())
			{
				TRACE_CPUPROFILER_EVENT_SCOPE_STR(*Asset->GetName());
			}
			Evaluated = Instance->Evaluator->TickWithInputPose(DeltaTime, Result);
		}
		Result.BlendBy(Evaluated, Instance->CurrentAlpha);
	}

	// End-of-pass sweep: destroy evaluators for and remove every Expired entry.
	// Reverse iteration is the standard safe pattern for in-place RemoveAt.
	// Doing this AFTER the iteration (rather than mid-loop) means we never
	// invalidate the loop's traversal even if multiple entries expire on the
	// same frame.
	for (int32 i = ActivePatches.Num() - 1; i >= 0; --i)
	{
		UComposableCameraPatchInstance* Instance = ActivePatches[i];
		if (!Instance || Instance->Phase == EComposableCameraPatchPhase::Expired)
		{
			if (Instance && Instance->Evaluator && IsValid(Instance->Evaluator))
			{
				Instance->Evaluator->Destroy();
				Instance->Evaluator = nullptr;
			}
			ActivePatches.RemoveAt(i);
		}
	}

	return Result;
}

void UComposableCameraPatchManager::ApplyParameterBlockToActivePatch(
	UComposableCameraPatchHandle* Handle,
	const FComposableCameraParameterBlock& Parameters)
{
	if (!Handle)
	{
		return;
	}
	UComposableCameraPatchInstance* Instance = Handle->GetInstance();
	if (!Instance)
	{
		return;
	}
	// Skip Patches whose evaluator has already been destroyed (Expired and swept)
	// or that are mid-Exiting — pushing parameters into a fade-out evaluator just
	// flickers the last visible alpha frames. Sequencer keying is for "live"
	// patches only; the exit ramp is its own concern.
	if (Instance->Phase == EComposableCameraPatchPhase::Exiting ||
		Instance->Phase == EComposableCameraPatchPhase::Expired)
	{
		return;
	}
	AComposableCameraCameraBase* Evaluator = Instance->Evaluator;
	if (!Evaluator || !IsValid(Evaluator) || !Evaluator->OwnedRuntimeDataBlock)
	{
		return;
	}
	UComposableCameraPatchTypeAsset* Asset = Instance->SourcePatchAsset.Get();
	if (!Asset)
	{
		return;
	}
	// Cache the latest block on the instance too — keeps any future
	// re-construction (e.g. a hypothetical asset-modified hot-reload path)
	// reading the most-recently-keyed values instead of the AddPatch snapshot.
	Instance->CachedParameters = Parameters;
	Asset->ApplyParameterBlock(*Evaluator->OwnedRuntimeDataBlock, Parameters);
}

void UComposableCameraPatchManager::ExpireAll(float ExitDurationOverride)
{
	// Soft sweep: route every still-live patch through TransitionPatchToExiting
	// (via per-handle ExpirePatch) so each one runs its own exit ramp. Removal
	// is deferred to Apply's end-of-pass sweep, same as for individual ExpirePatch
	// calls — that way iteration order here is stable even if a future patch
	// node's exit envelope ends up triggering side effects.
	for (UComposableCameraPatchInstance* Instance : ActivePatches)
	{
		if (!Instance)
		{
			continue;
		}
		if (Instance->Phase == EComposableCameraPatchPhase::Exiting ||
			Instance->Phase == EComposableCameraPatchPhase::Expired)
		{
			continue;
		}
		ExpirePatch(Instance->Handle.Get(), ExitDurationOverride);
	}
}

void UComposableCameraPatchManager::DestroyAll()
{
	for (UComposableCameraPatchInstance* Instance : ActivePatches)
	{
		if (Instance && Instance->Evaluator && IsValid(Instance->Evaluator))
		{
			Instance->Evaluator->Destroy();
			Instance->Evaluator = nullptr;
		}
	}
	ActivePatches.Reset();
}

void UComposableCameraPatchManager::BuildDebugSnapshot(
	TArray<FComposableCameraPatchSnapshot>& OutPatches) const
{
	OutPatches.Reset(ActivePatches.Num());
	for (const UComposableCameraPatchInstance* Instance : ActivePatches)
	{
		if (!Instance)
		{
			continue;
		}
		FComposableCameraPatchSnapshot& Snap = OutPatches.AddDefaulted_GetRef();
		if (const UComposableCameraPatchTypeAsset* Asset = Instance->SourcePatchAsset.Get())
		{
			Snap.AssetName = Asset->GetName();
		}
		else
		{
			Snap.AssetName = TEXT("(missing)");
		}
		Snap.LayerIndex            = Instance->LayerIndex;
		Snap.Phase                 = static_cast<int8>(Instance->Phase);
		Snap.Alpha                 = Instance->CurrentAlpha;
		Snap.ElapsedInPhase        = Instance->ElapsedInPhase;
		Snap.ElapsedTimeActive     = Instance->ElapsedTimeActive;
		Snap.EnterDuration         = Instance->EnterDuration;
		Snap.ExitDuration          = Instance->ExitDuration;
		Snap.Duration              = Instance->Duration;
		Snap.ExpirationType        = Instance->ExpirationType;
		Snap.bExpireOnCameraChange = Instance->bExpireOnCameraChange;
	}
}
