// Copyright 2026 Sulley. All Rights Reserved.

#include "Core/ComposableCameraModifierManager.h"

#include "ComposableCameraSystemModule.h"   // STATGROUP_CCS
#include "DataAssets/ComposableCameraModifierDataAsset.h"
#include "Modifiers/ComposableCameraModifierBase.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"

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

	for (auto& TagPair : This->ModifierData.ModifierData)
	{
		for (auto& NodeClassPair : TagPair.Value)
		{
			for (FModifierEntry& Entry : NodeClassPair.Value)
			{
				AddEntryRefs(Entry);
			}
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
	if (!ModifierAsset || ModifierAsset->CameraTags.IsEmpty() || ModifierAsset->Modifiers.IsEmpty())
	{
		return;
	}

	for (FGameplayTag CameraTag : ModifierAsset->CameraTags)
	{
		if (!ModifierData.ModifierData.Contains(CameraTag))
		{
			ModifierData.ModifierData.Emplace(CameraTag, T_NodeModifierArray{});
		}
		
		for (UComposableCameraModifierBase* Modifier : ModifierAsset->Modifiers)
		{
			if (!Modifier || !Modifier->NodeClass)
			{
				continue;
			}
			
			TSubclassOf<UComposableCameraCameraNodeBase> NodeClass = Modifier->NodeClass;
			auto& AllNodeModifiers = ModifierData.ModifierData[CameraTag];
			auto* NodeModifiers = AllNodeModifiers.Find(NodeClass);

			if (!NodeModifiers)
			{
				AllNodeModifiers.Emplace(NodeClass, TArray<FModifierEntry>{});
				NodeModifiers = AllNodeModifiers.Find(NodeClass);
			}

			if (!NodeModifiers->Contains(FModifierEntry{ Modifier, ModifierAsset }))
			{
				NodeModifiers->Emplace(Modifier, ModifierAsset);
			}
		}
	}
}

void UComposableCameraModifierManager::RemoveModifier(UComposableCameraNodeModifierDataAsset* ModifierAsset)
{
	if (!ModifierAsset || ModifierAsset->CameraTags.IsEmpty() || ModifierAsset->Modifiers.IsEmpty())
	{
		return;
	}

	for (FGameplayTag CameraTag : ModifierAsset->CameraTags)
	{
		if (!ModifierData.ModifierData.Contains(CameraTag))
		{
			continue;
		}
		
		for (UComposableCameraModifierBase* Modifier : ModifierAsset->Modifiers)
		{
			if (!Modifier || !Modifier->NodeClass)
			{
				continue;
			}
			
			TSubclassOf<UComposableCameraCameraNodeBase> NodeClass = Modifier->NodeClass;
			auto& AllNodeModifiers = ModifierData.ModifierData[CameraTag];

			if (auto* NodeModifiers = AllNodeModifiers.Find(NodeClass))
			{
				auto Index = NodeModifiers->Find(FModifierEntry{ Modifier, ModifierAsset });
				if (Index != INDEX_NONE)
				{
					NodeModifiers->RemoveAt(Index);
				}
			}
		}
	}
}

DECLARE_CYCLE_STAT(TEXT("ModifierManager UpdateEffective"), STAT_CCS_ModifierManager_UpdateEffectiveModifiers, STATGROUP_CCS);

std::pair<bool, UComposableCameraTransitionBase*>
UComposableCameraModifierManager::FComposableCameraModifierData::UpdateEffectiveModifiers(AComposableCameraCameraBase* Camera)
{
	SCOPE_CYCLE_COUNTER(STAT_CCS_ModifierManager_UpdateEffectiveModifiers);
	TRACE_CPUPROFILER_EVENT_SCOPE(CCS_ModifierManager_UpdateEffectiveModifiers);

	FGameplayTag CameraTag = Camera->CameraTag;

	// Build new effective camera modifiers.
	T_NodeModifier NewEffectiveModifiers {};

	if (const auto* NodeModifiers = ModifierData.Find(CameraTag))
	{
		for (auto& NodeModifier : *NodeModifiers)
		{
			const T_NodeClass& NodeClass = NodeModifier.Key;
			const TArray<FModifierEntry>& Modifiers = NodeModifier.Value;

			int BestPriority = TNumericLimits<int32>::Lowest();
			FModifierEntry BestModifier { nullptr, nullptr };

			for (const FModifierEntry& Modifier : Modifiers)
			{
				if (Modifier.Modifier && Modifier.Asset && Modifier.Asset->Priority >= BestPriority)
				{
					BestPriority = Modifier.Asset->Priority;
					BestModifier = Modifier;
				}
			}

			if (BestModifier.Modifier && BestModifier.Asset)
			{
				NewEffectiveModifiers.Add(NodeClass, BestModifier);	
			}
		}
	}

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
				Transition = OldModifier.Asset->OverrideExitTransition
						   ? OldModifier.Asset->OverrideExitTransition
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
					Transition = NewModifier.Asset->OverrideEnterTransition
							   ? NewModifier.Asset->OverrideEnterTransition
							   : Camera->EnterTransition;
					BestPriorityForTransition = NewModifier.Asset->Priority;
				}
				
				// Theoretically this branch will never be reached because NewModifier always has a higher priority then OldModifier.
				else if (OldModifier.Asset->Priority > BestPriorityForTransition) 
				{
					Transition = OldModifier.Asset->OverrideExitTransition
							   ? OldModifier.Asset->OverrideExitTransition
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
				Transition = NewModifier.Asset->OverrideEnterTransition
						   ? NewModifier.Asset->OverrideEnterTransition
						   : Camera->EnterTransition;
				BestPriorityForTransition = NewModifier.Asset->Priority;
			}
		}
	}

	EffectiveModifiers = MoveTemp(NewEffectiveModifiers);
	
	return { bModifierChanged, Transition };
}