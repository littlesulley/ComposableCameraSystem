// Copyright Sulley. All rights reserved.

#include "Core/ComposableCameraContextStack.h"

#include "ComposableCameraSystemModule.h"
#include "Core/ComposableCameraDirector.h"
#include "Core/ComposableCameraEvaluationTree.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "DataAssets/ComposableCameraTransitionDataAsset.h"
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
		// Already on top — nothing to do.
		if (ExistingIndex == Entries.Num() - 1)
		{
			return Entries[ExistingIndex].Director;
		}

		// Not on top — move to top so it becomes the active context.
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
		// No running camera in resume context or no PCM — instant cut, destroy popped context.
		if (PoppedEntry.Director)
		{
			PoppedEntry.Director->DestroyAllCameras();
		}
		return;
	}

	TSubclassOf<AComposableCameraCameraBase> CameraClass = ResumingCamera->GetClass();
	UComposableCameraTransitionDataAsset* TransitionDataAsset = TransitionOverride;
	FComposableCameraActivateParams ResumeActivationParams = ActivationParams;

	if (!TransitionDataAsset)
	{
		// No transition — instant cut. Destroy popped context immediately.
		if (PoppedEntry.Director)
		{
			PoppedEntry.Director->DestroyAllCameras();
		}

		UE_LOG(LogComposableCameraSystem, Log, TEXT("Context '%s' popped with camera cut (no transition)."),
			*PoppedEntry.ContextName.ToString());
		return;
	}

	// Transition pop: A's director activates a new camera with B's director as reference source.
	// B stays alive during the transition so the reference leaf can evaluate it.
	UComposableCameraTransitionBase* PopTransition = nullptr;
	ResumeEntry.Director->ActivateNewCameraWithReferenceSource(
		PlayerCameraManager,
		CameraClass,
		TransitionDataAsset,
		ResumeActivationParams,
		FOnCameraFinishConstructed{},
		PoppedEntry.Director,
		&PopTransition);

	if (PopTransition)
	{
		// Hold B in pending destruction until the transition finishes.
		PendingDestroyEntries.Add(PoppedEntry);

		PopTransition->OnTransitionFinishesDelegate.AddLambda(
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
		// Transition was nullptr after ActivateNewCameraWithReferenceSource (camera cut path).
		if (PoppedEntry.Director)
		{
			PoppedEntry.Director->DestroyAllCameras();
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

FComposableCameraPose UComposableCameraContextStack::Evaluate(float DeltaTime)
{
	if (Entries.Num() == 0)
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT("Context stack is empty."));
		return FComposableCameraPose{};
	}

	// Copy the active entry index rather than holding a reference — PopActiveContextInternal
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
	// pointing to them — the reference leaf calls Director->Evaluate() on the source context.
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

			UComposableCameraTransitionDataAsset* TransitionOverride = nullptr;
			if (RunningCamera->DefaultTransition)
			{
				UComposableCameraTransitionBase* DuplicatedTransition = DuplicateObject(RunningCamera->DefaultTransition, PCM);
				TransitionOverride = NewObject<UComposableCameraTransitionDataAsset>(PCM);
				TransitionOverride->Transition = DuplicatedTransition;
			}

			FComposableCameraActivateParams ActiveParams;
			ActiveParams.bPreserveCameraPose = RunningCamera->bDefaultPreserveCameraPose;

			PopActiveContextInternal(PCM, TransitionOverride, ActiveParams);
		}
	}

	return ResultPose;
}

void UComposableCameraContextStack::BuildDebugString(TStringBuilder<1024>& OutString) const
{
	OutString.Appendf(TEXT("Context Stack (depth: %d, pending destroy: %d)\n"), Entries.Num(), PendingDestroyEntries.Num());

	// Display stack from top to bottom.
	for (int32 i = Entries.Num() - 1; i >= 0; --i)
	{
		const FComposableCameraContextEntry& Entry = Entries[i];
		const bool bIsActive = (i == Entries.Num() - 1);
		const TCHAR* ActiveMarker = bIsActive ? TEXT("-> ") : TEXT("   ");
		const TCHAR* BaseMarker = (i == 0) ? TEXT(" [base]") : TEXT("");

		OutString.Appendf(TEXT("%s[%d] %s%s\n"), ActiveMarker, i, *Entry.ContextName.ToString(), BaseMarker);

		if (Entry.Director)
		{
			Entry.Director->BuildDebugString(OutString, 2);
		}
	}

	if (PendingDestroyEntries.Num() > 0)
	{
		OutString.Append(TEXT("Pending Destroy:\n"));
		for (const FComposableCameraContextEntry& Entry : PendingDestroyEntries)
		{
			OutString.Appendf(TEXT("   [pending] %s\n"), *Entry.ContextName.ToString());
		}
	}
}

void UComposableCameraContextStack::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UComposableCameraContextStack* This = CastChecked<UComposableCameraContextStack>(InThis);
	for (FComposableCameraContextEntry& Entry : This->Entries)
	{
		Collector.AddReferencedObject(Entry.Director);
	}
	for (FComposableCameraContextEntry& Entry : This->PendingDestroyEntries)
	{
		Collector.AddReferencedObject(Entry.Director);
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