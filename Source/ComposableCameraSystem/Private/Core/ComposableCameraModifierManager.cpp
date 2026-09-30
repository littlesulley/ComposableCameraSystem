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
		UComposableCameraNodeModifierDataAsset* ModifierAsset,
		uint64& NextRegistrationOrder)
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
			const FModifierEntry Entry {
				Modifier, ModifierAsset, NextRegistrationOrder++ };
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
		T_EffectiveModifier& OutEffectiveModifiers)
	{
		for (const auto& NodeModifier : Candidates)
		{
			const T_NodeClass& NodeClass = NodeModifier.Key;
			const TArray<FModifierEntry>& Modifiers = NodeModifier.Value;

			// Preserve the legacy Custom Modifier boundary. If the same
			// node-class candidate that would previously have won is Custom,
			// keep one whole-node winner instead of composing unknown side
			// effects with property overrides.
			const FModifierEntry* BestNodeEntry = nullptr;
			int32 BestNodePriority = TNumericLimits<int32>::Lowest();
			for (const FModifierEntry& Modifier : Modifiers)
			{
				if (Modifier.Modifier
					&& Modifier.Asset
					&& Modifier.Asset->MatchesCameraTags(CameraTags)
					&& Modifier.Asset->Priority >= BestNodePriority)
				{
					BestNodeEntry = &Modifier;
					BestNodePriority = Modifier.Asset->Priority;
				}
			}
			if (!BestNodeEntry)
			{
				continue;
			}

			T_PropertyModifier& EffectiveProperties =
				OutEffectiveModifiers.FindOrAdd(NodeClass);
			if (!BestNodeEntry->Modifier->UsesNodeTemplateOverride())
			{
				EffectiveProperties.Add(NAME_None, *BestNodeEntry);
				continue;
			}

			// Generic Node Type entries expose their ownership explicitly.
			// Resolve each property independently so disjoint lower-priority
			// candidates can coexist while overlaps retain existing priority
			// and tie behavior (later registered candidate wins on >=).
			for (const FModifierEntry& Modifier : Modifiers)
			{
				if (!Modifier.Modifier
					|| !Modifier.Asset
					|| !Modifier.Modifier->UsesNodeTemplateOverride()
					|| !Modifier.Asset->MatchesCameraTags(CameraTags))
				{
					continue;
				}

				for (const FName PropertyName
					: Modifier.Modifier->OverrideProperties)
				{
					if (PropertyName.IsNone())
					{
						continue;
					}

					const FModifierEntry* Existing =
						EffectiveProperties.Find(PropertyName);
					const int32 ExistingPriority =
						Existing && Existing->Asset
							? Existing->Asset->Priority
							: TNumericLimits<int32>::Lowest();
					if (Modifier.Asset->Priority >= ExistingPriority)
					{
						EffectiveProperties.FindOrAdd(PropertyName) = Modifier;
					}
				}
			}

			if (EffectiveProperties.IsEmpty())
			{
				OutEffectiveModifiers.Remove(NodeClass);
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
		for (auto& PropertyPair : EffectivePair.Value)
		{
			AddEntryRefs(PropertyPair.Value);
		}
	}

	Super::AddReferencedObjects(InThis, Collector);
}

void UComposableCameraModifierManager::AddModifier(UComposableCameraNodeModifierDataAsset* ModifierAsset)
{
	if (!ModifierAsset || ModifierAsset->Modifiers.IsEmpty())
	{
		return;
	}

	AddModifierEntries(
		ModifierData.ModifierData, ModifierAsset, NextRegistrationOrder);
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

FComposableCameraModifierUpdateResult
UComposableCameraModifierManager::FComposableCameraModifierData::UpdateEffectiveModifiers(AComposableCameraCameraBase* Camera)
{
	SCOPE_CYCLE_COUNTER(STAT_CCS_ModifierManager_UpdateEffectiveModifiers);
	TRACE_CPUPROFILER_EVENT_SCOPE(CCS_ModifierManager_UpdateEffectiveModifiers);

	FComposableCameraModifierUpdateResult Result;
	if (!Camera)
	{
		return Result;
	}

	// Build new effective camera modifiers.
	T_EffectiveModifier NewEffectiveModifiers {};
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
	
	int32 BestCameraTransitionPriority = TNumericLimits<int32>::Lowest();
	int32 BestModifierTransitionPriority = TNumericLimits<int32>::Lowest();
	uint64 BestCameraTransitionOrder = 0;
	uint64 BestModifierTransitionOrder = 0;
	bool bBestCameraTransitionEntering = false;
	bool bBestModifierTransitionEntering = false;

	auto ConsiderChange = [&Result, Camera, &BestCameraTransitionPriority,
		&BestModifierTransitionPriority, &BestCameraTransitionOrder,
		&BestModifierTransitionOrder, &bBestCameraTransitionEntering,
		&bBestModifierTransitionEntering](
			const FModifierEntry& Entry,
			bool bEntering,
			FName PropertyName)
	{
		if (!Entry.Asset)
		{
			return;
		}

		Result.bChanged = true;
		if (Entry.Asset->ApplyMode == EComposableCameraModifierApplyMode::ReactivateCamera)
		{
			if (!Result.bRequiresCameraReactivation)
			{
				Result.ReactivationReason = FString::Printf(
					TEXT("Legacy %s: %s%s%s"),
					bEntering ? TEXT("Enter") : TEXT("Exit"),
					*Entry.Asset->GetName(),
					PropertyName.IsNone() ? TEXT("") : TEXT("."),
					PropertyName.IsNone() ? TEXT("") : *PropertyName.ToString());
			}
			Result.bRequiresCameraReactivation = true;
			const bool bHigherRank =
				Entry.Asset->Priority > BestCameraTransitionPriority
				|| (Entry.Asset->Priority == BestCameraTransitionPriority
					&& ((bEntering && !bBestCameraTransitionEntering)
						|| (bEntering == bBestCameraTransitionEntering
							&& Entry.RegistrationOrder
								> BestCameraTransitionOrder)));
			if (bHigherRank)
			{
				Result.CameraTransition = bEntering
					? Entry.Asset->OverrideEnterTransition.Get()
					: Entry.Asset->OverrideExitTransition.Get();
				if (!Result.CameraTransition)
				{
					Result.CameraTransition = Camera->EnterTransition;
				}
				BestCameraTransitionPriority = Entry.Asset->Priority;
				BestCameraTransitionOrder = Entry.RegistrationOrder;
				bBestCameraTransitionEntering = bEntering;
			}
			return;
		}

		const bool bHigherRank =
			Entry.Asset->Priority > BestModifierTransitionPriority
				|| (Entry.Asset->Priority == BestModifierTransitionPriority
					&& ((bEntering && !bBestModifierTransitionEntering)
						|| (bEntering == bBestModifierTransitionEntering
							&& Entry.RegistrationOrder
								> BestModifierTransitionOrder)));
		if (bHigherRank)
		{
			Result.ModifierTransition = bEntering
				? Entry.Asset->OverrideEnterValueTransition.Get()
				: Entry.Asset->OverrideExitValueTransition.Get();
			BestModifierTransitionPriority = Entry.Asset->Priority;
			BestModifierTransitionOrder = Entry.RegistrationOrder;
			bBestModifierTransitionEntering = bEntering;
		}
	};

	const auto ConsiderReplacement = [&ConsiderChange](
		const FModifierEntry& OldModifier,
		const FModifierEntry& NewModifier,
		FName PropertyName)
	{
		const int32 NewPriority = NewModifier.Asset
			? NewModifier.Asset->Priority
			: TNumericLimits<int32>::Lowest();
		const int32 OldPriority = OldModifier.Asset
			? OldModifier.Asset->Priority
			: TNumericLimits<int32>::Lowest();
		if (NewPriority >= OldPriority)
		{
			ConsiderChange(NewModifier, true, PropertyName);
			if (OldModifier.Asset
				&& NewModifier.Asset
				&& OldModifier.Asset->ApplyMode
					!= NewModifier.Asset->ApplyMode)
			{
				ConsiderChange(OldModifier, false, PropertyName);
			}
		}
		else
		{
			ConsiderChange(OldModifier, false, PropertyName);
			if (OldModifier.Asset
				&& NewModifier.Asset
				&& OldModifier.Asset->ApplyMode
					!= NewModifier.Asset->ApplyMode)
			{
				ConsiderChange(NewModifier, true, PropertyName);
			}
		}
	};

	// Compare old and new winners per (exact node class, property).
	for (const auto& NodeModifiers : EffectiveModifiers)
	{
		const T_NodeClass& NodeClass = NodeModifiers.Key;
		const T_PropertyModifier* NewProperties =
			NewEffectiveModifiers.Find(NodeClass);

		for (const auto& PropertyModifier : NodeModifiers.Value)
		{
			const FName PropertyName = PropertyModifier.Key;
			const FModifierEntry& OldModifier = PropertyModifier.Value;
			const FModifierEntry* NewModifier =
				NewProperties ? NewProperties->Find(PropertyName) : nullptr;
			if (!NewModifier)
			{
				ConsiderChange(OldModifier, false, PropertyName);
			}
			else if (*NewModifier != OldModifier)
			{
				ConsiderReplacement(OldModifier, *NewModifier, PropertyName);
			}
		}
	}

	// See if there are newly effective property winners.
	for (const auto& NodeModifiers : NewEffectiveModifiers)
	{
		const T_NodeClass& NodeClass = NodeModifiers.Key;
		const T_PropertyModifier* OldProperties =
			EffectiveModifiers.Find(NodeClass);

		for (const auto& PropertyModifier : NodeModifiers.Value)
		{
			if (!OldProperties
				|| !OldProperties->Contains(PropertyModifier.Key))
			{
				ConsiderChange(
					PropertyModifier.Value, true, PropertyModifier.Key);
			}
		}
	}

	EffectiveModifiers = MoveTemp(NewEffectiveModifiers);
	
	return Result;
}
