// Copyright 2026 Sulley. All Rights Reserved.

#include "Core/ComposableCameraModifierManager.h"

#include "ComposableCameraSystemModule.h"   // STATGROUP_CCS
#include "DataAssets/ComposableCameraModifierDataAsset.h"
#include "Modifiers/ComposableCameraModifierBase.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"

namespace
{
	void AddModifierEntries(
		T_NodeModifierArray& NodeModifierData,
		UComposableCameraNodeModifierDataAsset* ModifierAsset)
	{
		for (UComposableCameraModifierBase* Modifier : ModifierAsset->Modifiers)
		{
			const TSubclassOf<UComposableCameraCameraNodeBase> NodeClass =
				Modifier ? Modifier->GetTargetNodeClass() : nullptr;
			if (!NodeClass)
			{
				continue;
			}

			TArray<FModifierEntry>& NodeModifiers = NodeModifierData.FindOrAdd(NodeClass);
			const FModifierEntry Entry { Modifier, ModifierAsset };
			if (!NodeModifiers.Contains(Entry))
			{
				NodeModifiers.Add(Entry);
			}
		}
	}

	void RemoveModifierEntries(
		T_NodeModifierArray& NodeModifierData,
		UComposableCameraNodeModifierDataAsset* ModifierAsset)
	{
		for (UComposableCameraModifierBase* Modifier : ModifierAsset->Modifiers)
		{
			const TSubclassOf<UComposableCameraCameraNodeBase> NodeClass =
				Modifier ? Modifier->GetTargetNodeClass() : nullptr;
			if (!NodeClass)
			{
				continue;
			}

			if (TArray<FModifierEntry>* NodeModifiers = NodeModifierData.Find(NodeClass))
			{
				NodeModifiers->Remove(FModifierEntry { Modifier, ModifierAsset });
				if (NodeModifiers->IsEmpty())
				{
					NodeModifierData.Remove(NodeClass);
				}
			}
		}
	}

	void SelectBestModifiers(
		const T_NodeModifierArray& Candidates,
		const FGameplayTagContainer& CameraTags,
		T_NodeModifier& InOutEffectiveModifiers)
	{
		for (const auto& NodeModifier : Candidates)
		{
			const T_NodeClass& NodeClass = NodeModifier.Key;
			const TArray<FModifierEntry>& Modifiers = NodeModifier.Value;

			int32 BestPriority = TNumericLimits<int32>::Lowest();
			if (const FModifierEntry* Existing = InOutEffectiveModifiers.Find(NodeClass))
			{
				if (Existing->Asset)
				{
					BestPriority = Existing->Asset->Priority;
				}
			}

			for (const FModifierEntry& Modifier : Modifiers)
			{
				if (Modifier.Modifier && Modifier.Asset
					&& Modifier.Asset->MatchesCameraTags(CameraTags)
					&& Modifier.Asset->Priority >= BestPriority)
				{
					BestPriority = Modifier.Asset->Priority;
					InOutEffectiveModifiers.FindOrAdd(NodeClass) = Modifier;
				}
			}
		}
	}
}

void UComposableCameraModifierManager::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UComposableCameraModifierManager* This = CastChecked<UComposableCameraModifierManager>(InThis);

	auto AddEntryRefs = [&Collector](FModifierEntry& Entry)
	{
		if (Entry.Modifier)
		{
			Collector.AddReferencedObject(Entry.Modifier);
		}
		if (Entry.Asset)
		{
			Collector.AddReferencedObject(Entry.Asset);
		}
	};

	for (auto& NodeClassPair : This->ModifierData.ModifierData)
	{
		for (FModifierEntry& Entry : NodeClassPair.Value)
		{
			AddEntryRefs(Entry);
		}
	}

	for (auto& EffectivePair : This->ModifierData.EffectiveModifiers)
	{
		AddEntryRefs(EffectivePair.Value);
	}

	Super::AddReferencedObjects(InThis, Collector);
}

void UComposableCameraModifierManager::AddModifier(UComposableCameraNodeModifierDataAsset* ModifierAsset)
{
	if (!ModifierAsset || ModifierAsset->Modifiers.IsEmpty())
	{
		return;
	}

	AddModifierEntries(ModifierData.ModifierData, ModifierAsset);
}

void UComposableCameraModifierManager::RemoveModifier(UComposableCameraNodeModifierDataAsset* ModifierAsset)
{
	if (!ModifierAsset || ModifierAsset->Modifiers.IsEmpty())
	{
		return;
	}

	RemoveModifierEntries(ModifierData.ModifierData, ModifierAsset);
}

DECLARE_CYCLE_STAT(TEXT("ModifierManager UpdateEffective"), STAT_CCS_ModifierManager_UpdateEffectiveModifiers, STATGROUP_CCS);

std::pair<bool, UComposableCameraTransitionBase*>
UComposableCameraModifierManager::FComposableCameraModifierData::UpdateEffectiveModifiers(AComposableCameraCameraBase* Camera)
{
	SCOPE_CYCLE_COUNTER(STAT_CCS_ModifierManager_UpdateEffectiveModifiers);
	TRACE_CPUPROFILER_EVENT_SCOPE(CCS_ModifierManager_UpdateEffectiveModifiers);

	// Build new effective camera modifiers.
	T_NodeModifier NewEffectiveModifiers {};
	SelectBestModifiers(ModifierData, Camera->CameraTags, NewEffectiveModifiers);

	// Filter invalid for camera node ownership
	TArray<T_NodeClass> RemovalKeys;
	for (const auto& NodeModifier : NewEffectiveModifiers)
	{
		if (!Camera->GetNodeByClass(NodeModifier.Key))
		{
			RemovalKeys.Add(NodeModifier.Key);
		}
	}
	for (auto& Key : RemovalKeys)
	{
		NewEffectiveModifiers.Remove(Key);
	}
	
	bool bModifierChanged = false;
	UComposableCameraTransitionBase* Transition = nullptr;
	int BestPriorityForTransition = TNumericLimits<int32>::Lowest();

	// Compare with old effective modifiers and determine if anything is changed.
	for (const auto& NodeModifiers : EffectiveModifiers)
	{
		const T_NodeClass& NodeClass = NodeModifiers.Key;
		const FModifierEntry& OldModifier = NodeModifiers.Value;
		
		if (!NewEffectiveModifiers.Contains(NodeClass))
		{
			bModifierChanged = true;

			if (OldModifier.Asset->Priority > BestPriorityForTransition)
			{
				Transition = OldModifier.Asset->OverrideExitTransition.Get()
						   ? OldModifier.Asset->OverrideExitTransition.Get()
						   : Camera->EnterTransition;
				BestPriorityForTransition = OldModifier.Asset->Priority;
			}
		}
		else
		{
			const FModifierEntry& NewModifier = NewEffectiveModifiers[NodeClass];
			if (NewModifier.Modifier != OldModifier.Modifier)
			{
				bModifierChanged = true;

				if (NewModifier.Asset->Priority > BestPriorityForTransition)
				{
					Transition = NewModifier.Asset->OverrideEnterTransition.Get()
							   ? NewModifier.Asset->OverrideEnterTransition.Get()
							   : Camera->EnterTransition;
					BestPriorityForTransition = NewModifier.Asset->Priority;
				}
				
				// Theoretically this branch will never be reached because NewModifier always has a higher priority then OldModifier.
				else if (OldModifier.Asset->Priority > BestPriorityForTransition) 
				{
					Transition = OldModifier.Asset->OverrideExitTransition.Get()
							   ? OldModifier.Asset->OverrideExitTransition.Get()
							   : Camera->EnterTransition;
					BestPriorityForTransition = OldModifier.Asset->Priority;
				}
			}
		}
	}

	// See if there are newly added modifiers.
	for (const auto& NodeModifiers : NewEffectiveModifiers)
	{
		const T_NodeClass& NodeClass = NodeModifiers.Key;
		const FModifierEntry& NewModifier = NodeModifiers.Value;

		if (!EffectiveModifiers.Contains(NodeClass))
		{
			bModifierChanged = true;

			if (NewModifier.Asset->Priority > BestPriorityForTransition)
			{
				Transition = NewModifier.Asset->OverrideEnterTransition.Get()
						   ? NewModifier.Asset->OverrideEnterTransition.Get()
						   : Camera->EnterTransition;
				BestPriorityForTransition = NewModifier.Asset->Priority;
			}
		}
	}

	EffectiveModifiers = MoveTemp(NewEffectiveModifiers);
	
	return { bModifierChanged, Transition };
}
