// Copyright 2026 Sulley. All Rights Reserved.

#include "Core/ComposableCameraContextStack.h"

#include "ComposableCameraSystemModule.h"
#include "Core/ComposableCameraDirector.h"
#include "Core/ComposableCameraEvaluationTree.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "DataAssets/ComposableCameraTransitionDataAsset.h"
#include "Transitions/ComposableCameraTransitionBase.h"
#include "Utils/ComposableCameraProjectSettings.h"

UComposableCameraContextStack::UComposableCameraContextStack(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UComposableCameraDirector* UComposableCameraContextStack::EnsureContext(
	AComposableCameraPlayerCameraManager* PlayerCameraManager, FName ContextName)
{
	// If context already exists, move it to the top if needed and return its Director.
	const int32 ExistingIndex = FindContextIndex(ContextName);
	if (ExistingIndex != INDEX_NONE)
	{
		// Already on top. Nothing to do.
		if (ExistingIndex == Entries.Num() - 1)
		{
			return Entries[ExistingIndex].Director;
		}

		// Not on top. Move to top so it becomes the active context.
		FComposableCameraContextEntry Entry = Entries[ExistingIndex];
		Entries.RemoveAt(ExistingIndex);
		Entries.Add(Entry);

		UE_LOG(LogComposableCameraSystem, Log, TEXT("Moved camera context '%s' to top. Stack depth: %d."),
			*ContextName.ToString(), Entries.Num());

		return Entry.Director;
	}

	// Validate the name against project settings.
	const UComposableCameraProjectSettings* Settings = GetDefault<UComposableCameraProjectSettings>();
	if (!Settings->IsValidContextName(ContextName))
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT("Context name '%s' not found in project settings."),
			*ContextName.ToString());
		return nullptr;
	}

	// Create a new Director for this context.
	FName DirectorName = *FString::Printf(TEXT("Director_%s"), *ContextName.ToString());
	UComposableCameraDirector* NewDirector = NewObject<UComposableCameraDirector>(
		PlayerCameraManager, UComposableCameraDirector::StaticClass(), DirectorName);

	// Build the entry and push on top (LIFO).
	FComposableCameraContextEntry NewEntry;
	NewEntry.Director = NewDirector;
	NewEntry.ContextName = ContextName;

	Entries.Add(NewEntry);

	UE_LOG(LogComposableCameraSystem, Log, TEXT("Pushed camera context '%s'. Stack depth: %d."),
		*ContextName.ToString(), Entries.Num());

	return NewDirector;
}

void UComposableCameraContextStack::PopContext(
	FName ContextName,
	AComposableCameraPlayerCameraManager* PlayerCameraManager,
	UComposableCameraTransitionDataAsset* TransitionOverride,
	const FComposableCameraActivateParams& ActivationParams)
{
	const int32 Index = FindContextIndex(ContextName);
	if (Index == INDEX_NONE)
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT("Cannot pop context '%s': not on the stack."),
			*ContextName.ToString());
		return;
	}

	// Prevent popping if this is the last context.
	if (Entries.Num() <= 1)
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT("Cannot pop the base camera context '%s'."),
			*ContextName.ToString());
		return;
	}

	// If this is the active (top) context, use the transition flow.
	if (Index == Entries.Num() - 1)
	{
		PopActiveContextInternal(PlayerCameraManager, TransitionOverride, ActivationParams);
		return;
	}

	// Non-active context: remove immediately (no transition needed).
	FComposableCameraContextEntry& Entry = Entries[Index];
	if (Entry.Director)
	{
		Entry.Director->DestroyAllCameras();
	}
	Entries.RemoveAt(Index);

	UE_LOG(LogComposableCameraSystem, Log, TEXT("Popped non-active camera context '%s'. Stack depth: %d."),
		*ContextName.ToString(), Entries.Num());
}

void UComposableCameraContextStack::PopActiveContext(
	AComposableCameraPlayerCameraManager* PlayerCameraManager,
	UComposableCameraTransitionDataAsset* TransitionOverride,
	const FComposableCameraActivateParams& ActivationParams)
{
	if (Entries.Num() <= 1)
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT("Cannot pop the base camera context."));
		return;
	}

	PopActiveContextInternal(PlayerCameraManager, TransitionOverride, ActivationParams);
}

void UComposableCameraContextStack::PopActiveContextInternal(
	AComposableCameraPlayerCameraManager* PlayerCameraManager,
	UComposableCameraTransitionDataAsset* TransitionOverride,
	const FComposableCameraActivateParams& ActivationParams)
{
	check(Entries.Num() > 1);

	// B = active (top) context being popped.
	FComposableCameraContextEntry PoppedEntry = Entries.Last();
	Entries.RemoveAt(Entries.Num() - 1);

	// A = new active (top) context after pop.
	FComposableCameraContextEntry& ResumeEntry = Entries.Last();

	UE_LOG(LogComposableCameraSystem, Log, TEXT("Popping active context '%s'. Resuming '%s'. Stack depth: %d."),
		*PoppedEntry.ContextName.ToString(), *ResumeEntry.ContextName.ToString(), Entries.Num());

	// Determine the camera class for the resume context.
	AComposableCameraCameraBase* ResumingCamera = ResumeEntry.Director->GetRunningCamera();
	if (!ResumingCamera || !PlayerCameraManager)
	{
		// No running camera in resume context or no PCM. Instant cut, destroy popped context.
		if (PoppedEntry.Director)
		{
			PoppedEntry.Director->DestroyAllCameras();
		}
		return;
	}

	// Resolve transition through the five-tier chain:
	//   1. Caller override ->2. Table ->3. Source exit ->4. Target enter ->5. Cut
	AComposableCameraCameraBase* PoppedCamera = PoppedEntry.Director->GetRunningCamera();
	const UComposableCameraTypeAsset* SourceTypeAsset =
		PoppedCamera ? PoppedCamera->SourceTypeAsset.Get() : nullptr;
	const UComposableCameraTypeAsset* TargetTypeAsset =
		ResumingCamera->SourceTypeAsset.Get();
	UComposableCameraTransitionBase* ResolvedTransition =
		PlayerCameraManager->ResolveTransition(SourceTypeAsset, TargetTypeAsset, TransitionOverride);

	if (!ResolvedTransition)
	{
		// No transition. Instant cut. Destroy popped context immediately.
		if (PoppedEntry.Director)
		{
			PoppedEntry.Director->DestroyAllCameras();
		}

		UE_LOG(LogComposableCameraSystem, Log, TEXT("Context '%s' popped with camera cut (no transition)."),
			*PoppedEntry.ContextName.ToString());
		return;
	}

	// Pop path: resume the ORIGINAL ResumingCamera in place. Do NOT spawn
	// a fresh instance of the same class. The pre-push camera has been
	// ticking throughout the push period (via the pushed context's
	// RefLeaf->ResumeEntry.Director) and its per-node state (damping,
	// interpolators, spline progress, etc.) is continuous. Spawning a
	// replacement instance at pop time would reset all that state and
	// produce a visible "snap" on the first post-pop frame. The new
	// `ResumeCurrentCameraWithReferenceSource` path wraps the existing
	// tree root (holding ResumingCamera) under a new pop Inner node
	// with a RefLeaf ->popped director as the blend source.
	UComposableCameraTransitionBase* PopTransition = DuplicateObject(ResolvedTransition, PlayerCameraManager);

	// Pop with live (not frozen) source: the RefLeaf we install below
	// captures a TSharedPtr snapshot of the popped director's tree. That
	// snapshot re-evaluates each frame. The popped cameras and any
	// in-flight push transition inside them keep ticking. But the
	// snapshot root is fixed at the shape the popped tree had AT POP TIME,
	// so it cannot self-reference the newly-installed pop Inner. No cycle,
	// no feedback loop, no need to freeze.
	//
	// `bFreezeSourceCamera` is a semantic option for transition authors
	// who explicitly want "B holds its last pose during the pop blend"
	// behaviour (e.g. a cinematic freeze-frame). It is NOT needed to make
	// pop mechanically safe anymore. Default to false. Honour whatever
	// the transition data asset / type asset specified.
	ResumeEntry.Director->ResumeCurrentCameraWithReferenceSource(
		PlayerCameraManager,
		PopTransition,
		PoppedEntry.Director,
		/*bFreezeSourceCamera=*/false);

	// `ActivationParams` / `PrepareResumeCallback` are intentionally
	// UNUSED on the resume path: the resuming camera already exists with
	// its full RuntimeDataBlock, node setup, and exec chain. No
	// OnTypeAssetCameraConstructed callback is needed, no InitialTransform
	// needs applying. (The old activate-new path needed those because it
	// was spawning a fresh empty shell.) The `ActivationParams` argument
	// is kept on the public pop API for source-compat; it's a no-op for
	// this flow.
	(void)ActivationParams;

	if (PopTransition)
	{
		// Hold B in pending destruction until the transition finishes.
		PendingDestroyEntries.Add(PoppedEntry);

		PopTransition->OnTransitionFinishesDelegate.AddWeakLambda(
			this,
			[this, PoppedContextName = PoppedEntry.ContextName]()
			{
				// Find and destroy the pending entry.
				for (int32 i = PendingDestroyEntries.Num() - 1; i >= 0; --i)
				{
					if (PendingDestroyEntries[i].ContextName == PoppedContextName)
					{
						if (PendingDestroyEntries[i].Director)
						{
							PendingDestroyEntries[i].Director->DestroyAllCameras();
						}
						PendingDestroyEntries.RemoveAt(i);

						UE_LOG(LogComposableCameraSystem, Log, TEXT("Pending context '%s' destroyed after pop transition finished."),
							*PoppedContextName.ToString());
						break;
					}
				}
			});

		UE_LOG(LogComposableCameraSystem, Log, TEXT("Context '%s' popping with transition. Held in pending destruction."),
			*PoppedEntry.ContextName.ToString());
	}
	else
	{
		// DuplicateObject returned nullptr (shouldn't happen in practice
		// since ResolvedTransition was already non-null above, but guard
		// anyway). Instant cut path, destroy popped director immediately.
		if (PoppedEntry.Director)
		{
			PoppedEntry.Director->DestroyAllCameras();
		}
	}
}

void UComposableCameraContextStack::DemoteNonTopTransientContextsToPending(
	UComposableCameraTransitionBase* ActivatingTransition)
{
	if (Entries.Num() <= 1)
	{
		return;
	}

	// Walk non-top entries from bottom up and move any whose running camera is
	// transient. `Entries.Num() - 1` is the new active context. Never touched.
	// Reverse iteration (top-side-first among the non-top range) keeps the
	// indices of not-yet-visited entries stable across RemoveAt.
	for (int32 i = Entries.Num() - 2; i >= 0; --i)
	{
		FComposableCameraContextEntry& Entry = Entries[i];
		AComposableCameraCameraBase* Camera = Entry.Director
			? Entry.Director->GetRunningCamera()
			: nullptr;

		if (!Camera || !Camera->IsTransient())
		{
			continue;
		}

		const FName DemotedName = Entry.ContextName;

		if (ActivatingTransition)
		{
			// Mirror the explicit-pop path: move the entry into the pending
			// bucket; its director stays GC-alive and tickable via the
			// activating tree's RefLeaf snapshot until the blend resolves.
			PendingDestroyEntries.Add(Entry);
			Entries.RemoveAt(i);

			ActivatingTransition->OnTransitionFinishesDelegate.AddWeakLambda(
				this,
				[this, DemotedName]()
				{
					for (int32 j = PendingDestroyEntries.Num() - 1; j >= 0; --j)
					{
						if (PendingDestroyEntries[j].ContextName == DemotedName)
						{
							if (PendingDestroyEntries[j].Director)
							{
								PendingDestroyEntries[j].Director->DestroyAllCameras();
							}
							PendingDestroyEntries.RemoveAt(j);

							UE_LOG(LogComposableCameraSystem, Log, TEXT(
								"Demoted transient context '%s' destroyed after activation transition finished."),
								*DemotedName.ToString());
							break;
						}
					}
				});

			UE_LOG(LogComposableCameraSystem, Log, TEXT(
				"Context '%s' implicitly popped (transient camera + demoted from top by inter-context activation). Held in pending destruction."),
				*DemotedName.ToString());
		}
		else
		{
			// No blend. There is no RefLeaf path keeping the camera alive,
			// so nothing is reading from it after this frame. Destroy now to
			// match the single-frame semantics of a cut.
			if (Entry.Director)
			{
				Entry.Director->DestroyAllCameras();
			}
			Entries.RemoveAt(i);

			UE_LOG(LogComposableCameraSystem, Log, TEXT(
				"Context '%s' implicitly popped (transient camera, inter-context cut)."),
				*DemotedName.ToString());
		}
	}
}

UComposableCameraDirector* UComposableCameraContextStack::GetActiveDirector() const
{
	if (Entries.Num() > 0)
	{
		return Entries.Last().Director;
	}
	return nullptr;
}

UComposableCameraDirector* UComposableCameraContextStack::GetDirectorForContext(FName ContextName) const
{
	const int32 Index = FindContextIndex(ContextName);
	if (Index != INDEX_NONE)
	{
		return Entries[Index].Director;
	}
	return nullptr;
}

AComposableCameraCameraBase* UComposableCameraContextStack::GetRunningCamera() const
{
	if (UComposableCameraDirector* Director = GetActiveDirector())
	{
		return Director->GetRunningCamera();
	}
	return nullptr;
}

FName UComposableCameraContextStack::GetActiveContextName() const
{
	if (Entries.Num() > 0)
	{
		return Entries.Last().ContextName;
	}
	return NAME_None;
}

DECLARE_CYCLE_STAT(TEXT("ContextStack Evaluate"), STAT_CCS_ContextStack_Evaluate, STATGROUP_CCS);

FComposableCameraPose UComposableCameraContextStack::Evaluate(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_CCS_ContextStack_Evaluate);
	TRACE_CPUPROFILER_EVENT_SCOPE(CCS_ContextStack_Evaluate);

	if (Entries.Num() == 0)
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT("Context stack is empty."));
		return FComposableCameraPose{};
	}

	// Copy the active entry index rather than holding a reference. PopActiveContextInternal
	// mutates the Entries array, which would invalidate any reference to Entries.Last().
	const int32 ActiveIndex = Entries.Num() - 1;

	if (!Entries[ActiveIndex].Director)
	{
		UE_LOG(LogComposableCameraSystem, Error, TEXT("Active context '%s' has no Director."),
			*Entries[ActiveIndex].ContextName.ToString());
		return FComposableCameraPose{};
	}

	// Evaluate only the active (top) context.
	// Lower contexts may still tick if the active context's tree contains a reference leaf
	// pointing to them. The reference leaf calls Director->Evaluate() on the source context.
	UComposableCameraDirector* ActiveDirector = Entries[ActiveIndex].Director;
	FComposableCameraPose ResultPose = ActiveDirector->Evaluate(DeltaTime);
	Entries[ActiveIndex].LastPose = ResultPose;

	// Auto-pop: if the active context's running camera is transient and finished,
	// pop the context with a transition. The camera drives lifecycle, not the context.
	if (Entries.Num() > 1)
	{
		AComposableCameraCameraBase* RunningCamera = ActiveDirector->GetRunningCamera();
		if (RunningCamera && RunningCamera->IsTransient() && RunningCamera->IsFinished())
		{
			FName PoppedName = Entries[ActiveIndex].ContextName;
			UE_LOG(LogComposableCameraSystem, Log, TEXT("Context '%s' auto-popping (camera finished)."),
				*PoppedName.ToString());

			// Use the PCM from the outer (the PlayerCameraManager that owns this stack).
			AComposableCameraPlayerCameraManager* PCM = Cast<AComposableCameraPlayerCameraManager>(GetOuter());

			FComposableCameraActivateParams ActiveParams;
			ActiveParams.bPreserveCameraPose = RunningCamera->bDefaultPreserveCameraPose;

			// Pass nullptr as TransitionOverride. PopActiveContextInternal will
			// resolve the transition through the five-tier chain (table, source
			// exit, target enter, etc.) using ResolveTransition.
			PopActiveContextInternal(PCM, nullptr, ActiveParams);
		}
	}

	return ResultPose;
}

void UComposableCameraContextStack::BuildDebugSnapshot(FComposableCameraContextStackSnapshot& OutSnapshot) const
{
	OutSnapshot.Contexts.Reset();
	OutSnapshot.LiveStackDepth     = Entries.Num();
	OutSnapshot.PendingDestroyCount = PendingDestroyEntries.Num();

	OutSnapshot.Contexts.Reserve(Entries.Num() + PendingDestroyEntries.Num());

	// Live entries emitted top ->base so the visual layout reads "active at the
	// top, base at the bottom" for every consumer (panel + showdebug + dumps).
	for (int32 i = Entries.Num() - 1; i >= 0; --i)
	{
		const FComposableCameraContextEntry& Entry = Entries[i];
		FComposableCameraContextSnapshot Snap;
		Snap.ContextName       = Entry.ContextName;
		Snap.bIsActive         = (i == Entries.Num() - 1);
		Snap.bIsBase           = (i == 0);
		Snap.bIsPendingDestroy = false;
		if (Entry.Director)
		{
			Entry.Director->BuildDebugSnapshot(Snap);
		}
		OutSnapshot.Contexts.Add(MoveTemp(Snap));
	}

	// Pending-destroy entries. Still evaluating through reference leaves,
	// shown after the live stack so the reader sees them "trailing off".
	for (const FComposableCameraContextEntry& Entry : PendingDestroyEntries)
	{
		FComposableCameraContextSnapshot Snap;
		Snap.ContextName       = Entry.ContextName;
		Snap.bIsActive         = false;
		Snap.bIsBase           = false;
		Snap.bIsPendingDestroy = true;
		if (Entry.Director)
		{
			Entry.Director->BuildDebugSnapshot(Snap);
		}
		OutSnapshot.Contexts.Add(MoveTemp(Snap));
	}
}

void UComposableCameraContextStack::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UComposableCameraContextStack* This = CastChecked<UComposableCameraContextStack>(InThis);
	// Walk Entry.Director (UObject ref) and the embedded `LastPose` whose
	// `FPostProcessSettings` carries TObjectPtr refs invisible to the
	// reflection walk (FComposableCameraContextEntry's LastPose is a non-
	// UPROPERTY struct field. See ComposableCameraContextStack.h). Without
	// the explicit pose walk, a color-grading material referenced ONLY
	// through a stack-entry's saved pose can be GC'd while the entry sits
	// underneath the active context, then surface a dangling pointer when
	// the user pops back to that entry.
	for (FComposableCameraContextEntry& Entry : This->Entries)
	{
		Collector.AddReferencedObject(Entry.Director);
		Collector.AddPropertyReferencesWithStructARO(
			FComposableCameraPose::StaticStruct(),
			&Entry.LastPose);
	}
	for (FComposableCameraContextEntry& Entry : This->PendingDestroyEntries)
	{
		Collector.AddReferencedObject(Entry.Director);
		Collector.AddPropertyReferencesWithStructARO(
			FComposableCameraPose::StaticStruct(),
			&Entry.LastPose);
	}
	Super::AddReferencedObjects(InThis, Collector);
}

int32 UComposableCameraContextStack::FindContextIndex(FName ContextName) const
{
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		if (Entries[i].ContextName == ContextName)
		{
			return i;
		}
	}
	return INDEX_NONE;
}
